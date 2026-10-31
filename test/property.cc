// Dependency-free property tests for the arithmetic kernels, checked against a
// reference schoolbook multiply on 32-bit words and against algebraic identities.
// Runs under CTest, so CI's sanitizer jobs cover every multiplication and division path.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>
// The size helpers, kernels and cutoffs are private; the size-guard and buffer checks below reach them directly.
#define UINT_T_PUBLIC_IMPLEMENTATION
#include "uinteger_t.hh"

static std::mt19937_64 rng(20261030);
static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); if (++failures > 10) std::exit(1); } } while (0)

using U = uinteger_t;
using digit = U::digit;
static constexpr std::size_t limb_words = sizeof(digit) / 4;

// Little-endian 32-bit words, trimmed.
using words = std::vector<std::uint32_t>;

static void trim(words& w) {
	while (!w.empty() && !w.back()) w.pop_back();
}

static words random_words(std::size_t n) {
	words w(n);
	int style = static_cast<int>(rng() % 6);
	for (auto& x : w) {
		auto r = static_cast<std::uint32_t>(rng());
		switch (style) {
			case 0: x = r; break;
			case 1: x = 0xffffffffu; break;
			case 2: x = rng() % 4 ? 0 : r; break;
			case 3: x = (rng() & 1) ? 0xffffffffu : 0; break;
			case 4: x = r >> (rng() % 32); break;
			default: x = r; break;
		}
	}
	if (n) w.back() |= 1u << (rng() % 32);
	return w;
}

static uinteger_t to_uint(const words& w) {
	std::string bytes;
	for (auto i = w.size(); i--;) {
		for (int s = 24; s >= 0; s -= 8) bytes.push_back(static_cast<char>(w[i] >> s));
	}
	return bytes.empty() ? uinteger_t(0) : uinteger_t(bytes, 256);
}

static words from_uint(const uinteger_t& u) {
	words w;
	if (!u) return w;
	auto bytes = u.str(256);
	std::uint32_t cur = 0;
	int shift = 0;
	for (auto i = bytes.size(); i--;) {
		cur |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << shift;
		shift += 8;
		if (shift == 32) { w.push_back(cur); cur = 0; shift = 0; }
	}
	if (shift) w.push_back(cur);
	trim(w);
	return w;
}

static words ref_mul(const words& a, const words& b) {
	words r(a.size() + b.size(), 0);
	for (std::size_t j = 0; j < b.size(); ++j) {
		std::uint64_t c = 0;
		for (std::size_t i = 0; i < a.size(); ++i) {
			std::uint64_t t = static_cast<std::uint64_t>(a[i]) * b[j] + r[i + j] + c;
			r[i + j] = static_cast<std::uint32_t>(t);
			c = t >> 32;
		}
		r[a.size() + j] = static_cast<std::uint32_t>(c);
	}
	trim(r);
	return r;
}

// Sizes in 32-bit words that hit the schoolbook, Karatsuba, odd-split and lopsided paths.
static std::size_t pick_words(std::size_t big) {
	switch (rng() % 5) {
		case 0: return 1 + rng() % 8;
		case 1: return 1 + rng() % 80;
		case 2: return 60 + rng() % 140;
		case 3: return 1 + rng() % big;
		default: return 1 + rng() % 300;
	}
}

static void check_mul(const words& a, const words& b) {
	auto ua = to_uint(a), ub = to_uint(b);
	auto expect = ref_mul(a, b);
	CHECK(from_uint(ua * ub) == expect, "mul %zu x %zu words", a.size(), b.size());
	CHECK(from_uint(ub * ua) == expect, "mul commuted %zu x %zu words", b.size(), a.size());
	auto acc = ua;
	acc *= ub;
	CHECK(from_uint(acc) == expect, "mul in place %zu x %zu words", a.size(), b.size());
}

// Squares: the same operand takes the squaring kernels, a copy of it the general multiply.
static void check_sqr(const words& a) {
	auto ua = to_uint(a);
	auto expect = ref_mul(a, a);
	CHECK(from_uint(ua * ua) == expect, "square %zu words", a.size());
	auto copy = ua;
	CHECK(from_uint(ua * copy) == expect, "square as product of copies %zu words", a.size());
	auto acc = ua;
	acc *= acc;
	CHECK(from_uint(acc) == expect, "square in place %zu words", a.size());
}

static std::vector<digit> random_limbs(std::size_t n) {
	auto w = random_words(n * limb_words);
	std::vector<digit> limbs(n);
	for (std::size_t i = 0; i < n; ++i) {
		std::uint64_t x = 0;
		for (std::size_t j = 0; j < limb_words; ++j) x |= static_cast<std::uint64_t>(w[i * limb_words + j]) << (32 * j);
		limbs[i] = static_cast<digit>(x);
	}
	return limbs;
}

static words limbs_to_words(const digit* p, std::size_t n) {
	words w;
	for (std::size_t i = 0; i < n; ++i) {
		for (std::size_t j = 0; j < limb_words; ++j) w.push_back(static_cast<std::uint32_t>(static_cast<std::uint64_t>(p[i]) >> (32 * j)));
	}
	trim(w);
	return w;
}

// The raw kernels on dirty result and scratch buffers with a guard after each: a kernel that
// writes past its result, outgrows its scratch size or reads scratch it never wrote fails.
static const digit guard_pattern = static_cast<digit>(0x5a5a5a5a5a5a5a5aull);
static const std::size_t guard_limbs = 4;

static void dirty(std::vector<digit>& buf, std::size_t n) {
	for (std::size_t i = 0; i < n; ++i) buf[i] = static_cast<digit>(rng());
	for (std::size_t i = 0; i < guard_limbs; ++i) buf[n + i] = guard_pattern;
}

static bool guard_intact(const std::vector<digit>& buf, std::size_t n) {
	for (std::size_t i = 0; i < guard_limbs; ++i) if (buf[n + i] != guard_pattern) return false;
	return true;
}

static void check_kernel_mul(const std::vector<digit>& a, const std::vector<digit>& b) {
	const std::size_t an = a.size(), bn = b.size();
	const std::size_t sn = U::k_mul_scratch_size(an, bn);
	std::vector<digit> r(an + bn + guard_limbs), s(sn + guard_limbs);
	dirty(r, an + bn);
	dirty(s, sn);
	U::k_mul(r.data(), a.data(), an, b.data(), bn, s.data());
	CHECK(limbs_to_words(r.data(), an + bn) == ref_mul(limbs_to_words(a.data(), an), limbs_to_words(b.data(), bn)), "k_mul %zu x %zu limbs", an, bn);
	CHECK(guard_intact(r, an + bn) && guard_intact(s, sn), "k_mul %zu x %zu limbs wrote past its buffers (scratch %zu)", an, bn, sn);
	CHECK(sn <= 8 * (an + bn) + 64, "k_mul %zu x %zu limbs asks for %zu scratch limbs", an, bn, sn);
}

static void check_kernel_sqr(const std::vector<digit>& a) {
	const std::size_t n = a.size();
	const std::size_t sn = U::k_sqr_scratch_size(n);
	std::vector<digit> r(2 * n + guard_limbs), s(sn + guard_limbs);
	dirty(r, 2 * n);
	dirty(s, sn);
	U::k_sqr(r.data(), a.data(), n, s.data());
	auto w = limbs_to_words(a.data(), n);
	CHECK(limbs_to_words(r.data(), 2 * n) == ref_mul(w, w), "k_sqr %zu limbs", n);
	CHECK(guard_intact(r, 2 * n) && guard_intact(s, sn), "k_sqr %zu limbs wrote past its buffers (scratch %zu)", n, sn);
	CHECK(sn <= 8 * n + 64, "k_sqr %zu limbs asks for %zu scratch limbs", n, sn);
}

// Every size pair up to a small bound, then the sizes around each cutoff (and its multiples)
// against the shapes that steer the dispatch: balanced, one or two short, the Toom-3 third-piece
// boundary, the Karatsuba half boundary, and the lopsided chunking.
static void check_kernel_sizes() {
	for (std::size_t an = 1; an <= 40; ++an) {
		check_kernel_sqr(random_limbs(an));
		for (std::size_t bn = 1; bn <= an; ++bn) check_kernel_mul(random_limbs(an), random_limbs(bn));
	}
	std::vector<std::size_t> sizes;
	for (std::size_t t : {U::mul_karatsuba_threshold, U::mul_toom3_threshold, U::sqr_karatsuba_threshold, U::sqr_toom3_threshold}) {
		for (std::size_t m : {1, 2, 3}) {
			for (std::size_t d = 0; d < 5; ++d) {
				if (m * t + d >= 2) sizes.push_back(m * t + d - 2);
			}
		}
	}
	std::sort(sizes.begin(), sizes.end());
	sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
	for (std::size_t n : sizes) {
		if (!n) continue;
		check_kernel_sqr(random_limbs(n));
		std::vector<std::size_t> others = {n, n - 1, n - 2, n - n / 6, (2 * n + 2) / 3, 2 * n / 3, n / 2 + 1, (n + 1) / 2, n / 2, n / 3 + 1};
		for (std::size_t bn : others) {
			if (!bn || bn > n) continue;
			check_kernel_mul(random_limbs(n), random_limbs(bn));
			check_kernel_mul(random_limbs(2 * n + 1), random_limbs(bn));
		}
	}
}

static void check_div(const uinteger_t& a, const uinteger_t& b, const char* what) {
	auto qr = a.divmod(b);
	CHECK(qr.second < b, "%s: remainder not below divisor", what);
	CHECK(qr.first * b + qr.second == a, "%s: q*b + r != a", what);
	CHECK(a / b == qr.first && a % b == qr.second, "%s: / and %% disagree with divmod", what);
}

// A value from random 32-bit words, built with shifts so it is the same at either limb width.
static uinteger_t random_value(std::size_t nwords) {
	auto w = random_words(nwords);
	uinteger_t v;
	for (auto i = w.size(); i--;) {
		v <<= 32;
		v |= uinteger_t(w[i]);
	}
	return v;
}

// Digits by repeated division with the general operators, independent of digits().
static std::vector<unsigned> ref_digits(uinteger_t v, int base) {
	std::vector<unsigned> d;
	const uinteger_t b(base);
	while (v) {
		d.push_back(static_cast<unsigned>(v % b));
		v = v / b;
	}
	if (d.empty()) d.push_back(0);
	std::reverse(d.begin(), d.end());
	return d;
}

template <typename F>
static std::string what_of(F f) {
	try { f(); } catch (const std::invalid_argument& e) { return e.what(); }
	return "no throw";
}

// digits() against the reference, then round trips through from_digits().
template <typename Result>
static void check_digits(const uinteger_t& v, int base, bool with_ref) {
	using value_type = typename Result::value_type;
	Result out;
	v.digits(out, base);
	if (with_ref) {
		auto ref = ref_digits(v, base);
		bool same = ref.size() == out.size();
		for (std::size_t i = 0; same && i < ref.size(); ++i) same = static_cast<unsigned char>(out[i]) == ref[i];
		CHECK(same, "digits base %d: %zu digits, reference %zu", base, out.size(), ref.size());
	}
	CHECK(uinteger_t::from_digits(out.data(), out.size(), base) == v, "from_digits(digits) base %d", base);

	Result padded(1 + rng() % 5, static_cast<value_type>(0));
	padded.insert(padded.end(), out.begin(), out.end());
	CHECK(uinteger_t::from_digits(padded.data(), padded.size(), base) == v, "from_digits with leading zeros base %d", base);

	Result tail(2, static_cast<value_type>(7));
	v.digits(tail, base);
	CHECK(tail.size() == out.size() + 2 && tail[0] == 7 && tail[1] == 7 && std::equal(out.begin(), out.end(), tail.begin() + 2),
	      "digits appends, base %d", base);
}

// Digits by single-limb division, a reference cheap enough for many small values.
static constexpr std::size_t digit_bits = uinteger_t::digit_bits;

static std::vector<unsigned char> chunk_digits(const uinteger_t& v, int base) {
	std::vector<unsigned char> d;
	uinteger_t q = v;
	while (q) d.push_back(static_cast<unsigned char>(q.divmod_small(static_cast<uinteger_t::digit>(base))));
	if (d.empty()) d.push_back(0);
	std::reverse(d.begin(), d.end());
	return d;
}

static bool is_power_of_two(int base) {
	return (base & (base - 1)) == 0;
}

// The inputs where the fraction method is tightest: b^j and D^j (D is the largest power of the
// base in a limb) and their neighbours, powers of two around limb boundaries, and all-ones.
static std::vector<uinteger_t> radix_edges(int base, std::size_t max_limbs) {
	std::vector<uinteger_t> v;
	unsigned k;
	const uinteger_t::digit D = uinteger_t::base_chunk(base, k);
	for (const auto& mult : {uinteger_t(base), uinteger_t(D)}) {
		uinteger_t p(1);
		while (p.size() <= max_limbs) {
			if (p > uinteger_t(1)) v.push_back(p - uinteger_t(1));
			v.push_back(p);
			v.push_back(p + uinteger_t(1));
			p *= mult;
		}
	}
	for (std::size_t bit = 1; bit <= max_limbs * digit_bits; bit += digit_bits / 2) {
		const uinteger_t p = uinteger_t(1) << bit;
		v.push_back(p - uinteger_t(1));
		v.push_back(p);
		v.push_back(p + uinteger_t(1));
	}
	return v;
}

static void check_against_chunks(const uinteger_t& v, int base) {
	const auto ref = chunk_digits(v, base);
	std::string got;
	v.digits(got, base);
	bool same = got.size() == ref.size();
	for (std::size_t i = 0; same && i < ref.size(); ++i) same = static_cast<unsigned char>(got[i]) == ref[i];
	CHECK(same, "digits of %zu limbs, %zu bits, base %d: %zu digits, reference %zu", v.size(), v.bits(), base, got.size(), ref.size());
	CHECK(uinteger_t::from_digits(got.data(), got.size(), base) == v, "from_digits(digits) of %zu limbs, base %d", v.size(), base);
}

// How many of the edge inputs a table built with the given guard limbs and 1/2 offset gets
// wrong (guard < 0 builds the table as production does).
static int mutant_failures(int base, int guard, bool half, std::size_t max_limbs) {
	const auto& rx = uinteger_t::_radix_for(base);
	int bad = 0;
	for (const auto& v : radix_edges(base, max_limbs)) {
		const std::size_t n = v.size();
		if (!n) continue;
		const bool by_bits = n <= uinteger_t::radix_unrolled;
		std::unique_ptr<uinteger_t::radix_table> t(uinteger_t::_make_radix_table(rx, n, by_bits ? v.bits() : n * digit_bits, guard, half));
		std::vector<unsigned char> buffer(static_cast<std::size_t>(t->chunks) * rx.k);
		const auto count = uinteger_t::_encode_radix(rx, *t, v.data(), n, buffer.data());
		const auto ref = chunk_digits(v, base);
		bool same = count == ref.size();
		for (std::size_t i = 0; same && i < count; ++i) same = buffer[i] == ref[i];
		bad += !same;
	}
	return bad;
}

// The values of exactly `limbs` limbs next to b^j, D^j and 2^(limbs * bits) - 1: the first and
// last power that fit, each with its neighbours.
static std::vector<uinteger_t> radix_edges_at(int base, std::size_t limbs) {
	std::vector<uinteger_t> v;
	unsigned k;
	const uinteger_t::digit D = uinteger_t::base_chunk(base, k);
	for (const auto& mult : {uinteger_t(base), uinteger_t(D)}) {
		uinteger_t p(1), last;
		bool seen = false;
		while (p.size() <= limbs) {
			if (p.size() == limbs) {
				if (!seen) {
					for (const auto& q : {p - uinteger_t(1), p, p + uinteger_t(1)}) v.push_back(q);
					seen = true;
				}
				last = p;
			}
			p *= mult;
		}
		for (const auto& q : {last - uinteger_t(1), last, last + uinteger_t(1)}) v.push_back(q);
	}
	v.push_back((uinteger_t(1) << (limbs * digit_bits)) - uinteger_t(1));
	return v;
}

static void check_radix_kernel() {
	// Every base that is not a power of two, at the sizes of the unrolled kernels.
	for (int base = 3; base <= 255; ++base) {
		if (is_power_of_two(base)) continue;
		for (const auto& v : radix_edges(base, 5)) check_against_chunks(v, base);
	}
	for (int base : {3, 5, 6, 7, 10, 11, 13, 36, 57, 58, 62, 85, 100, 255}) {
		for (const auto& v : radix_edges(base, 9)) check_against_chunks(v, base);
		// Through the general kernel, around its size limit (which divides instead), and above.
		for (std::size_t limbs : {10, 11, 12, 15, 16, 17, 31, 32, 33, 64, 65, 127, 128, 129, 130, 150}) {
			for (const auto& v : radix_edges_at(base, limbs)) check_against_chunks(v, base);
		}
	}
	// The tables built without a guard limb or without the 1/2 offset must fail on these inputs.
	// A base needs its guard limb at every size once D / B is large; 3 and 255 do at either
	// limb width, base 10 only at 64-bit.
	for (int base : {3, 255, 10}) {
		unsigned k;
		const double r = static_cast<double>(uinteger_t::base_chunk(base, k)) / std::ldexp(1.0, static_cast<int>(digit_bits));
		CHECK(mutant_failures(base, -1, true, 12) == 0, "tables of base %d", base);
		if (r >= 0.5) CHECK(mutant_failures(base, 0, true, 12) > 0, "no guard limb survives in base %d", base);
	}
	for (int base : {10, 36, 58, 62, 255}) {
		CHECK(mutant_failures(base, -1, true, 12) == 0, "tables of base %d", base);
		CHECK(mutant_failures(base, -1, false, 12) > 0, "no 1/2 offset survives in base %d", base);
	}
	std::unique_ptr<uinteger_t::radix_table> t58(uinteger_t::_make_radix_table(uinteger_t::_radix_for(58), 4, 4 * digit_bits));
	std::unique_ptr<uinteger_t::radix_table> t255(uinteger_t::_make_radix_table(uinteger_t::_radix_for(255), 4, 4 * digit_bits));
	CHECK(t58->guard == 0, "base 58 guard");
	CHECK(t255->guard == 1, "base 255 guard");
}

// A digit that is not below the base is found wherever it sits in a run of valid ones, and
// the message names the first one counted from the most significant end.
static void check_digit_errors() {
	for (int base : {3, 10, 58, 200}) {
		for (std::size_t n : {1u, 7u, 8u, 9u, 22u, 23u, 40u, 41u, 100u, 257u}) {
			std::string valid(n, '\0');
			for (auto& c : valid) c = static_cast<char>(rng() % static_cast<unsigned>(base));
			CHECK(what_of([&] { uinteger_t::from_digits(valid.data(), n, base); }) == "no throw", "valid run base %d", base);
			for (std::size_t at : {std::size_t(0), n / 2, n - 1}) {
				for (unsigned bad : {static_cast<unsigned>(base), 255u}) {
					std::string s = valid;
					s[at] = static_cast<char>(bad);
					if (at + 1 < n) s[n - 1] = static_cast<char>(bad == 255u ? base : 255);  // a later one is ignored
					const std::string expect = "Error: Not a digit in base " + std::to_string(base) + ": " + std::to_string(bad) + " at " + std::to_string(n - at);
					CHECK(what_of([&] { uinteger_t::from_digits(s.data(), n, base); }) == expect, "bad digit %u at %zu of %zu, base %d", bad, at, n, base);
				}
			}
		}
	}
}

static void check_digit_values() {
	// b^k and b^k - 1 have known digits at every size, across limb and chunk boundaries.
	for (int base = 2; base <= 256; ++base) {
		uinteger_t power(1);
		// Chunks of a limb hold at most 40 digits in a base above 40; low bases and the
		// bit-sliced bases get longer runs.
		const unsigned kmax = (base <= 40 || (base & (base - 1)) == 0) ? 150 : 40;
		for (unsigned k = 0; k <= kmax; ++k) {
			std::string expect(k + 1, '\0');
			expect[0] = 1;
			std::string got;
			power.digits(got, base);
			CHECK(got == expect, "digits of %d^%u", base, k);
			if (k) {
				std::string nines(k, static_cast<char>(base - 1));
				got.clear();
				(power - uinteger_t(1)).digits(got, base);
				CHECK(got == nines, "digits of %d^%u - 1", base, k);
			}
			power *= uinteger_t(base);
		}
		std::string zero;
		uinteger_t(0).digits(zero, base);
		CHECK(zero == std::string(1, '\0'), "digits of zero in base %d", base);
		const unsigned char none = 0;
		CHECK(uinteger_t::from_digits(&none, 0, base) == uinteger_t(0), "from_digits of nothing, base %d", base);
	}

	// Digits that straddle a limb boundary (3 and 5 bits do not divide 32 or 64).
	std::string d;
	(uinteger_t(1) << 63).digits(d, 8);
	CHECK(d == std::string("\x01") + std::string(21, '\0'), "2^63 in base 8");
	d.clear();
	((uinteger_t(1) << 64) - uinteger_t(1)).digits(d, 8);
	CHECK(d == std::string("\x01") + std::string(21, '\x07'), "2^64-1 in base 8");
	d.clear();
	((uinteger_t(1) << 64) - uinteger_t(1)).digits(d, 32);
	CHECK(d == std::string("\x0f") + std::string(12, '\x1f'), "2^64-1 in base 32");
	d.clear();
	uinteger_t(1000).digits(d, 256);
	CHECK(d == std::string("\x03\xe8", 2), "1000 in base 256");

	// Invalid input.
	for (int base = 2; base < 256; ++base) {
		unsigned char bad[3] = {1, static_cast<unsigned char>(base), 1};
		CHECK(what_of([&] { uinteger_t::from_digits(bad, 3, base); }).rfind("Error", 0) == 0, "digit == base %d throws", base);
		bad[1] = 1;
		bad[2] = 255;
		CHECK(what_of([&] { uinteger_t::from_digits(bad, 3, base); }).rfind("Error", 0) == 0, "digit 255 in base %d throws", base);
	}
	unsigned char nine[3] = {1, 0, 9};
	CHECK(what_of([&] { uinteger_t::from_digits(nine, 3, 8); }) == "Error: Not a digit in base 8: 9 at 1", "from_digits message");
	for (int base : {0, 1, 257, -2}) {
		CHECK(what_of([&] { uinteger_t::from_digits(nine, 3, base); }).rfind("Error", 0) == 0, "from_digits base %d", base);
		std::string out;
		CHECK(what_of([&] { uinteger_t(5).digits(out, base); }) == "Base must be in the range [2, 256]", "digits base %d", base);
	}
	CHECK(what_of([&] { uinteger_t(5).str(37); }) == "Base must be in the range [2, 36]", "str base 37");

	// Parsing a digit that is not below the base, and characters that are not digits.
	CHECK(what_of([] { uinteger_t("19", 8); }) == "Error: Not a digit in base 8: '9' at 1", "parse 9 in base 8");
	CHECK(what_of([] { uinteger_t("12z4", 10); }) == "Error: Not a digit in base 10: 'z' at 2", "parse z in base 10");
	CHECK(what_of([] { uinteger_t("2", 2); }) == "Error: Not a digit in base 2: '2' at 1", "parse 2 in base 2");
	CHECK(what_of([] { uinteger_t("1g", 16); }) == "Error: Not a digit in base 16: 'g' at 1", "parse g in base 16");
	CHECK(what_of([] { uinteger_t("1-9", 16); }) == "Error: Not a digit in base 16: '-' at 2", "parse - in base 16");
	CHECK(uinteger_t("FF", 16) == uinteger_t(255) && uinteger_t("Zz", 36) == uinteger_t(1295), "parse upper case");

	// Bytes >= 0x80 are not digits. (An unsigned char index keeps ord() inside its table;
	// a plain char sign-extends and reads before it.)
	const std::string high_prefix = "Error: Not a digit in base ";
	for (int base : {2, 3, 8, 10, 16, 32, 36}) {
		for (unsigned char byte : {0x80, 0xe9, 0xff}) {
			std::string text = "1";
			text.push_back(static_cast<char>(byte));
			text.push_back('1');
			auto msg = what_of([&] { uinteger_t(text, base); });
			CHECK(msg == high_prefix + std::to_string(base) + ": '" + std::string(1, static_cast<char>(byte)) + "' at 2", "parse byte %u in base %d", byte, base);
			std::vector<char> vec(text.begin(), text.end());
			CHECK(what_of([&] { uinteger_t(vec, base); }) == msg, "parse byte %u from a vector in base %d", byte, base);
			CHECK(what_of([&] { uinteger_t(text.data(), text.size(), base); }) == msg, "parse byte %u from a pointer in base %d", byte, base);
		}
	}
	CHECK(what_of([] { uinteger_t("\xff", 16); }) == "Error: Not a digit in base 16: '\xff' at 1", "parse 0xff in base 16");
	CHECK(what_of([] { uinteger_t("1\x80", 10); }) == "Error: Not a digit in base 10: '\x80' at 1", "parse 0x80 in base 10");
}

// The size arithmetic of the bit-sliced conversions must throw exactly when the exact
// result does not fit the size type, so a 32-bit size_t cannot wrap into a short
// buffer. Checked at 16 and 32 bits against 64-bit arithmetic, and at the sizes a
// 512 MiB base-256 input reaches.
template <typename F>
static bool throws_length(F f) {
	try { f(); } catch (const std::length_error&) { return true; }
	return false;
}

template <typename Size>
static void check_limbs_for_digits(unsigned long long n, unsigned bits) {
	const unsigned long long digit_bits = 8 * sizeof(uinteger_t::digit);
	const unsigned long long need = n * bits + digit_bits - 1;
	Size got = 0;
	const bool threw = throws_length([&] { got = uinteger_t::_limbs_for_digits<Size>(static_cast<Size>(n), bits); });
	CHECK(threw == (need > std::numeric_limits<Size>::max()), "limbs for %llu digits of %u bits (%zu-bit size): %s", n, bits, 8 * sizeof(Size), threw ? "threw" : "did not throw");
	CHECK(threw || got == need / digit_bits, "limbs for %llu digits of %u bits (%zu-bit size): got %llu", n, bits, 8 * sizeof(Size), static_cast<unsigned long long>(got));
}

template <typename Size>
static void check_digits_for_limbs(unsigned long long n, unsigned top, unsigned bits) {
	const unsigned long long digit_bits = 8 * sizeof(uinteger_t::digit);
	const unsigned long long need = (n - 1) * digit_bits + top + bits - 1;
	Size got = 0;
	const bool threw = throws_length([&] { got = uinteger_t::_digits_for_limbs<Size>(static_cast<Size>(n), top, bits); });
	CHECK(threw == (need > std::numeric_limits<Size>::max()), "digits for %llu limbs, top %u, %u bits (%zu-bit size): %s", n, top, bits, 8 * sizeof(Size), threw ? "threw" : "did not throw");
	CHECK(threw || got == need / bits, "digits for %llu limbs, top %u, %u bits (%zu-bit size): got %llu", n, top, bits, 8 * sizeof(Size), static_cast<unsigned long long>(got));
}

template <typename Size>
static void check_size_window(unsigned long long around, unsigned long long span) {
	const unsigned long long digit_bits = 8 * sizeof(uinteger_t::digit);
	for (unsigned bits = 1; bits <= 8; ++bits) {
		const unsigned long long max = std::numeric_limits<Size>::max();
		// n where n * bits + digit_bits - 1 crosses the maximum, and where the digit count does.
		for (unsigned long long centre : {around, (max - (digit_bits - 1)) / bits, (max - digit_bits - bits) / digit_bits + 1}) {
			for (unsigned long long n = centre > span ? centre - span : 0; n <= centre + span && n <= max; ++n) {
				check_limbs_for_digits<Size>(n, bits);
				if (n) {
					for (unsigned top : {1u, static_cast<unsigned>(digit_bits / 2), static_cast<unsigned>(digit_bits)}) check_digits_for_limbs<Size>(n, top, bits);
				}
			}
		}
	}
}

static void check_size_guards() {
	// Everything small, then the neighbourhood of each limit.
	for (unsigned long long n = 0; n <= 300; ++n) {
		for (unsigned bits = 1; bits <= 8; ++bits) {
			check_limbs_for_digits<std::uint16_t>(n, bits);
			check_limbs_for_digits<std::uint32_t>(n, bits);
		}
	}
	check_size_window<std::uint16_t>(65535, 70);
	check_size_window<std::uint32_t>(std::numeric_limits<std::uint32_t>::max(), 70);
	// 2^29 + 5 base-256 digits wrap a 32-bit bit count, so they throw; just under the limit they do not.
	CHECK(throws_length([] { uinteger_t::_limbs_for_digits<std::uint32_t>((1u << 29) + 5, 8); }), "2^29 + 5 base-256 digits fit 32 bits");
	CHECK(!throws_length([] { uinteger_t::_limbs_for_digits<std::uint32_t>((1u << 29) - 8, 8); }), "2^29 - 8 base-256 digits throw");
	// At the real size type, ordinary sizes pass through.
	CHECK(uinteger_t::_limbs_for_digits<std::size_t>(1000, 8) == (8000 + 8 * sizeof(uinteger_t::digit) - 1) / (8 * sizeof(uinteger_t::digit)), "limbs at size_t");
}

static void check_digits_random(std::size_t big) {
	int base = rng() % 3 ? 2 + static_cast<int>(rng() % 255) : (1 << (1 + rng() % 8));
	// Mostly short values, some long enough to cross many limbs and chunks.
	auto v = random_value(rng() % 8 ? 1 + rng() % 40 : 1 + rng() % std::min<std::size_t>(big, 300));
	bool with_ref = v.bits() <= 4000;
	if (rng() & 1) {
		check_digits<std::string>(v, base, with_ref);
	} else {
		check_digits<std::vector<unsigned char>>(v, base, with_ref);
	}
	if (base <= 36) {
		CHECK(uinteger_t(v.str(base), base) == v, "str/parse base %d", base);
	}
}

// Recursive-division shapes: divisor and quotient lengths (in limbs) around the threshold, with
// the quotient shorter than, equal to and many times longer than the divisor, over several
// recursion depths. Divisors and dividends that push the quotient estimate to its extremes
// (clamped quotient, two corrections) ride along.
static void check_dc_shapes(int rounds) {
	const std::size_t lw = sizeof(uinteger_t::digit) / 4;
	const std::size_t bits = sizeof(uinteger_t::digit) * 8;
	const std::size_t t = DIV_DC_THRESHOLD;
	const std::size_t ts = DIV_DC_STEP_THRESHOLD;
	std::vector<std::size_t> dns = {t - 1, t, t + 1, 2 * t - 1, 2 * t, 2 * t + 1, 3 * t, 4 * t + 1, 6 * t - 1, 8 * t + 3};
	if (t <= 32) {
		dns.push_back(16 * t + 7);
		dns.push_back(33 * t + 1);
	}
	const uinteger_t one(1);
	for (auto dn : dns) {
		const std::size_t ms[] = {1, ts - 1, ts, ts + 1, t - 1, t, t + 1, dn / 2, dn - 1, dn, dn + 1, 2 * dn, 3 * dn + t / 2, 4 * dn + 1};
		for (auto m : ms) {
			if (!m) continue;
			for (int k = 0; k < rounds; ++k) {
				words d = random_words(dn * lw);
				if (k & 1) d.back() |= 0x80000000u;
				check_div(to_uint(random_words((dn + m) * lw)), to_uint(d), "dc shape");
			}

			// Divisors 100..0ff..f and ff..f, dividends just below d * B^m (quotient all ones).
			words e(dn * lw, 0xffffffffu), f(e);
			e.back() = 0x80000000u;
			for (auto* w : {&e, &f}) {
				auto ud = to_uint(*w);
				check_div((ud << (m * bits)) - one, ud, "dc max quotient");
				check_div((ud << (m * bits)) - uinteger_t(3), ud, "dc max quotient - 2");
			}

			// Top limbs of the dividend equal to the top c limbs of the divisor times B^c - 1:
			// the quotient estimate is two too large.
			auto ue = to_uint(e);
			for (std::size_t c : {m < dn ? m : (dn + 1) / 2, (dn + 1) / 2, dn / 2}) {
				if (!c || c >= dn) continue;
				auto hi = ue >> ((dn - c) * bits);
				check_div((((one << (c * bits)) - one) * hi) << ((dn - c + m - c) * bits), ue, "dc double correction");
				check_div(((((one << (c * bits)) - one) * hi) << ((dn - c + m - c) * bits)) + ue - one, ue, "dc double correction + max rem");
			}
		}
	}
}

// 2^k from bytes, so the shift checks do not rest on the shift operators.
static uinteger_t pow2(std::size_t k) {
	std::string bytes(k / 8 + 1, '\0');
	bytes[0] = static_cast<char>(1u << (k % 8));
	return uinteger_t(bytes, 256);
}

// Shifts by a native count, a one-limb uinteger_t count and in place, against multiplication and
// division by 2^k.
static void check_shifts(const uinteger_t& a, std::size_t k) {
	const auto p = pow2(k);
	const auto left = a * p;
	const auto right = a / p;
	const uinteger_t count(k);
	const unsigned long long kk = k;
	CHECK((a << k) == left, "a << %zu", k);
	CHECK((a << kk) == left, "a << %zu (unsigned long long)", k);
	CHECK((a << count) == left, "a << uinteger_t(%zu)", k);
	CHECK((a >> k) == right, "a >> %zu", k);
	CHECK((a >> kk) == right, "a >> %zu (unsigned long long)", k);
	CHECK((a >> count) == right, "a >> uinteger_t(%zu)", k);
	if (k < 100) {
		const int ki = static_cast<int>(k);
		CHECK((a << ki) == left && (a >> ki) == right, "a << int %d", ki);
	}
	auto t = a;
	t <<= k;
	CHECK(t == left, "a <<= %zu", k);
	t >>= k;
	CHECK(t == a, "a <<= k then >>= %zu", k);
	t = a;
	t <<= count;
	CHECK(t == left, "a <<= uinteger_t(%zu)", k);
	t >>= count;
	CHECK(t == a, "a <<= k then >>= uinteger_t(%zu)", k);
	t = a;
	t >>= k;
	CHECK(t == right, "a >>= %zu", k);
	t = a;
	t >>= count;
	CHECK(t == right, "a >>= uinteger_t(%zu)", k);
	CHECK((right << k) + a % p == a, "(a >> k << k) + a %% 2^k == a for k %zu", k);

	// The static forms with the result aliasing the operand (a right shift once ran the left one).
	t = a;
	uinteger_t::bitwise_rshift(t, t, count);
	CHECK(t == right, "bitwise_rshift in place %zu", k);
	t = a;
	uinteger_t::bitwise_lshift(t, t, count);
	CHECK(t == left, "bitwise_lshift in place %zu", k);
}

// Operands and results that are the same object, and a subtraction that wraps.
static void check_alias(const uinteger_t& a, const uinteger_t& b) {
	const auto sum = a + b;
	const auto diff = a - b;
	uinteger_t r = a;
	uinteger_t::add(r, r, b);
	CHECK(r == sum, "add(r, r, b)");
	r = b;
	uinteger_t::add(r, a, r);
	CHECK(r == sum, "add(r, a, r)");
	r = a;
	uinteger_t::add(r, r, r);
	CHECK(r == a + a, "add(r, r, r)");
	r = a;
	r += r;
	CHECK(r == a + a, "r += r");
	r = a;
	uinteger_t::sub(r, r, b);
	CHECK(r == diff, "sub(r, r, b)");
	r = b;
	uinteger_t::sub(r, a, r);
	CHECK(r == diff, "sub(r, a, r)");
	r = a;
	r -= r;
	CHECK(!r, "r -= r");
	r = a;
	r *= r;
	CHECK(r == a * a, "r *= r");

	// a - b wraps modulo B^max(size): adding b back gives a, or a + B^n when it wrapped.
	const auto n = std::max(a.size(), b.size()) * uinteger_t::digit_bits;
	CHECK(diff.size() <= std::max(a.size(), b.size()), "a - b wider than its operands");
	CHECK(diff + b == (a >= b ? a : a + pow2(n)), "(a - b) + b");

	if (b) {
		const auto qr = a.divmod(b);
		uinteger_t q = a, rem = b;
		uinteger_t::divmod(q, rem, a, b);
		CHECK(q == qr.first && rem == qr.second, "divmod(q = a, r = b, a, b)");
		q = b;
		rem = a;
		uinteger_t::divmod(q, rem, a, b);
		CHECK(q == qr.first && rem == qr.second, "divmod(q = b, r = a, a, b)");
		r = a;
		r /= b;
		CHECK(r == qr.first, "r /= b");
		r = a;
		r %= b;
		CHECK(r == qr.second, "r %%= b");
		r = b;
		r /= r;
		CHECK(r == uinteger_t(1), "r /= r");
		r = b;
		r %= r;
		CHECK(!r, "r %%= r");
	}
}

// The representation is the limb vector alone; moving and copying leave no stale state, and an
// in-place right shift does not expose the limbs it dropped (x *= 1 once did).
static void check_repr() {
	static_assert(sizeof(uinteger_t) == sizeof(uinteger_t::container), "a uinteger_t is just its limb vector");
	const std::size_t bits = uinteger_t::digit_bits;

	for (std::size_t n = 0; n <= 9; ++n) {
		for (int rep = 0; rep < 4; ++rep) {
			const auto a = random_value(n * bits / 32);
			const auto b = random_value(rng() % 8 ? n * bits / 32 + rng() % 5 : 0);
			for (std::size_t k : {std::size_t(0), std::size_t(1), bits - 1, bits, bits + 1, 2 * bits - 1, 2 * bits, 2 * bits + 5, 3 * bits, n * bits - 1, n * bits, n * bits + 1, n * bits + 70, std::size_t(5 * bits + 17)}) {
				if (k <= 12 * bits) check_shifts(a, k);
			}
			check_alias(a, b);
			check_alias(b, a);
		}
	}

	// Counts wider than a limb: a right shift gives zero, a left shift of zero stays zero, and
	// a left shift of anything else cannot be represented.
	const auto wide = pow2(2 * bits);
	CHECK(!(random_value(4) >> wide), "right shift by a two-limb count");
	CHECK(!(uinteger_t() << wide), "zero shifted left by a two-limb count");
	bool threw = false;
	try {
		auto x = uinteger_t(1) << wide;
		CHECK(false, "1 << 2^%zu returned %zu limbs", 2 * bits, x.size());
	} catch (const std::length_error&) {
		threw = true;
	}
	CHECK(threw, "left shift by a two-limb count throws std::length_error");
	CHECK(!(random_value(4) >> -1), "right shift by -1 gives zero");

	// Carry and borrow chains across limb boundaries.
	for (std::size_t n = 1; n <= 10; ++n) {
		const auto p = pow2(n * bits);
		const auto m = p - uinteger_t(1);
		CHECK(m.size() == n && m + uinteger_t(1) == p, "B^%zu - 1 + 1", n);
		CHECK(m + m == pow2(n * bits + 1) - uinteger_t(2), "(B^%zu - 1) * 2", n);
		CHECK(p - p == uinteger_t(), "B^%zu - B^%zu", n, n);
		CHECK(p - m == uinteger_t(1), "B^%zu - (B^%zu - 1)", n, n);
		auto x = m;
		x += uinteger_t(1);
		CHECK(x == p, "(B^%zu - 1) += 1", n);
		x -= uinteger_t(1);
		CHECK(x == m, "B^%zu -= 1", n);
		auto y = uinteger_t(1);
		y -= p;
		CHECK(y == pow2((n + 1) * bits) - p + uinteger_t(1), "1 - B^%zu wraps within %zu limbs", n, n + 1);
		--x;
		++x;
		CHECK(x == m, "-- then ++");
	}

	// x >>= 128 then x *= 1 gave back the five limbs the shift had dropped.
	auto mk = [&] { return uinteger_t(std::string(5 * bits / 8, '\x7f'), 256); };
	const auto want = mk() >> (2 * bits);
	CHECK(want.size() == 3, "five limbs shifted right by two");
	auto x = mk();
	x >>= 2 * bits;
	x *= uinteger_t(1);
	CHECK(x == want, "x >>= 2 limbs; x *= 1");
	x = mk();
	x >>= 2 * bits;
	x /= uinteger_t(1);
	CHECK(x == want, "x >>= 2 limbs; x /= 1");
	x = mk();
	x >>= 2 * bits;
	x %= uinteger_t(1);
	CHECK(!x, "x >>= 2 limbs; x %%= 1");
	x = mk();
	x >>= 2 * bits;
	const auto& same = x;
	x = same;
	CHECK(x == want, "x >>= 2 limbs; x = x");
	x = mk();
	x >>= 2 * bits;
	uinteger_t y = std::move(x);
	CHECK(y == want, "x >>= 2 limbs; y = std::move(x)");
	x = want;
	CHECK(x == want, "a moved-from value takes assignment");

	// Self copy-assignment, and so `x *= 1`, after an in-place right shift (hunt-1 R1).
	{
		uinteger_t v("100000000000000000000000000000005", 16); // 2^128 + 5
		v >>= 64;
		const auto two64 = pow2(64);
		CHECK(v == two64, "2^128 + 5 >>= 64 gives 2^64");
		v *= 1;
		CHECK(v == two64, "v >>= 64; v *= 1");
		uinteger_t z("100000000000000000000000000000005", 16);
		z >>= 64;
		const auto& zr = z;
		z = zr;
		CHECK(z == two64, "z >>= 64; z = z");
		uinteger_t w("100000000000000000000000000000005", 16);
		w >>= 64;
		w *= w;
		CHECK(w == pow2(128), "w >>= 64; w *= w");
	}

	// A moved-from value is zero, also after an in-place right shift (hunt-1 R2: it kept its
	// window offset, so size() read as 2^64 - 1).
	{
		uinteger_t v("100000000000000000000000000000005", 16);
		v >>= 64;
		uinteger_t moved(std::move(v));
		CHECK(moved == pow2(64), "move construction after >>=");
		CHECK(v.size() == 0 && !v, "moved-from after >>= has size %zu", v.size());
		if (!v.size()) {
			CHECK(v + uinteger_t(7) == uinteger_t(7), "moved-from value used as zero");
		}
		uinteger_t u("100000000000000000000000000000005", 16);
		u >>= 64;
		uinteger_t target;
		target = std::move(u);
		CHECK(target == pow2(64), "move assignment after >>=");
		CHECK(u.size() == 0 && !u, "move-assigned-from after >>= has size %zu", u.size());
		u = uinteger_t(9);
		CHECK(u == uinteger_t(9), "a moved-from value takes assignment after >>=");
	}
}

int main(int argc, char** argv) {
	int iters = argc > 1 ? std::atoi(argv[1]) : 300;
	std::size_t big = argc > 2 ? static_cast<std::size_t>(std::atoi(argv[2])) : 1200;

	for (int it = 0; it < iters; ++it) {
		auto a = random_words(pick_words(big));
		auto b = random_words(pick_words(big));
		check_mul(a, b);
		check_sqr(a);

		auto ua = to_uint(a), ub = to_uint(b);
		check_div(ua, ub, "random");
		check_div(ua * ub + ub - uinteger_t(1), ub, "max remainder");
		if (ub > uinteger_t(1)) check_div(ua * ub, ub, "exact");

		unsigned s = static_cast<unsigned>(rng() % 200);
		CHECK(((ua << s) >> s) == ua, "shift round trip %u", s);
		CHECK((ua << s) == ua * (uinteger_t(1) << s), "shift as multiply %u", s);
		CHECK((ua + ub) - ub == ua, "add/sub round trip");

		int base = 2 + static_cast<int>(rng() % 35);
		CHECK(uinteger_t(ua.str(base), base) == ua, "str/parse base %d", base);
	}

	for (int it = 0; it < 4 * iters; ++it) {
		check_digits_random(big);
	}
	check_digit_values();
	check_digit_errors();
	check_radix_kernel();
	check_size_guards();
	check_repr();
	check_kernel_sizes();

	const std::size_t karatsuba = U::mul_karatsuba_threshold;

	// Lopsided products of three or more chunks whose last chunk takes the odd split
	// (bn = 2*cn - 1) into scratch an earlier chunk dirtied.
	for (std::size_t cn = karatsuba; cn < karatsuba + 6; ++cn) {
		std::size_t bn = 2 * cn - 1;
		for (std::size_t an : {2 * bn + cn, 3 * bn + cn, 2 * bn + cn + 1}) {
			check_mul(random_words(an * limb_words), random_words(bn * limb_words));
		}
	}

	// Divisors whose top limbs make the quotient-digit estimate overshoot or saturate.
	for (std::size_t n = 2; n <= 40; n += 3) {
		for (int k = 0; k < 6; ++k) {
			words d(n * limb_words, 0xffffffffu), x((n + 3 + k) * limb_words, 0xffffffffu);
			if (k & 1) d[0] = 1;
			if (k & 2) { std::fill(d.begin(), d.end() - 1, 0u); d.back() = 0x80000000u; }
			if (k & 4) x[x.size() / 2] = 0;
			check_div(to_uint(x), to_uint(d), "edge divisor");
			auto ud = to_uint(d);
			check_div(ud * ud - uinteger_t(1), ud, "square minus one");
		}
	}

	check_dc_shapes(4);

	if (failures) {
		std::printf("property: %d failures\n", failures);
		return 1;
	}
	std::printf("property OK (%d iterations, up to %zu words)\n", iters, big);
	return 0;
}

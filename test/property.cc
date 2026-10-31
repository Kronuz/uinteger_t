// Dependency-free property tests for the arithmetic kernels, checked against a
// reference schoolbook multiply on 32-bit words and against algebraic identities.
// Runs under CTest, so CI's sanitizer jobs cover every multiplication and division path.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <vector>
// The size helpers are private; the size-guard checks below reach them directly.
#define UINT_T_PUBLIC_IMPLEMENTATION
#include "uinteger_t.hh"

static std::mt19937_64 rng(20261030);
static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); if (++failures > 10) std::exit(1); } } while (0)

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
	std::vector<std::size_t> dns = {t - 1, t, t + 1, 2 * t - 1, 2 * t, 2 * t + 1, 3 * t, 4 * t + 1, 6 * t - 1, 8 * t + 3};
	if (t <= 16) {
		dns.push_back(16 * t + 7);
		dns.push_back(33 * t + 1);
	}
	const uinteger_t one(1);
	for (auto dn : dns) {
		const std::size_t ms[] = {1, t - 1, t, t + 1, dn / 2, dn - 1, dn, dn + 1, 2 * dn, 3 * dn + t / 2, 4 * dn + 1};
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

int main(int argc, char** argv) {
	int iters = argc > 1 ? std::atoi(argv[1]) : 300;
	std::size_t big = argc > 2 ? static_cast<std::size_t>(std::atoi(argv[2])) : 1200;

	for (int it = 0; it < iters; ++it) {
		auto a = random_words(pick_words(big));
		auto b = random_words(pick_words(big));
		check_mul(a, b);

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
	check_size_guards();
	const std::size_t limb_words = sizeof(uinteger_t::digit) / 4;
	const std::size_t karatsuba = 2048 / (8 * sizeof(uinteger_t::digit));

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

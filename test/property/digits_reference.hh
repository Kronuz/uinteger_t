// Division identities, digits() against repeated division, chunk checks and the radix tables. Included by property.cc.

#pragma once

#include "common.hh"

static void check_div(const uinteger_t& a, const uinteger_t& b, const char* what) {
	auto qr = a.divmod(b);
	CHECK(qr.second < b, "%s: remainder not below divisor", what);
	CHECK(qr.first * b + qr.second == a, "%s: q*b + r != a", what);
	CHECK(a / b == qr.first && a % b == qr.second, "%s: / and %% disagree with divmod", what);
}

// A value from random 32-bit words, built with shifts so it is the same at either limb width
// (each word goes in as two 16-bit halves, which fit the narrowest limb).
static uinteger_t random_value(std::size_t nwords) {
	auto w = random_words(nwords);
	uinteger_t v;
	for (auto i = w.size(); i--;) {
		v <<= 16;
		v |= uinteger_t(w[i] >> 16);
		v <<= 16;
		v |= uinteger_t(w[i] & 0xffffu);
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

// Whether a section that visits every base visits this one: all of them, or in light mode the
// bases below 12, every eleventh, the powers of two and the top five (they span every digit
// count per limb and both table shapes: with and without a guard limb).
static bool sampled(int base) {
	return !light || base < 12 || base % 11 == 0 || is_power_of_two(base) || base > 250;
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

// The table's R against K single-limb divisions of B^(L + n) by D, floor(floor(a / D) / D) being
// floor(a / D^2): every table by bit count and by limb count up to the kernels' limit (a stride in
// light mode), in bases of different chunk shapes.
static void check_radix_tables() {
	for (int base : {3, 10, 58, 139, 255}) {
		if (light && (base == 3 || base == 139)) continue;
		const auto& rx = uinteger_t::_radix_for(base);
		const uinteger_t::small_divisor dv(rx.D);
		const std::size_t by_bits = uinteger_t::radix_unrolled * digit_bits;
		const std::size_t step = light ? 7 : 1;
		std::vector<std::pair<std::size_t, std::size_t>> shapes;  // (n, bits) as _table_for asks
		for (std::size_t bits = 1; bits <= by_bits; bits += step) shapes.emplace_back((bits + digit_bits - 1) / digit_bits, bits);
		for (std::size_t n = uinteger_t::radix_unrolled + 1; n <= uinteger_t::radix_max_limbs; n += step) shapes.emplace_back(n, n * digit_bits);
		shapes.emplace_back(uinteger_t::radix_unrolled, by_bits);  // the largest of each kind, whatever the stride
		shapes.emplace_back(uinteger_t::radix_max_limbs, uinteger_t::radix_max_limbs * digit_bits);
		for (const auto& s : shapes) {
			std::unique_ptr<uinteger_t::radix_table> t(uinteger_t::_make_radix_table(rx, s.first, s.second));
			uinteger_t a;
			a.resize(t->fraction + s.first + 1, 0);
			a.data()[t->fraction + s.first] = 1;
			for (unsigned i = 0; i < t->chunks; ++i) a.divmod_small(dv);
			bool same = a.size() <= t->rlen;
			for (std::size_t l = 0; same && l < t->rlen; ++l) same = t->R[l] == (l < a.size() ? a.data()[l] : 0);
			CHECK(same, "table R of base %d, %zu limbs, %zu bits", base, s.first, s.second);
		}
	}
}

// The per-core defaults, restated: every cutoff gives the same digits, so a wrong default in the
// header's platform branches changes no output and only this sees it.
static void check_conversion_defaults() {
#ifdef PROPERTY_DEFAULT_CUTOFFS
#if defined __APPLE__ && (defined __aarch64__ || defined __arm64__)
	const std::size_t radix_bits = 14336;
#else
	const std::size_t radix_bits = 8192;
#endif
#if defined __x86_64__ || defined _M_X64
	const std::size_t parse_bits = digit_bits == 64 ? 12288 : 8192;
#else
	const std::size_t parse_bits = 8192;
#endif
	CHECK(uinteger_t::radix_max_bits == radix_bits, "radix_max_bits %zu, want %zu", static_cast<std::size_t>(uinteger_t::radix_max_bits), radix_bits);
	CHECK(uinteger_t::digits_tree_threshold == radix_bits / digit_bits, "digits_tree_threshold %zu limbs, want %zu bits",
	      static_cast<std::size_t>(uinteger_t::digits_tree_threshold), radix_bits);
	CHECK(uinteger_t::from_digits_tree_threshold == parse_bits / digit_bits, "from_digits_tree_threshold %zu limbs, want %zu bits",
	      static_cast<std::size_t>(uinteger_t::from_digits_tree_threshold), parse_bits);
#endif
}

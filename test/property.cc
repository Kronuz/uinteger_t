// Dependency-free property tests for the arithmetic kernels, checked against a
// reference schoolbook multiply on 32-bit words and against algebraic identities.
// Runs under CTest, so CI's sanitizer jobs cover every multiplication and division path.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
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

	// Divisors whose top limbs make the quotient-digit estimate overshoot or saturate.
	const std::size_t limb_words = sizeof(uinteger_t::digit) / 4;
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

	if (failures) {
		std::printf("property: %d failures\n", failures);
		return 1;
	}
	std::printf("property OK (%d iterations, up to %zu words)\n", iters, big);
	return 0;
}

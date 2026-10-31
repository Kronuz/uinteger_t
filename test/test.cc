// Smoke test for the standalone uinteger_t library.
// Build: c++ -std=c++17 -I.. test.cc -o test && ./test
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
// The single-limb primitives are private; this test checks them directly.
#define UINT_T_PUBLIC_IMPLEMENTATION
#include "uinteger_t.hh"

int main() {
	// Construct from an integer and from a base-16 string (no "0x" prefix).
	uinteger_t a = 3735879680u;       // 0xDEAD0000
	uinteger_t b("beee", 16);         // 0x0000BEEE
	uinteger_t num = a + b + 1;       // 0xDEADBEEF

	// Convert to string in a couple of bases.
	assert(num.str(10) == "3735928559");
	assert(num.str(16) == "deadbeef");
	assert(num.str(2) == "11011110101011011011111011101111");

	// Base-36 round-trip.
	uinteger_t z("zz", 36);
	assert(z.str(10) == "1295");
	assert(z.str(36) == "zz");

	// Multiplication beyond 64 bits: 2^64 * 2^64 == 2^128.
	uinteger_t two_64("18446744073709551616", 10);
	uinteger_t two_128 = two_64 * two_64;
	assert(two_128.str(10) == "340282366920938463463374607431768211456");

	// Shifting: 1 << 128 equals 2^128.
	uinteger_t shifted = uinteger_t(1) << uinteger_t(128);
	assert(shifted == two_128);

	// Comparisons across magnitudes.
	assert(a < num);
	assert(num > a);
	assert(two_128 >= shifted);
	assert(two_64 != two_128);

	// Convert a small value back to a native integer.
	uinteger_t small = 42;
	assert(static_cast<unsigned long>(small) == 42ul);
	assert(static_cast<bool>(small));
	assert(!static_cast<bool>(uinteger_t(0)));

	// In-place single-limb primitives match the general operators, including
	// divisors that need normalization and chunk boundaries in str/parse.
	for (unsigned long long d : {1ull, 3ull, 10ull, 58ull * 58 * 58, 1ull << 63, ~0ull}) {
		uinteger_t x = two_128 * two_128 + uinteger_t(12345);
		uinteger_t q = x;
		auto r = q.divmod_small(d);
		assert(q == x / d && uinteger_t(r) == x % d);
		uinteger_t y = q;
		y.mul_add_small(d, r);
		assert(y == x);
	}
	for (int base : {3, 10, 36}) {
		uinteger_t big = (two_128 * two_128) + uinteger_t(7);
		assert(uinteger_t(big.str(base), base) == big);
		assert(uinteger_t(std::string(25, '0') + big.str(base), base) == big);
	}
	assert(uinteger_t(1).str(10) == "1" && uinteger_t(0).str(10) == "0");
	assert(uinteger_t("10000000000000000000", 10).str(10) == "10000000000000000000");

	// Digit-level conversion: digits() appends byte values, from_digits() reverses it.
	{
		uinteger_t v("deadbeefcafe", 16);
		std::string d;
		v.digits(d, 16);
		assert(d == std::string("\x0d\x0e\x0a\x0d\x0b\x0e\x0e\x0f\x0c\x0a\x0f\x0e", 12));
		assert(uinteger_t::from_digits(d.data(), d.size(), 16) == v);
		std::vector<unsigned char> b;
		uinteger_t(1000).digits(b, 256);
		assert((b == std::vector<unsigned char>{3, 232}));
		assert(uinteger_t::from_digits(b.data(), b.size(), 256) == uinteger_t(1000));
		std::string z;
		uinteger_t(0).digits(z, 58);
		assert(z == std::string(1, '\0'));
	}

	std::printf("uinteger_t OK: %s + %s + 1 = %s (hex %s), 2^128 = %s\n",
	            a.str(10).c_str(), b.str(10).c_str(), num.str(10).c_str(),
	            num.str(16).c_str(), two_128.str(10).c_str());
	return 0;
}

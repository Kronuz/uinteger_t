// Smoke test for the standalone uinteger_t library.
// Build: c++ -std=c++17 -I.. test.cc -o test && ./test
#include <cassert>
#include <cstdio>
#include <string>
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

	std::printf("uinteger_t OK: %s + %s + 1 = %s (hex %s), 2^128 = %s\n",
	            a.str(10).c_str(), b.str(10).c_str(), num.str(10).c_str(),
	            num.str(16).c_str(), two_128.str(10).c_str());
	return 0;
}

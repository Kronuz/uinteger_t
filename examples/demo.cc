// A runnable tour of uinteger_t.
//
// Build (when this repo is the top-level project):
//   cmake -B build && cmake --build build && ./build/uinteger_t_demo
//
// uinteger_t is an arbitrary-precision unsigned integer that reads like ordinary
// integer code: you build values from native ints or strings, do the usual
// arithmetic, and render the result in any base. This demo computes values that
// overflow 64 bits (2^128, a factorial, a big power), shows the same value
// rendered in several bases and via std::hex, compares values across magnitudes,
// and uses divmod() to get quotient and remainder in one shot.
#include <cstdio>
#include <iostream>
#include <string>

#include "uinteger_t.hh"

static void rule(const char* title) {
	std::printf("\n\033[1m── %s ──\033[0m\n", title);
}

int main() {
	std::puts("uinteger_t demo  (unsigned integers as big as memory allows)");

	// --- 1. arithmetic that overflows 64 bits --------------------------------
	rule("values larger than a 64-bit register");
	uinteger_t u64_max("18446744073709551615", 10);  // 2^64 - 1, the widest uint64
	std::printf("  uint64 max         : %s\n", u64_max.str().c_str());
	uinteger_t one_past = u64_max + 1;                // would wrap a real uint64 to 0
	std::printf("  uint64 max + 1     : %s   (a real uint64 would wrap to 0)\n",
		one_past.str().c_str());

	uinteger_t two_64("18446744073709551616", 10);    // 2^64
	uinteger_t two_128 = two_64 * two_64;             // 2^128
	std::printf("  2^64 * 2^64 = 2^128: %s\n", two_128.str().c_str());
	std::printf("  bits()             : %zu significant bits\n", two_128.bits());

	// 1 << 128 lands on the same value, computed a different way.
	uinteger_t shifted = uinteger_t(1) << uinteger_t(128);
	std::printf("  1 << 128 == 2^128  : %s\n", shifted == two_128 ? "true" : "false");

	// --- 2. a 50-digit factorial, built with a plain loop --------------------
	rule("40! computed with ordinary-looking integer code");
	uinteger_t fact = 1;
	for (unsigned i = 2; i <= 40; ++i) {
		fact *= i;   // *= grows the value as needed, no overflow to worry about
	}
	std::printf("  40!                : %s\n", fact.str().c_str());
	std::printf("  decimal digits     : %zu\n", fact.str().size());

	// --- 3. the same value rendered in several bases -------------------------
	rule("one value, rendered in several bases");
	uinteger_t deadbeef = uinteger_t(3735879680u) + uinteger_t("beee", 16) + 1;  // 0xDEADBEEF
	std::printf("  built from 0xDEAD0000 + 0xBEEE + 1\n");
	std::printf("  base 10            : %s\n", deadbeef.str(10).c_str());
	std::printf("  base 16            : %s\n", deadbeef.str(16).c_str());
	std::printf("  base 2             : %s\n", deadbeef.str(2).c_str());
	std::printf("  base 36            : %s\n", deadbeef.str(36).c_str());
	// operator<< honors the stream's oct/dec/hex flag, just like a native int.
	std::cout << "  via std::hex       : " << std::hex << deadbeef << "\n";
	std::cout << "  via std::oct       : " << std::oct << deadbeef << std::dec << "\n";

	// --- 4. string round-trips in non-trivial bases --------------------------
	rule("string in, string out, across bases");
	uinteger_t z("zz", 36);  // largest two-digit base-36 value
	std::printf("  uinteger_t(\"zz\", 36) -> base 10 : %s\n", z.str(10).c_str());
	std::printf("  back to base 36                 : %s\n", z.str(36).c_str());
	uinteger_t big_hex("ffffffffffffffffffffffff", 16);  // 96 ones, 2^96 - 1
	std::printf("  2^96 - 1 (24 hex f's) base 10   : %s\n", big_hex.str(10).c_str());

	// --- 5. comparisons across magnitudes ------------------------------------
	rule("comparisons across magnitudes");
	std::printf("  u64_max < 2^128            : %s\n", u64_max < two_128 ? "true" : "false");
	std::printf("  2^128 >= (1 << 128)        : %s\n", two_128 >= shifted ? "true" : "false");
	std::printf("  2^64 != 2^128              : %s\n", two_64 != two_128 ? "true" : "false");
	std::printf("  2^128 == 2^128             : %s\n", two_128 == two_128 ? "true" : "false");
	// Sort a few values of wildly different sizes to show ordering is by value.
	std::printf("  u64_max < 2^64 < 2^128     : %s\n",
		(u64_max < two_64 && two_64 < two_128) ? "true" : "false");

	// --- 6. divmod: quotient and remainder in one call -----------------------
	rule("divmod() returns quotient and remainder together");
	uinteger_t divisor("1000000000000", 10);  // 10^12
	auto [q, r] = fact.divmod(divisor);
	std::printf("  40! / 10^12        : %s\n", q.str().c_str());
	std::printf("  40! %% 10^12        : %s   (the low 12 decimal digits)\n", r.str().c_str());
	std::printf("  q * 10^12 + r == 40! : %s\n",
		(q * divisor + r == fact) ? "true" : "false");

	std::puts("\ndone.");
	return 0;
}

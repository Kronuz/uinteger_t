// Exactness of the reciprocal and the 3-by-2 division step at 16-bit limbs, where the whole
// input space is small enough to enumerate: k_inv_3by2(d1, d0) must equal
// floor((B^3 - 1) / (d1 B + d0)) - B for all 2^31 normalized two-limb divisors (about 15 s),
// including the 720 whose last correction (top == d1 && lo >= d0) almost no random divisor
// reaches at any limb width; k_div_3by2 must match 64-bit division on a sample of divisors.
// Optional strides over d1 (argv[1] reciprocals, argv[2] divisions) trade coverage for time.
// Build: c++ -std=c++17 -O2 -DDIGIT_T=std::uint16_t -DHALF_DIGIT_T=std::uint8_t -I. test/recip16.cc
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#define private public
#include "uinteger_t.hh"
#undef private

using U = uinteger_t;
using D = U::digit;
static_assert(U::digit_bits == 16, "build with DIGIT_T=std::uint16_t HALF_DIGIT_T=std::uint8_t");

static const std::uint64_t B = 1ull << 16;
static std::mt19937_64 rng(777);
static unsigned long long checked_inv = 0, checked_div = 0, bad = 0;

static D rnd_digit() {
	const D top = static_cast<D>(~D(0));
	switch (rng() % 8) {
		case 0: return 0;
		case 1: return 1;
		case 2: return top;
		case 3: return static_cast<D>(top - 1);
		case 4: return static_cast<D>(1u << 15);
		case 5: return static_cast<D>((1u << 15) + 1);
		case 6: return static_cast<D>(rng() >> (rng() % 16));
		default: return static_cast<D>(rng());
	}
}

static D check_inv(std::uint64_t d1, std::uint64_t d0) {
	++checked_inv;
	const D v = U::k_inv_3by2(static_cast<D>(d1), static_cast<D>(d0));
	const std::uint64_t expect = (B * B * B - 1) / (d1 * B + d0) - B;
	if (v != expect && ++bad <= 10) {
		std::printf("FAIL k_inv_3by2(%04llx, %04llx) = %04x, expected %04llx\n", static_cast<unsigned long long>(d1), static_cast<unsigned long long>(d0), static_cast<unsigned>(v), static_cast<unsigned long long>(expect));
	}
	return v;
}

// A few dividends below d * B for one divisor, the extremes included.
static void check_div(std::uint64_t d1, std::uint64_t d0, D v, int cases) {
	const std::uint64_t d = d1 * B + d0;
	for (int i = 0; i < cases; ++i) {
		std::uint64_t top;
		switch (rng() % 4) {
			case 0: top = d - 1; break;
			case 1: top = (d1 - 1) * B + rnd_digit(); break;
			case 2: top = rng() % d; break;
			default: top = (static_cast<std::uint64_t>(rnd_digit()) * B + rnd_digit()) % d; break;
		}
		const D n0 = rnd_digit();
		const std::uint64_t n = top * B + n0;
		D r1, r0;
		const D q = U::k_div_3by2(static_cast<D>(top >> 16), static_cast<D>(top), n0, static_cast<D>(d1), static_cast<D>(d0), v, &r1, &r0);
		++checked_div;
		if ((q != n / d || (static_cast<std::uint64_t>(r1) << 16 | r0) != n % d) && ++bad <= 10) {
			std::printf("FAIL k_div_3by2(%08llx:%04x / %08llx)\n", static_cast<unsigned long long>(top), static_cast<unsigned>(n0), static_cast<unsigned long long>(d));
		}
	}
}

int main(int argc, char** argv) {
	// argv[1]: stride over d1 for the reciprocals (1 = all 2^31 divisors, the default);
	// argv[2]: stride over d1 for the division step. A stride above 1 takes every divisor
	// with d1 below B/2 + 1024 whole: the last correction of the reciprocal is reached by 720
	// divisors in all, 228 of them in that range (none above d1 = 0x9bdd).
	const std::uint64_t inv_stride = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1;
	const std::uint64_t div_stride = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 251;
	const std::uint64_t whole = inv_stride > 1 ? 1024 : 0;
	unsigned long long corrected = 0;
	for (std::uint64_t d1 = B / 2; d1 < B; d1 += d1 < B / 2 + whole ? 1 : inv_stride) {
		const D v0 = U::k_inv_1(static_cast<D>(d1));
		for (std::uint64_t d0 = 0; d0 < B; ++d0) {
			const D v = check_inv(d1, d0);
			// Divisors whose reciprocal needed both corrections: the division step on a sample.
			if (v + 2u <= v0 && ++corrected % 211 == 0) {
				check_div(d1, d0, v, 2);
			}
		}
	}
	for (std::uint64_t d1 = B / 2; d1 < B; d1 += div_stride) {
		for (std::uint64_t d0 = 0; d0 < B; ++d0) {
			check_div(d1, d0, check_inv(d1, d0), 2);
		}
	}
	for (std::uint64_t d1 : {0x8000ull, 0x8001ull, 0xfffeull, 0xffffull}) {
		for (std::uint64_t d0 = 0; d0 < B; ++d0) {
			check_div(d1, d0, check_inv(d1, d0), 16);
		}
	}
	if (bad) {
		std::printf("recip16: %llu failures\n", bad);
		return 1;
	}
	std::printf("recip16 OK (%llu reciprocals, %llu divisions)\n", checked_inv, checked_div);
	return 0;
}

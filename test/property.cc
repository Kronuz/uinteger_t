// Dependency-free property tests for the arithmetic kernels, checked against a
// reference schoolbook multiply on 32-bit words and against algebraic identities.
// Runs under CTest, so CI's sanitizer jobs cover every multiplication and division path.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>
// Whether this build takes the header's default conversion cutoffs (check_conversion_defaults).
#if !defined UINTEGER_T_RADIX_MAX_BITS && !defined UINTEGER_T_DIGITS_TREE_THRESHOLD && !defined UINTEGER_T_FROM_DIGITS_TREE_THRESHOLD
#define PROPERTY_DEFAULT_CUTOFFS 1
#endif
// The size helpers, kernels and cutoffs are private; the size-guard and buffer checks below reach them directly.
#define UINTEGER_T_PUBLIC_IMPLEMENTATION
#include "uinteger_t.hh"

#include "property/common.hh"
#include "property/kernels.hh"
#include "property/digits_reference.hh"
#include "property/radix_kernels.hh"
#include "property/digits_decode.hh"
#include "property/shapes.hh"
#include "property/operators.hh"
#include "property/repr.hh"

int main(int argc, char** argv) {
	int iters = argc > 1 ? std::atoi(argv[1]) : 300;
	std::size_t big = argc > 2 ? static_cast<std::size_t>(std::atoi(argv[2])) : 1200;
	light = argc > 3 && std::atoi(argv[3]) != 0;

	for (int it = 0; it < iters; ++it) {
		auto a = random_words(pick_words(big));
		auto b = random_words(pick_words(big));
		check_mul(a, b);
		check_sqr(a);

		auto ua = to_uint(a), ub = to_uint(b);
		check_div(ua, ub, "random");
		check_div(ua * ub + ub - uinteger_t(1), ub, "max remainder");
		if (ub > uinteger_t(1)) check_div(ua * ub, ub, "exact");
		if (it % 4 == 0) check_public_forms(ua, ub);

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
	check_leading_zero_bytes();
	check_bytes();
	check_digit_errors();
	check_radix_kernel();
	check_split_shapes();
	check_horner_bounds();
	check_size_guards();
	check_repr();
	check_kernel_sizes();
	check_toom4_patterns();
	check_toom4_cap();
	check_toom4_dispatch();
	check_ntt_shapes();

	const std::size_t karatsuba = U::mul_karatsuba_threshold;

	// Lopsided products of three or more chunks whose last chunk takes the odd split
	// (bn = 2*cn - 1) into scratch an earlier chunk dirtied.
	for (std::size_t cn = karatsuba; cn < karatsuba + 6; ++cn) {
		std::size_t bn = 2 * cn - 1;
		for (std::size_t an : {2 * bn + cn, 3 * bn + cn, 2 * bn + cn + 1}) {
			check_mul(random_words(words_for(an)), random_words(words_for(bn)));
		}
	}

	// Divisors whose top limbs make the quotient-digit estimate overshoot or saturate.
	for (std::size_t n = 2; n <= 40; n += 3) {
		for (int k = 0; k < 6; ++k) {
			words d(words_for(n), 0xffffffffu), x(words_for(n + 3 + k), 0xffffffffu);
			if (k & 1) d[0] = 1;
			if (k & 2) { std::fill(d.begin(), d.end() - 1, 0u); d.back() = 0x80000000u; }
			if (k & 4) x[x.size() / 2] = 0;
			check_div(to_uint(x), to_uint(d), "edge divisor");
			auto ud = to_uint(d);
			check_div(ud * ud - uinteger_t(1), ud, "square minus one");
		}
	}

	// Light mode keeps two rounds: a random divisor and one with its top bit set.
	check_dc_shapes(light ? 2 : 4);
	check_newton_shapes(light ? 1 : 2);

	if (failures) {
		std::printf("property: %d failures\n", failures);
		return 1;
	}
	std::printf("property OK (%d iterations, up to %zu words%s)\n", iters, big, light ? ", light" : "");
	return 0;
}

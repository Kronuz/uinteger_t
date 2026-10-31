// Kernel-level check of the recursive division: k_div_dc against k_div_sb on raw limbs, with
// the scratch buffer exactly k_div_dc_scratch_size limbs between guard limbs, and filled with
// different patterns to show the result does not depend on its contents. The kernels are
// private, so this defines UINTEGER_T_PUBLIC_IMPLEMENTATION before the header.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#define UINTEGER_T_PUBLIC_IMPLEMENTATION
#include "uinteger_t.hh"

using U = uinteger_t;
using digit = U::digit;

static std::mt19937_64 rng(20261031);
static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); if (++failures > 10) std::exit(1); } } while (0)

static std::vector<digit> random_limbs(std::size_t n) {
	std::vector<digit> v(n);
	const int style = static_cast<int>(rng() % 5);
	for (auto& x : v) {
		switch (style) {
			case 0: x = static_cast<digit>(rng()); break;
			case 1: x = static_cast<digit>(~digit(0)); break;
			case 2: x = rng() % 4 ? 0 : static_cast<digit>(rng()); break;
			case 3: x = (rng() & 1) ? static_cast<digit>(~digit(0)) : 0; break;
			default: x = static_cast<digit>(rng() >> (rng() % 64)); break;
		}
	}
	return v;
}

static void check_shape(std::size_t dn, std::size_t m) {
	const std::size_t nn = dn + m;
	auto d = random_limbs(dn);
	d[dn - 1] |= digit(1) << (U::digit_bits - 1);
	auto a = random_limbs(nn);
	// The top dn limbs must be below d.
	a[nn - 1] = static_cast<digit>(rng() % d[dn - 1]);

	std::vector<digit> n_sb(a), q_sb(m);
	U::k_div_sb(q_sb.data(), n_sb.data(), nn, d.data(), dn);

	const std::size_t need = U::k_div_dc_scratch_size(nn, dn);
	const std::size_t guard = 8;
	for (digit fill : {digit(0), static_cast<digit>(~digit(0)), digit(0x5a5a5a5a5a5a5a5aull)}) {
		std::vector<digit> n_dc(a), q_dc(m + 2 * guard, fill);
		std::vector<digit> buf(need + 2 * guard, fill);
		for (std::size_t i = 0; i < guard; ++i) {
			buf[i] = buf[need + guard + i] = digit(0xc0ffee00 + i);
		}
		U::k_div_dc(q_dc.data() + guard, n_dc.data(), nn, d.data(), dn, buf.data() + guard);
		for (std::size_t i = 0; i < guard; ++i) {
			CHECK(buf[i] == digit(0xc0ffee00 + i) && buf[need + guard + i] == digit(0xc0ffee00 + i), "scratch guard hit, dn=%zu m=%zu need=%zu", dn, m, need);
		}
		for (std::size_t i = 0; i < guard; ++i) {
			CHECK(q_dc[i] == fill && q_dc[m + guard + i] == fill, "quotient guard hit, dn=%zu m=%zu", dn, m);
		}
		CHECK(std::equal(q_sb.begin(), q_sb.end(), q_dc.begin() + guard), "quotient differs, dn=%zu m=%zu", dn, m);
		CHECK(std::equal(n_sb.begin(), n_sb.begin() + dn, n_dc.begin()), "remainder differs, dn=%zu m=%zu", dn, m);
	}
	CHECK(need <= 8 * nn + 256, "scratch %zu limbs for %zu by %zu", need, nn, dn);
}

int main(int argc, char** argv) {
	const int iters = argc > 1 ? std::atoi(argv[1]) : 400;
	const std::size_t big = argc > 2 ? static_cast<std::size_t>(std::atoi(argv[2])) : 700;
	const std::size_t t = DIV_DC_THRESHOLD;
	for (int it = 0; it < iters; ++it) {
		std::size_t dn = 2 + rng() % big;
		std::size_t m = 1 + rng() % (3 * dn);
		switch (rng() % 4) {
			case 0: dn = t - 1 + rng() % 3; break;
			case 1: m = DIV_DC_STEP_THRESHOLD - 1 + rng() % 3; break;
			case 2: m = dn - (dn > 2 ? rng() % 2 : 0); break;
			default: break;
		}
		if (dn < 2) dn = 2;
		if (m < 1) m = 1;
		check_shape(dn, m);
	}
	if (failures) {
		std::printf("dc_kernel: %d failures\n", failures);
		return 1;
	}
	std::printf("dc_kernel OK (%d shapes)\n", iters);
	return 0;
}

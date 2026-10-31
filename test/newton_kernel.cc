// Kernel-level check of the Newton (reciprocal) division (opens up `private`; run by CTest): the
// reciprocal against its defining inequality, the division against a dividend built as Q d + R (so the
// expected quotient and remainder never come from the code under test), with divisors and quotients at
// the edges, dirty buffers with guard limbs around an exactly sized scratch, the number of corrections
// each block makes against its bound, and the size plan at narrower size types. The thresholds
// (UINTEGER_T_DIV_NEWTON_THRESHOLD, _STEP_THRESHOLD and _INV_BASE) can be set small to run the
// iteration and the blocks on small operands.
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

#define private public
#include "uinteger_t.hh"
#undef private

using U = uinteger_t;
using digit = U::digit;

static std::mt19937_64 rng(20261031);
static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); if (++failures > 10) std::exit(1); } } while (0)

static const digit ones = static_cast<digit>(~digit(0));
static const digit top_bit = static_cast<digit>(digit(1) << (U::digit_bits - 1));

// Schoolbook product, r[0..an+bn) = a * b, from the limb primitives only.
static std::vector<digit> t_mul(const std::vector<digit>& a, const std::vector<digit>& b) {
	std::vector<digit> r(a.size() + b.size(), 0);
	for (std::size_t i = 0; i < b.size(); ++i) {
		digit carry = 0;
		for (std::size_t j = 0; j < a.size(); ++j) {
			carry = U::_multadd(a[j], b[i], r[i + j], carry, &r[i + j]);
		}
		r[i + a.size()] = carry;
	}
	return r;
}

// A limb pattern: random, all ones, sparse, or runs of ones and zeros.
static std::vector<digit> pattern(std::size_t n, int style) {
	std::vector<digit> v(n);
	for (auto& x : v) {
		switch (style) {
			case 0: x = static_cast<digit>(rng()); break;
			case 1: x = ones; break;
			case 2: x = rng() % 4 ? 0 : static_cast<digit>(rng()); break;
			case 3: x = (rng() & 1) ? ones : 0; break;
			case 4: x = static_cast<digit>(rng() >> (rng() % 64)); break;
			default: x = 0; break;
		}
	}
	return v;
}

// A normalized divisor of n limbs, with the shapes that stress the reciprocal: the least (B^n / 2), one
// above it, the greatest (B^n - 1), a power of two, and random low limbs under a random top limb.
static std::vector<digit> divisor(std::size_t n, int shape) {
	std::vector<digit> d;
	switch (shape) {
		case 0: d.assign(n, 0); d[n - 1] = top_bit; break;
		case 1: d.assign(n, 0); d[n - 1] = top_bit; d[0] = 1; break;
		case 2: d.assign(n, ones); break;
		case 3: d.assign(n, ones); d[0] = static_cast<digit>(ones - 1); break;
		case 4: d.assign(n, 0); d[n - 1] = ones; break;
		case 5: d.assign(n, ones); d[n - 1] = top_bit; break;
		case 6: d.assign(n, 0); d[n - 1] = top_bit; std::fill(d.begin(), d.begin() + (n - 1) / 2, ones); break;
		default: d = pattern(n, static_cast<int>(rng() % 5)); break;
	}
	d[n - 1] |= top_bit;
	return d;
}

static const int shapes = 10;
static const std::size_t guard = 8;

// Scratch of exactly `need` limbs between guard limbs, filled with `fill`.
struct guarded {
	std::vector<digit> buf;
	std::size_t need;
	guarded(std::size_t n, digit fill) : buf(n + 2 * guard, fill), need(n) {
		for (std::size_t i = 0; i < guard; ++i) {
			buf[i] = buf[need + guard + i] = static_cast<digit>(0xc0ffee00 + i);
		}
	}
	digit* data() { return buf.data() + guard; }
	bool intact() const {
		for (std::size_t i = 0; i < guard; ++i) {
			if (buf[i] != static_cast<digit>(0xc0ffee00 + i) || buf[need + guard + i] != static_cast<digit>(0xc0ffee00 + i)) {
				return false;
			}
		}
		return true;
	}
};

static const digit fills[] = {0, ones, static_cast<digit>(0x5a5a5a5a5a5a5a5aull)};

// 0 <= B^(2n) - t W < 2 t for the reciprocal W of t.
static void check_inv(std::size_t n, int shape) {
	const auto t = divisor(n, shape);
	for (digit fill : fills) {
		guarded sc(U::k_div_newton_inv_scratch_size(n), fill);
		guarded out(n + 1, fill);
		U::k_div_newton_inv(out.data(), t.data(), n, sc.data());
		CHECK(sc.intact() && out.intact(), "guard hit, n=%zu shape=%d", n, shape);
		std::vector<digit> w(out.data(), out.data() + n + 1);
		CHECK(w[n] == 1, "reciprocal top limb %llu, n=%zu shape=%d", static_cast<unsigned long long>(w[n]), n, shape);
		auto prod = t_mul(t, w);                       // 2n + 1 limbs
		std::vector<digit> pow(2 * n + 1, 0);
		pow[2 * n] = 1;
		const digit borrow = U::k_sub_n(pow.data(), pow.data(), prod.data(), 2 * n + 1);
		CHECK(!borrow, "t W exceeds B^(2n), n=%zu shape=%d", n, shape);
		std::vector<digit> t2(2 * n + 1, 0);           // 2 t
		t2[n] = U::k_lshift(t2.data(), t.data(), n, 1);
		CHECK(U::k_cmp_n(pow.data(), t2.data(), 2 * n + 1) < 0, "B^(2n) - t W is not below 2 t, n=%zu shape=%d", n, shape);
	}
}

static unsigned worst_down = 0, worst_up = 0;

// The quotient of a = Q d + R by d, with a block-by-block replica of k_div_newton that reports the
// corrections of each block.
static void check_div(std::size_t dn, std::size_t m, int dshape, int qstyle, int rstyle) {
	const auto d = divisor(dn, dshape);
	auto Q = pattern(m, qstyle);
	if (qstyle == 6) {
		Q.assign(m, 0);
		Q[m - 1] = static_cast<digit>(top_bit >> 1);
	}
	auto R = pattern(dn, rstyle);
	// R below d: clear the top limb under d's, or take d - 1.
	if (rstyle == 6) {
		R = d;
		U::k_sub_1(R.data(), R.data(), dn, 1);
	} else {
		R[dn - 1] = static_cast<digit>(R[dn - 1] % d[dn - 1]);
		if (R[dn - 1] == d[dn - 1]) R[dn - 1] = 0;
	}
	auto a = t_mul(d, Q);
	a.resize(dn + m, 0);
	const digit carry = U::k_add_n(a.data(), a.data(), R.data(), dn);
	U::k_add_1(a.data() + dn, a.data() + dn, m, carry);
	const std::size_t nn = dn + m;

	const std::size_t need = U::k_div_newton_scratch_size(nn, dn);
	for (digit fill : fills) {
		auto n_ = a;
		guarded q(m, fill), sc(need, fill);
		U::k_div_newton(q.data(), n_.data(), nn, d.data(), dn, sc.data());
		CHECK(sc.intact() && q.intact(), "guard hit, dn=%zu m=%zu", dn, m);
		CHECK(std::equal(Q.begin(), Q.end(), q.data()), "quotient differs, dn=%zu m=%zu dshape=%d qstyle=%d rstyle=%d", dn, m, dshape, qstyle, rstyle);
		CHECK(std::equal(R.begin(), R.end(), n_.begin()), "remainder differs, dn=%zu m=%zu dshape=%d qstyle=%d rstyle=%d", dn, m, dshape, qstyle, rstyle);
	}

	// The same blocks, counting corrections.
	auto n_ = a;
	std::vector<digit> q(m);
	guarded sc(need, 0);
	const std::size_t c = U::k_div_newton_block(m, dn);
	digit* w = sc.data();
	U::k_div_newton_inv(w, d.data() + (dn - c), c, w + c + 1);
	std::size_t mm = m, cb = m % c ? m % c : c;
	while (mm) {
		mm -= cb;
		const auto count = U::k_div_newton_step(q.data() + mm, n_.data() + mm, dn, cb, d.data(), w + (c - cb), w + c + 1);
		CHECK(count.down <= U::div_newton_down && count.up <= U::div_newton_up, "%u down and %u up corrections, dn=%zu m=%zu block=%zu", count.down, count.up, dn, m, cb);
		worst_down = std::max(worst_down, count.down);
		worst_up = std::max(worst_up, count.up);
		cb = c;
	}
	CHECK(std::equal(Q.begin(), Q.end(), q.begin()) && std::equal(R.begin(), R.end(), n_.begin()), "replica differs, dn=%zu m=%zu", dn, m);
}

// The plan at a narrower size type: it refuses what its arithmetic could not hold, and agrees with the
// full-width plan where it accepts.
template <typename Size>
static void check_plan(Size nn, Size dn) {
	Size narrow = 0;
	std::size_t full = 0;
	const bool ok_full = U::_div_newton_plan<std::size_t>(nn, dn, &full);
	const bool ok = U::_div_newton_plan<Size>(nn, dn, &narrow);
	const unsigned long long max = std::numeric_limits<Size>::max();
	const bool fits = ok_full && nn <= max / 64 && full <= max / sizeof(digit) / 4;
	CHECK(ok == fits, "plan %d, expected %d at %zu / %zu in %zu-bit sizes", ok, fits, static_cast<std::size_t>(nn), static_cast<std::size_t>(dn), sizeof(Size) * 8);
	if (ok && fits) {
		CHECK(static_cast<std::size_t>(narrow) == full, "plan scratch %zu, expected %zu", static_cast<std::size_t>(narrow), full);
	}
}

int main(int argc, char** argv) {
	const int iters = argc > 1 ? std::atoi(argv[1]) : 300;
	const std::size_t big = argc > 2 ? static_cast<std::size_t>(std::atoi(argv[2])) : 400;

	// Compile-time shape of the thresholds the test relies on.
	static_assert(U::div_newton_inv_base >= 3, "the reciprocal recursion needs n >= 3");

	// The block size: at least two blocks for a quotient of two limbs or more, none above the divisor's size.
	for (std::size_t dn = 2; dn <= 300; dn += 1 + dn / 20) {
		for (std::size_t m = 2; m <= 1000; m += 1 + m / 30) {
			const std::size_t c = U::k_div_newton_block(m, dn);
			const std::size_t blocks = (m + c - 1) / c;
			CHECK(c >= 1 && c <= dn && blocks >= 2 && blocks <= std::max<std::size_t>(2, (m + dn - 1) / dn), "block %zu for %zu limbs over a divisor of %zu: %zu blocks", c, m, dn, blocks);
		}
	}

	for (std::size_t n = 1; n <= 3 * U::div_newton_inv_base + 20 && n <= big; ++n) {
		for (int shape = 0; shape < shapes; ++shape) {
			check_inv(n, shape);
		}
	}
	for (int it = 0; it < iters; ++it) {
		check_inv(1 + rng() % big, static_cast<int>(rng() % shapes));
	}

	for (int it = 0; it < iters; ++it) {
		std::size_t dn = 2 + rng() % big;
		std::size_t m = 1 + rng() % (3 * dn);
		switch (rng() % 6) {
			case 0: m = dn; break;
			case 1: m = 1 + rng() % 4; break;
			case 2: m = dn * (1 + rng() % 4); break;
			case 3: m = dn - rng() % 2; break;
			case 4: m = dn * (1 + rng() % 3) + 1; break;
			default: break;
		}
		if (m < 1) m = 1;
		check_div(dn, m, static_cast<int>(rng() % shapes), static_cast<int>(rng() % 7), static_cast<int>(rng() % 7));
	}
	// Quotient and divisor shapes in a sweep: every divisor size from 2, the quotient shorter than, equal
	// to and longer than it.
	for (std::size_t dn = 2; dn <= std::min<std::size_t>(big, 2 * U::div_newton_inv_base + 10); ++dn) {
		for (std::size_t m : {std::size_t(1), std::size_t(2), dn / 2 + 1, dn - 1 > 0 ? dn - 1 : 1, dn, dn + 1, 2 * dn, 2 * dn + 1, 5 * dn + 3}) {
			check_div(dn, m, static_cast<int>(rng() % shapes), static_cast<int>(rng() % 7), static_cast<int>(rng() % 7));
		}
	}

	// Size plans at 16- and 32-bit size types.
	for (std::size_t nn = 2; nn < 400; nn += 1 + nn / 16) {
		for (std::size_t dn = 2; dn < nn; dn += 1 + dn / 8) {
			check_plan<std::uint16_t>(static_cast<std::uint16_t>(nn), static_cast<std::uint16_t>(dn));
			check_plan<std::uint32_t>(static_cast<std::uint32_t>(nn), static_cast<std::uint32_t>(dn));
		}
	}
#ifndef UINTEGER_T_MUL_KARATSUBA_THRESHOLD
	// Near the largest operands a 32-bit size type takes: the dividend a sixty-fourth of the address
	// space, then one limb more, with the divisor at half, and with a quotient of one limb.
	{
		typedef std::uint32_t S;
		const S edge = std::numeric_limits<S>::max() / 64;
		for (S nn : {static_cast<S>(edge - 1), edge, static_cast<S>(edge + 1), static_cast<S>(edge / 2)}) {
			for (S dn : {static_cast<S>(nn / 2), static_cast<S>(nn - 1), static_cast<S>(nn / 8), static_cast<S>(1000)}) {
				check_plan<S>(nn, dn);
			}
		}
		S sc = 0;
		CHECK(!U::_div_newton_plan<S>(static_cast<S>(edge + 1), static_cast<S>(edge / 2), &sc), "an oversized dividend is refused");
		CHECK(!U::_div_newton_plan<S>(std::numeric_limits<S>::max(), static_cast<S>(1000), &sc), "a dividend of the whole address space is refused");
	}
#endif

	if (failures) {
		std::printf("newton_kernel: %d failures\n", failures);
		return 1;
	}
	std::printf("newton_kernel OK (%d shapes, at most %u down and %u up corrections)\n", iters, worst_down, worst_up);
	return 0;
}

// The transform multiplication kernels against independent arithmetic (opens up `private`; run by
// CTest): the primes, roots and constants, the word primitives, the transform against a naive DFT, the
// product against schoolbook on dirty buffers with guard limbs around an exactly sized scratch, and the
// size plan at narrower size types. UINTEGER_T_NTT_LEAF can be set small to run the recursion on short
// transforms, and the limb width and UINTEGER_T_NO_INT128 select the primitives under test.
#define UINT_T_PUBLIC_IMPLEMENTATION
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>
#include "uinteger_t.hh"

using U = uinteger_t;
using digit = U::digit;
using word = U::ntt_word;

static std::mt19937_64 rng(20261031);
static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); if (++failures > 10) std::exit(1); } } while (0)

// Arithmetic modulo p < 2^62 that shares nothing with the library: double-and-add.
static word t_addmod(word a, word b, word p) {
	word s = a + b;
	return s >= p ? s - p : s;
}

static word t_mulmod(word a, word b, word p) {
	word r = 0;
	for (int i = 63; i >= 0; --i) {
		r = t_addmod(r, r, p);
		if ((b >> i) & 1) r = t_addmod(r, a, p);
	}
	return r;
}

static word t_powmod(word a, word e, word p) {
	word r = 1;
	for (int i = 63; i >= 0; --i) {
		r = t_mulmod(r, r, p);
		if ((e >> i) & 1) r = t_mulmod(r, a, p);
	}
	return r;
}

// Miller-Rabin with the first twelve primes as bases, which is deterministic below 3.3e24.
static bool t_is_prime(word n) {
	static const word bases[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
	for (word b : bases) {
		if (n % b == 0) return n == b;
	}
	word d = n - 1;
	int s = 0;
	while (!(d & 1)) { d >>= 1; ++s; }
	for (word b : bases) {
		word x = t_powmod(b, d, n);
		if (x == 1 || x == n - 1) continue;
		bool witness = true;
		for (int i = 1; i < s && witness; ++i) {
			x = t_mulmod(x, x, n);
			if (x == n - 1) witness = false;
		}
		if (witness) return false;
	}
	return true;
}

// (hi, lo) = x * y from 32-bit halves.
static word t_mul128(word x, word y, word* lo) {
	const word m = 0xffffffffu;
	word x0 = x & m, x1 = x >> 32, y0 = y & m, y1 = y >> 32;
	word p00 = x0 * y0, p01 = x0 * y1, p10 = x1 * y0, p11 = x1 * y1;
	word mid = (p00 >> 32) + (p01 & m) + (p10 & m);
	*lo = (mid << 32) | (p00 & m);
	return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

static word rand_below(word n) {
	return rng() % n;
}

static word edge_word(int k, word p) {
	switch (k) {
		case 0: return 0;
		case 1: return 1;
		case 2: return p - 1;
		case 3: return p;
		case 4: return p + 1;
		case 5: return 2 * p - 1;
		case 6: return 2 * p;
		case 7: return ~static_cast<word>(0);
		case 8: return ~static_cast<word>(0) - p;
		case 9: return 3 * p;
		default: return rng();
	}
}

static void check_constants() {
	const word two61 = static_cast<word>(1) << 61, two62 = static_cast<word>(1) << 62;
	const word order = static_cast<word>(3) << U::ntt_log_max;
	for (unsigned i = 0; i < 3; ++i) {
		const word p = U::ntt_prime(i), w = U::ntt_root(i);
		CHECK(p > two61 && p < two62, "prime %u is not in (2^61, 2^62)", i);
		CHECK(t_is_prime(p), "prime %u is not prime", i);
		CHECK(p % order == 1, "prime %u is not 1 mod 3 2^%u", i, U::ntt_log_max);
		CHECK(w > 1 && w < p, "root %u out of range", i);
		// order exactly 3 2^53: w^(order / 2) = -1 and w^(order / 3) != 1 (and w^order = 1)
		CHECK(t_powmod(w, order / 2, p) == p - 1, "root %u has order below 3 2^%u", i, U::ntt_log_max);
		CHECK(t_powmod(w, order / 3, p) != 1, "root %u has order dividing 2^%u", i, U::ntt_log_max);
		CHECK(t_powmod(w, order, p) == 1, "root %u is not a 3 2^%u-th root of unity", i, U::ntt_log_max);
		for (unsigned j = 0; j < i; ++j) CHECK(p != U::ntt_prime(j), "primes %u and %u are equal", i, j);
	}
	// The coefficients of a product are below N 2^128 for N <= 2^53 and the three primes multiply to more.
	CHECK(U::ntt_log_max + 128 < 3 * 61, "three primes of 61 bits do not cover 2^%u 2^128", U::ntt_log_max);

	const word p0 = U::ntt_prime(0), p1 = U::ntt_prime(1), p2 = U::ntt_prime(2);
	CHECK(t_mulmod(U::ntt_crt_inv01, p0 % p1, p1) == 1, "p0^-1 mod p1");
	CHECK(U::ntt_crt_p0_mod_p2 == p0 % p2, "p0 mod p2");
	CHECK(t_mulmod(t_mulmod(p0 % p2, p1 % p2, p2), U::ntt_crt_inv012, p2) == 1, "(p0 p1)^-1 mod p2");
	word lo;
	word hi = t_mul128(p0, p1, &lo);
	CHECK(hi == U::ntt_crt_p01_hi && lo == U::ntt_crt_p01_lo, "p0 p1 is %llx:%llx", static_cast<unsigned long long>(hi), static_cast<unsigned long long>(lo));
	CHECK(U::ntt_crt_inv01 < p1 && U::ntt_crt_inv012 < p2, "CRT constants are reduced");
}

static void check_primitives() {
	for (int it = 0; it < 20000; ++it) {
		word x = it < 12 ? edge_word(it, 0x3ea0000000000001ull) : rng(), y = it < 12 ? edge_word(11 - it, 0x3ae0000000000001ull) : rng();
		word lo, ref_lo;
		word hi = U::_mul64(x, y, &lo);
		word ref_hi = t_mul128(x, y, &ref_lo);
		CHECK(hi == ref_hi && lo == ref_lo, "_mul64(%llx, %llx)", static_cast<unsigned long long>(x), static_cast<unsigned long long>(y));
		CHECK(U::_mulhi64(x, y) == ref_hi, "_mulhi64(%llx, %llx)", static_cast<unsigned long long>(x), static_cast<unsigned long long>(y));

		// _div64: q d + r = (h, l) with r < d, for h < d.
		word d = rng() >> (rng() % 64);
		if (!d) d = 1;
		word h = rand_below(d), l = rng();
		if (it % 7 == 0) h = d - 1;
		if (it % 11 == 0) l = ~static_cast<word>(0);
		word q;
		word r = U::_div64(h, l, d, &q);
		word qd_lo;
		word qd_hi = t_mul128(q, d, &qd_lo);
		word s_lo = qd_lo + r;
		word s_hi = qd_hi + (s_lo < qd_lo);
		CHECK(r < d && s_hi == h && s_lo == l, "_div64(%llx:%llx, %llx)", static_cast<unsigned long long>(h), static_cast<unsigned long long>(l), static_cast<unsigned long long>(d));
	}

	for (unsigned i = 0; i < 3; ++i) {
		const U::ntt_mod m = U::ntt_make_mod(i);
		const word p = m.p;
		CHECK(p == U::ntt_prime(i) && m.p2 == 2 * p, "modulus %u", i);
		CHECK(m.mu * p + m.r == 0 && m.r < p, "mu and 2^64 mod p of prime %u", i);  // 2^64 = mu p + r
		CHECK(static_cast<word>(m.pinv * p) == 1, "p^-1 mod 2^64 of prime %u", i);
		for (int it = 0; it < 20000; ++it) {
			word w = it < 5 ? (it == 0 ? 0 : it == 1 ? 1 : it == 2 ? 2 : it == 3 ? p - 1 : p - 2) : rand_below(p);
			// The quotient: floor(w 2^64 / p), so that w 2^64 - q p is in [0, p).
			word q = U::ntt_quot(m, w);
			word qp_lo;
			word qp_hi = t_mul128(q, p, &qp_lo);
			word r_lo = static_cast<word>(0 - qp_lo);
			word r_hi = w - qp_hi - (qp_lo != 0);
			CHECK(r_hi == 0 && r_lo < p, "Shoup quotient of %llx modulo prime %u", static_cast<unsigned long long>(w), i);

			word x = it < 10 ? edge_word(it, p) : rng();
			word got = U::ntt_mulq(x, w, q, p);
			word want = t_mulmod(x % p, w, p);
			CHECK(got < 2 * p && got % p == want, "Shoup product %llx * %llx modulo prime %u", static_cast<unsigned long long>(x), static_cast<unsigned long long>(w), i);
			CHECK(U::ntt_mulmod(m, x, w) == want, "ntt_mulmod %llx * %llx modulo prime %u", static_cast<unsigned long long>(x), static_cast<unsigned long long>(w), i);
		}
		for (unsigned lg = 0; lg <= U::ntt_log_max; ++lg) {
			// unscale(lg) * n = 2^64 (mod p) for n = 2^lg and 3 2^lg
			word pow = t_powmod(2, lg, p);
			CHECK(t_mulmod(U::ntt_unscale(m, lg, false), pow, p) == m.r, "2^64 / 2^%u mod prime %u", lg, i);
			CHECK(t_mulmod(U::ntt_unscale(m, lg, true), t_mulmod(pow, 3, p), p) == m.r, "2^64 / (3 2^%u) mod prime %u", lg, i);
		}
		CHECK(U::ntt_half(2 * p - 1, p) == (2 * p - 1 + p) / 2 && U::ntt_half(10, p) == 5 && U::ntt_half(p + 1, p) == p + 1 - (p + 1) / 2, "ntt_half modulo prime %u", i);
		CHECK(t_mulmod(U::ntt_half(2 * p - 2, p), 2, p) == p - 2 && t_mulmod(U::ntt_half(2 * p - 1, p) % p, 2, p) == p - 1, "ntt_half halves modulo prime %u", i);
	}
}

// ntt_table and the transform of n = 2^lg or 3 2^lg words against a naive DFT, in the output order: bit-reversed
// within each block of 2^lg after the radix-3 layer (block j, index t: the point w^(j + 3 bitrev(t))).
static std::size_t bitrev(std::size_t x, unsigned bits) {
	std::size_t r = 0;
	for (unsigned i = 0; i < bits; ++i) r |= ((x >> i) & 1) << (bits - 1 - i);
	return r;
}

static void check_transform(unsigned i, unsigned lg, bool three) {
	const std::size_t pow2 = static_cast<std::size_t>(1) << lg;
	const std::size_t n = three ? 3 * pow2 : pow2;
	const std::size_t last = three ? 2 * pow2 : n / 2;
	const U::ntt_mod m = U::ntt_make_mod(i);
	const word p = m.p;
	word w = U::ntt_root(i);
	if (!three) w = t_powmod(w, 3, p);
	for (unsigned j = lg; j < U::ntt_log_max; ++j) w = t_mulmod(w, w, p);
	CHECK(t_powmod(w, n, p) == 1 && (n < 2 || t_powmod(w, n / 2, p) == p - 1) && (!three || t_powmod(w, n / 3, p) != 1), "root of order %zu of prime %u", n, i);

	std::vector<word> tw(2 * last + 2 + 2, 0x5a5a5a5a5a5a5a5aull);
	U::ntt_table(tw.data(), last, m, w);
	CHECK(tw[2 * last + 2] == 0x5a5a5a5a5a5a5a5aull && tw[2 * last + 3] == 0x5a5a5a5a5a5a5a5aull, "table of %zu wrote past its words", n);
	CHECK(U::ntt_table_words(n) == 2 * last + 2, "table words of %zu", n);
	word pw = 1;
	for (std::size_t t = 0; t <= last; ++t) {
		CHECK(tw[2 * t] == pw, "table entry %zu of %zu, prime %u", t, n, i);
		CHECK(tw[2 * t + 1] == U::ntt_quot(m, pw), "table quotient %zu of %zu, prime %u", t, n, i);
		pw = t_mulmod(pw, w, p);
	}
	CHECK(n < 2 || tw[2 * (n / 2)] == p - 1, "w^(n/2) is -1");

	U::ntt_ctx c = {p, 2 * p, tw.data(), n, 0, 0};
	if (three) {
		// s = (z - z^2) / 2 with z = w^(n/3): s^2 = -3 / 4
		const word z = tw[2 * pow2], z2 = tw[4 * pow2];
		const word d = z + p - z2;
		c.s = U::ntt_half(d, p) % p;
		c.sq = U::ntt_quot(m, c.s);
		CHECK(t_mulmod(c.s, 2, p) == (z + p - z2) % p, "2 s = z - z^2");
	}
	for (int round = 0; round < 3; ++round) {
		std::vector<word> x(n), x0(n);
		for (std::size_t k = 0; k < n; ++k) {
			x0[k] = round == 0 ? rand_below(2 * p) : round == 1 ? 2 * p - 1 : (k & 1 ? p - 1 : 0);
			x[k] = x0[k];
		}
		U::ntt_forward(x.data(), c);
		for (std::size_t idx = 0; idx < n; ++idx) {
			// x(w^e) with e from the output index
			const std::size_t e = three ? idx / pow2 + 3 * bitrev(idx % pow2, lg) : bitrev(idx, lg);
			word sum = 0, wp = 1, step = t_powmod(w, e, p);
			for (std::size_t k = 0; k < n; ++k) {
				sum = t_addmod(sum, t_mulmod(x0[k] % p, wp, p), p);
				wp = t_mulmod(wp, step, p);
			}
			CHECK(x[idx] < 2 * p && x[idx] % p == sum, "transform of %zu words, prime %u, output %zu", n, i, idx);
		}
		U::ntt_inverse(x.data(), c);
		for (std::size_t k = 0; k < n; ++k) {
			CHECK(x[k] < 2 * p && x[k] % p == t_mulmod(x0[k] % p, n % p, p), "inverse of %zu words, prime %u, element %zu", n, i, k);
		}
	}
}

// Limbs for the operand shapes that stress the reductions: random, all ones, sparse, small, limbs at and
// around the primes (a limb can exceed 2p), and alternating zero and all ones.
static std::vector<digit> make_limbs(std::size_t n, int style) {
	std::vector<digit> v(n);
	for (std::size_t i = 0; i < n; ++i) {
		switch (style) {
			case 0: v[i] = static_cast<digit>(rng()); break;
			case 1: v[i] = static_cast<digit>(~digit(0)); break;
			case 2: v[i] = rng() % 3 ? 0 : static_cast<digit>(rng()); break;
			case 3: v[i] = static_cast<digit>(rng() >> (rng() % 64)); break;
			case 4: v[i] = static_cast<digit>(edge_word(static_cast<int>(rng() % 10), U::ntt_prime(static_cast<unsigned>(rng() % 3)))); break;
			default: v[i] = (i & 1) ? static_cast<digit>(~digit(0)) : 0; break;
		}
	}
	if (n) v.back() |= 1;
	return v;
}

static const digit guard_pattern = static_cast<digit>(0x5a5a5a5a5a5a5a5aull);
static const std::size_t guard_limbs = 8;

// a * b (or a^2) by k_ntt_mul on a dirty result with a guard, and a dirty scratch of exactly the planned
// size with a guard, started at each limb offset that keeps 8-byte alignment out of reach of the plan.
static void check_kernel(const std::vector<digit>& a, const std::vector<digit>& b, bool sqr, std::size_t offset) {
	const std::size_t an = a.size(), bn = sqr ? an : b.size();
	const std::size_t sn = U::k_ntt_scratch_size(an, bn, sqr);
	std::vector<digit> r(an + bn + guard_limbs), s(sn + guard_limbs + offset);
	for (std::size_t i = 0; i < an + bn; ++i) r[i] = static_cast<digit>(rng());
	for (std::size_t i = 0; i < sn + offset; ++i) s[i] = static_cast<digit>(rng());
	for (std::size_t i = 0; i < guard_limbs; ++i) r[an + bn + i] = s[sn + offset + i] = guard_pattern;
	// The plan reserves a word for alignment, so the scratch may start up to 8 - sizeof(digit) bytes into
	// the buffer's own alignment: offset limbs past an 8-byte boundary, then the plan's sn limbs.
	digit* sp = s.data() + offset;
	U::k_ntt_mul(r.data(), a.data(), an, b.data(), bn, sqr, sp);
	std::vector<digit> want(an + bn);
	U::k_mul_basecase(want.data(), an >= bn ? a.data() : b.data(), std::max(an, bn), an >= bn ? b.data() : a.data(), std::min(an, bn));
	bool same = std::equal(want.begin(), want.end(), r.begin());
	CHECK(same, "k_ntt_mul %zu x %zu limbs%s", an, bn, sqr ? " (square)" : "");
	bool guards = true;
	for (std::size_t i = 0; i < guard_limbs; ++i) guards = guards && r[an + bn + i] == guard_pattern && s[sn + offset + i] == guard_pattern;
	CHECK(guards, "k_ntt_mul %zu x %zu limbs%s wrote past its buffers (scratch %zu limbs at offset %zu)", an, bn, sqr ? " (square)" : "", sn, offset);
}

static void check_kernel_shapes(std::size_t an, std::size_t bn) {
	const int style_a = static_cast<int>(rng() % 6), style_b = static_cast<int>(rng() % 6);
	auto a = make_limbs(an, style_a), b = make_limbs(bn, style_b);
	const std::size_t offsets = 8 / sizeof(digit);
	check_kernel(a, b, false, rng() % offsets);
	if (an == bn) check_kernel(a, a, true, rng() % offsets);
}

// The plan against an independent computation at a size type of the given width.
template <typename Size>
static void check_plan_at(Size an, Size bn, bool sqr) {
	typedef unsigned long long wide;
	const wide per = U::ntt_limbs_per_word;
	const wide max = std::numeric_limits<Size>::max();
	const wide aw = static_cast<wide>(an) / per + (static_cast<wide>(an) % per != 0);
	const wide bw = sqr ? aw : static_cast<wide>(bn) / per + (static_cast<wide>(bn) % per != 0);
	const wide cw = aw + bw - 1;
	// the least of 2^k and 3 2^k (k >= 2) not below cw
	wide n = 0;
	for (wide two = 1; two <= (static_cast<wide>(1) << 62); two <<= 1) {
		if (two >= cw) { n = two; break; }
		if (two >= 8 && 3 * (two / 2) >= cw) { n = 3 * (two / 2); break; }
	}
	const bool three = n % 3 == 0;
	const wide table = three ? 2 * (2 * (n / 3)) + 2 : 2 * (n / 2) + 2;
	const wide words = (sqr ? 3 : 4) * n + table + 1;
	const bool fits = n != 0 && n <= (static_cast<wide>(1) << U::ntt_log_max) && words * 8 <= max / 4;
	Size len = 0, scratch = 0;
	const bool ok = U::_ntt_plan<Size>(an, bn, sqr, &len, &scratch);
	CHECK(ok == fits, "plan for %llu x %llu limbs%s at %zu-bit size: %s", static_cast<wide>(an), static_cast<wide>(bn), sqr ? " (square)" : "", 8 * sizeof(Size), ok ? "usable" : "refused");
	if (ok && fits) {
		CHECK(len == n && scratch == words * 8 / sizeof(digit), "plan for %llu x %llu limbs at %zu-bit size: length %llu scratch %llu", static_cast<wide>(an), static_cast<wide>(bn), 8 * sizeof(Size), static_cast<wide>(len), static_cast<wide>(scratch));
		// The callers add a few multiples of an operand size to this: it cannot wrap.
		CHECK(static_cast<wide>(scratch) * sizeof(digit) <= max / 4, "plan scratch exceeds a quarter of the range");
	}
}

template <typename Size>
static void check_plan_sizes() {
	const unsigned long long max = std::numeric_limits<Size>::max();
	const unsigned long long per = U::ntt_limbs_per_word;
	// Every length around each power of two the size type can reach, and around the point where the scratch
	// stops fitting.
	for (unsigned lg = 0; lg < 8 * sizeof(Size) && lg <= 56; ++lg) {
		const unsigned long long n = 1ull << lg;
		for (long long d = -3; d <= 3; ++d) {
			for (unsigned long long cw : {n, n / 2 + 1}) {
				// operands of cw / 2 + 1 words (so the product has about cw coefficients) and the extremes
				for (unsigned long long words : {cw / 2 + d + 1, cw + d, 2 * cw + d}) {
					if (words == 0 || words > max / per) continue;
					const unsigned long long limbs = words * per;
					if (limbs > max) continue;
					check_plan_at<Size>(static_cast<Size>(limbs), static_cast<Size>(limbs), true);
					check_plan_at<Size>(static_cast<Size>(limbs), static_cast<Size>(limbs), false);
					check_plan_at<Size>(static_cast<Size>(limbs), static_cast<Size>(limbs / 2 + 1), false);
				}
			}
		}
	}
	check_plan_at<Size>(1, 1, false);
	check_plan_at<Size>(1, 1, true);
	check_plan_at<Size>(static_cast<Size>(max), static_cast<Size>(max), false);
	check_plan_at<Size>(static_cast<Size>(max), static_cast<Size>(max), true);
	check_plan_at<Size>(static_cast<Size>(max), 1, false);
}

static void check_plans() {
	check_plan_sizes<std::uint16_t>();
	check_plan_sizes<std::uint32_t>();
	check_plan_sizes<std::uint64_t>();
	check_plan_sizes<std::size_t>();
	// A 32-bit size type refuses a product whose transform needs more than a quarter of 4 GiB, and takes one
	// just under; 2^24 words give 5 * 2^24 words = 640 MiB of scratch.
	typedef std::uint32_t S;
	S len = 0, scratch = 0;
	const S half = static_cast<S>((1u << 23) * U::ntt_limbs_per_word);  // limbs: 2^23 words
	CHECK(U::_ntt_plan<S>(half, half, false, &len, &scratch) && len == (1u << 24), "2^23-word operands fit a 32-bit size");
	CHECK(!U::_ntt_plan<S>(half + static_cast<S>(U::ntt_limbs_per_word), half + static_cast<S>(U::ntt_limbs_per_word), false, &len, &scratch), "2^23 + 1 words need a transform of 3 2^23 words: just over a quarter of 4 GiB of scratch, refused");
	// The lopsided bound: under ntt_lopsided times the smaller operand takes the transform, from there on chunks.
	{
		const std::size_t t = U::mul_ntt_threshold, f = U::ntt_lopsided;
		CHECK(U::k_mul_uses_ntt(f * t - 1, t) && U::k_mul_uses_ntt(t, t) && U::k_mul_uses_ntt(2 * t, t + 1), "products under %zu times lopsided take the transform", f);
		// the smaller operand needs two thirds of the cutoff, and the product twice the cutoff in all
		const std::size_t lo = t - t / 3;
		CHECK(U::k_mul_uses_ntt(2 * t - lo, lo) && !U::k_mul_uses_ntt(2 * t - lo - 1, lo) && !U::k_mul_uses_ntt(3 * t, lo - 1), "the floor of the smaller operand is two thirds of the cutoff");
		CHECK(U::k_mul_uses_ntt(t, t) && !U::k_mul_uses_ntt(t - 1, t - 1) && U::k_mul_uses_ntt(t + 1, t - 1) == (t - 1 >= lo), "the sum of both operands is at least twice the cutoff");
		CHECK(!U::k_mul_uses_ntt(f * t, t) && !U::k_mul_uses_ntt(f * t + 1, t) && !U::k_mul_uses_ntt(100 * f * t, t), "products %zu times lopsided or more do not", f);
		CHECK(!U::k_mul_uses_ntt(t - 1, t - 1) && !U::k_mul_uses_ntt(3 * t, t / 3), "products under the cutoff do not take the transform");
		CHECK(U::k_mul_uses_ntt(f * (t + 5) - 1, t + 5) && !U::k_mul_uses_ntt(f * (t + 5), t + 5), "the lopsided bound scales with the smaller operand");
		CHECK(U::k_sqr_uses_ntt(U::sqr_ntt_threshold) && !U::k_sqr_uses_ntt(U::sqr_ntt_threshold - 1), "the square cutoff");
	}
	// At the real size type the dispatch predicates agree with the plan, and the scratch size of k_mul
	// is the plan's for the sizes that take the transform.
	for (std::size_t an : {std::size_t(100), std::size_t(1000), std::size_t(5000), std::size_t(70000)}) {
		for (std::size_t bn : {an / 2 + 1, an - an / 4, an}) {
			std::size_t l = 0, sc = 0;
			const bool plan = U::_ntt_plan<std::size_t>(an, bn, false, &l, &sc);
			CHECK(U::k_mul_uses_ntt(an, bn) == (plan && 3 * bn >= 2 * U::mul_ntt_threshold && an < U::ntt_lopsided * bn && an + bn >= 2 * U::mul_ntt_threshold), "k_mul_uses_ntt(%zu, %zu)", an, bn);
			if (U::k_mul_uses_ntt(an, bn)) CHECK(U::k_mul_scratch_size(an, bn) == sc, "k_mul_scratch_size(%zu, %zu) is the plan's", an, bn);
		}
		std::size_t l = 0, sc = 0;
		const bool plan = U::_ntt_plan<std::size_t>(an, an, true, &l, &sc);
		CHECK(U::k_sqr_uses_ntt(an) == (plan && an >= U::sqr_ntt_threshold), "k_sqr_uses_ntt(%zu)", an);
		if (U::k_sqr_uses_ntt(an)) CHECK(U::k_sqr_scratch_size(an) == sc, "k_sqr_scratch_size(%zu) is the plan's", an);
	}
}

// The dispatch really takes the transform where it says it does: k_mul and k_sqr run on a scratch of the size
// they report, filled with a pattern; the transform's last word (the table's final quotient) is overwritten
// and Toom-3's smaller scratch use never reaches it.
static void check_dispatch(std::size_t an, std::size_t bn, bool sqr) {
	const std::size_t sn = sqr ? U::k_sqr_scratch_size(an) : U::k_mul_scratch_size(an, bn);
	const bool uses = sqr ? U::k_sqr_uses_ntt(an) : U::k_mul_uses_ntt(an, bn);
	const std::size_t per = U::ntt_limbs_per_word;
	if (!uses) return;
	auto a = make_limbs(an, 0), b = sqr ? a : make_limbs(bn, 0);
	std::vector<digit> r(an + bn), s(sn, guard_pattern);
	if (sqr) U::k_sqr(r.data(), a.data(), an, s.data());
	else U::k_mul(r.data(), a.data(), an, b.data(), bn, s.data());
	std::vector<digit> want(an + bn);
	U::k_mul_basecase(want.data(), a.data(), an, b.data(), bn);
	CHECK(r == want, "dispatched %s %zu x %zu limbs", sqr ? "square" : "product", an, bn);
	bool touched = false;
	for (std::size_t i = sn - 2 * per; i < sn - per; ++i) touched = touched || s[i] != guard_pattern;
	CHECK(touched, "the %s of %zu x %zu limbs did not take the transform", sqr ? "square" : "product", an, bn);
	// the alignment word at the end is never written
	bool slack = true;
	for (std::size_t i = sn - per; i < sn; ++i) slack = slack && s[i] == guard_pattern;
	CHECK(slack, "the %s of %zu x %zu limbs wrote its alignment word", sqr ? "square" : "product", an, bn);
}

int main(int argc, char** argv) {
	const int rounds = argc > 1 ? std::atoi(argv[1]) : 1;
	// A second argument other than 0 selects the light mode (CMake passes it in Debug builds, where CI runs
	// the sanitizers unoptimized): the geometric sizes step by half of the size (a quarter in full mode), the
	// dispatch section checks the sizes at and just above each cutoff and at twice it (the lopsided shape
	// only at the cutoff), a cutoff shared by products and squares once, and the chunked product once.
	const bool light = argc > 2 && std::atoi(argv[2]) != 0;
	check_constants();
	check_primitives();
	for (unsigned i = 0; i < 3; ++i) {
		for (unsigned lg = 0; lg <= 7; ++lg) check_transform(i, lg, false);
		for (unsigned lg = 2; lg <= 6; ++lg) check_transform(i, lg, true);
	}
	check_plans();

	// Every shape of a few limbs, then geometric sizes up to a few thousand limbs.
	for (std::size_t an = 1; an <= 20; ++an) {
		for (std::size_t bn = 1; bn <= an; ++bn) {
			for (int k = 0; k < 4 * rounds; ++k) check_kernel_shapes(an, bn);
		}
	}
	for (std::size_t an = 21; an <= 3000; an += 1 + an / (light ? 2 : 4)) {
		for (std::size_t bn : {an, an - an / 7, an / 2 + 1, an / 3 + 1, std::size_t(1)}) {
			if (!bn) continue;
			for (int k = 0; k < rounds; ++k) check_kernel_shapes(an, bn);
		}
	}
	// The dispatch from k_mul and k_sqr, at and above the cutoffs, balanced and within a factor of two.
	for (std::size_t which = 0; which < 2; ++which) {
		const std::size_t t = which ? U::sqr_ntt_threshold : U::mul_ntt_threshold;
		if (light && which && t == U::mul_ntt_threshold) continue;
		for (std::size_t n : {t, t + 1, t + 7, 2 * t, 3 * t + 1}) {
			if (n > 5000) continue;
			if (light && (n == t + 7 || n == 3 * t + 1)) continue;
			check_dispatch(n, n, false);
			check_dispatch(n, n, true);
			check_dispatch(n + n / 3, n, false);
			check_dispatch(2 * n - 1, n, false);
			check_dispatch(2 * n + 1, n, false);
			if (n <= 2000 && (!light || n == t)) check_dispatch(U::ntt_lopsided * n - 1, n, false);
		}
	}
	// Products longer than ntt_lopsided times the smaller operand are chunked, and the chunks take the transform.
	for (std::size_t bn : {U::mul_ntt_threshold, U::mul_ntt_threshold + 1}) {
		if (light && bn != U::mul_ntt_threshold) continue;
		const std::size_t an = U::ntt_lopsided * bn + 3;
		auto a = make_limbs(an, 0), b = make_limbs(bn, 0);
		const std::size_t sn = U::k_mul_scratch_size(an, bn);
		std::vector<digit> r(an + bn), want(an + bn), s(sn);
		U::k_mul(r.data(), a.data(), an, b.data(), bn, s.data());
		U::k_mul_basecase(want.data(), a.data(), an, b.data(), bn);
		CHECK(!U::k_mul_uses_ntt(an, bn) && r == want, "chunked %zu x %zu limbs", an, bn);
	}
	// The largest coefficients: all-ones operands whose every coefficient is a full sum.
	for (std::size_t n : {std::size_t(63), std::size_t(64), std::size_t(65), std::size_t(1023), std::size_t(1024), std::size_t(4097)}) {
		auto a = make_limbs(n, 1);
		check_kernel(a, a, false, 0);
		check_kernel(a, a, true, 0);
	}

	if (failures) {
		std::printf("ntt_kernel: %d failures\n", failures);
		return 1;
	}
	std::printf("ntt_kernel OK (%d rounds, %zu-bit limbs, leaf %zu%s)\n", rounds, static_cast<std::size_t>(U::digit_bits), U::ntt_leaf, light ? ", light" : "");
	return 0;
}

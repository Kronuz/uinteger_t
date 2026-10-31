// Schoolbook division kernel variants, identity-checked against the library's k_div_sb and timed.
// Build: c++ -std=c++17 -O2 -I. bench/sbmb.cc -I/usr/local/include -L/usr/local/lib -lgmp -o sbmb
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include <gmp.h>

#define private public
#include "uinteger_t.hh"
#undef private

using U = uinteger_t;
using digit = U::digit;
using Clock = std::chrono::steady_clock;

template <typename F>
static double bench(F&& f) {
	double best = 1e300;
	for (int round = 0; round < 7; ++round) {
		std::size_t n = 1;
		for (;;) {
			auto s = Clock::now();
			for (std::size_t i = 0; i < n; ++i) f(i);
			double us = std::chrono::duration<double, std::micro>(Clock::now() - s).count();
			if (us > 20000) { best = std::min(best, us / n); break; }
			n *= 2;
		}
	}
	return best * 1000.0;
}

static volatile digit dsink;

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// Branch-free first correction (Moller and Granlund, Algorithm 5 with the conditional as a mask).
static inline digit div3by2_m(digit n2, digit n1, digit n0, digit d1, digit d0, digit v, digit* rem1, digit* rem0) {
	digit q0;
	digit q1 = U::_mult(n2, v, &q0);
	q1 = static_cast<digit>(q1 + n2 + U::_addcarry(q0, n1, 0, &q0));
	digit r1 = static_cast<digit>(n1 - U::_mullo(d1, q1));
	digit r0;
	r1 = static_cast<digit>(r1 - d1 - U::_subborrow(n0, d0, 0, &r0));
	digit t0;
	digit t1 = U::_mult(d0, q1, &t0);
	r1 = static_cast<digit>(r1 - t1 - U::_subborrow(r0, t0, 0, &r0));
	q1 = static_cast<digit>(q1 + 1);
	const digit mask = static_cast<digit>(0) - static_cast<digit>(r1 >= q0);
	q1 = static_cast<digit>(q1 + mask);
	r1 = static_cast<digit>(r1 + (d1 & mask) + U::_addcarry(r0, d0 & mask, 0, &r0));
	if (UNLIKELY(r1 > d1 || (r1 == d1 && r0 >= d0))) {
		q1 = static_cast<digit>(q1 + 1);
		r1 = static_cast<digit>(r1 - d1 - U::_subborrow(r0, d0, 0, &r0));
	}
	*rem1 = r1;
	*rem0 = r0;
	return q1;
}

static inline digit submul_s2(digit* r, const digit* a, std::size_t n, digit m) {
	digit c = 0;
	for (std::size_t i = 0; i < n; ++i) {
		digit lo;
		digit hi = U::_mult(a[i], m, &lo);
		digit s;
		digit b1 = U::_subborrow(r[i], lo, 0, &s);
		digit b2 = U::_subborrow(s, c, 0, &r[i]);
		c = static_cast<digit>(hi + b1 + b2);
	}
	return c;
}

// A: the library loop with the mask 3by2.
static void div_sb_A(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn) {
	const digit d1 = dp[dn - 1];
	const digit d0 = dp[dn - 2];
	const digit dinv = U::k_inv_3by2(d1, d0);
	for (std::size_t i = nn - dn; i--;) {
		digit* w = np + i;
		digit qd;
		if (w[dn] == d1 && w[dn - 1] == d0) {
			qd = static_cast<digit>(~static_cast<digit>(0));
		} else {
			digit r1, r0;
			qd = div3by2_m(w[dn], w[dn - 1], w[dn - 2], d1, d0, dinv, &r1, &r0);
		}
		digit cy = U::k_submul_1(w, dp, dn, qd);
		digit bor = U::_subborrow(w[dn], cy, 0, &w[dn]);
		if (bor) {
			--qd;
			digit c = U::k_add_n(w, w, dp, dn);
			w[dn] = static_cast<digit>(w[dn] + c);
		}
		q[i] = qd;
	}
}

// B: A with the single-chain submul.
static void div_sb_B(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn) {
	const digit d1 = dp[dn - 1];
	const digit d0 = dp[dn - 2];
	const digit dinv = U::k_inv_3by2(d1, d0);
	for (std::size_t i = nn - dn; i--;) {
		digit* w = np + i;
		digit qd;
		if (w[dn] == d1 && w[dn - 1] == d0) {
			qd = static_cast<digit>(~static_cast<digit>(0));
		} else {
			digit r1, r0;
			qd = div3by2_m(w[dn], w[dn - 1], w[dn - 2], d1, d0, dinv, &r1, &r0);
		}
		digit cy = submul_s2(w, dp, dn, qd);
		digit bor = U::_subborrow(w[dn], cy, 0, &w[dn]);
		if (bor) {
			--qd;
			digit c = U::k_add_n(w, w, dp, dn);
			w[dn] = static_cast<digit>(w[dn] + c);
		}
		q[i] = qd;
	}
}

// C: B with the subtraction over dn-2 limbs; the 3-by-2 remainder gives the top two.
template <bool NEWSUB>
static void div_sb_C(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn) {
	const digit d1 = dp[dn - 1];
	const digit d0 = dp[dn - 2];
	const digit dinv = U::k_inv_3by2(d1, d0);
	for (std::size_t i = nn - dn; i--;) {
		digit* w = np + i;
		digit qd;
		if (UNLIKELY(w[dn] == d1 && w[dn - 1] == d0)) {
			qd = static_cast<digit>(~static_cast<digit>(0));
			digit cy = U::k_submul_1(w, dp, dn, qd);
			digit bor = U::_subborrow(w[dn], cy, 0, &w[dn]);
			if (bor) {
				--qd;
				digit c = U::k_add_n(w, w, dp, dn);
				w[dn] = static_cast<digit>(w[dn] + c);
			}
			q[i] = qd;
			continue;
		}
		digit r1, r0;
		qd = div3by2_m(w[dn], w[dn - 1], w[dn - 2], d1, d0, dinv, &r1, &r0);
		digit cy = NEWSUB ? submul_s2(w, dp, dn - 2, qd) : U::k_submul_1(w, dp, dn - 2, qd);
		// (r1, r0) -= cy
		digit b = U::_subborrow(r0, cy, 0, &r0);
		digit bor = U::_subborrow(r1, 0, b, &r1);
		w[dn - 2] = r0;
		w[dn - 1] = r1;
		w[dn] = 0;
		if (UNLIKELY(bor)) {
			--qd;
			U::k_add_n(w, w, dp, dn);
		}
		q[i] = qd;
	}
}

// T: quotient only. The product of digit i is subtracted only over the limbs that can still change
// a later 3-by-2 estimate (G guard limbs); the low remainder limbs are left stale.
template <int G>
static void div_sb_T(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn) {
	const digit d1 = dp[dn - 1];
	const digit d0 = dp[dn - 2];
	const digit dinv = U::k_inv_3by2(d1, d0);
	for (std::size_t i = nn - dn; i--;) {
		digit* w = np + i;
		digit qd;
		if (UNLIKELY(w[dn] == d1 && w[dn - 1] == d0)) {
			qd = static_cast<digit>(~static_cast<digit>(0));
			digit cy = U::k_submul_1(w, dp, dn, qd);
			digit bor = U::_subborrow(w[dn], cy, 0, &w[dn]);
			if (bor) { --qd; digit c = U::k_add_n(w, w, dp, dn); w[dn] = static_cast<digit>(w[dn] + c); }
			q[i] = qd;
			continue;
		}
		digit r1, r0;
		qd = U::k_div_3by2(w[dn], w[dn - 1], w[dn - 2], d1, d0, dinv, &r1, &r0);
		const std::size_t len = std::min<std::size_t>(dn - 2, i + G);
		const std::size_t k0 = dn - 2 - len;
		digit cy = submul_s2(w + k0, dp + k0, len, qd);
		digit b = U::_subborrow(r0, cy, 0, &w[dn - 2]);
		digit bor = U::_subborrow(r1, 0, b, &w[dn - 1]);
		w[dn] = 0;
		if (UNLIKELY(bor)) { --qd; U::k_add_n(w + k0, w + k0, dp + k0, dn - k0); }
		q[i] = qd;
	}
}

int main(int argc, char** argv) {
	std::mt19937_64 g(11);
	std::vector<int> sizes;
	for (int i = 1; i < argc; ++i) sizes.push_back(std::atoi(argv[i]));
	if (sizes.empty()) sizes = {3, 5, 9, 17, 33, 65, 129};
	constexpr int K = 32;
	std::printf("%-5s %10s %10s %10s %10s %10s   (ns per call, k_div_sb with qn = dn-1 digits)\n", "dn", "lib", "A mask", "B +sub", "C dn-2", "C2 +sub");
	for (int dn : sizes) {
		const std::size_t nn = 2 * dn - 1 + 1; // nn+1 as in knuth_divmod for an n/2 shape
		// identity first, including adversarial limbs
		for (int trial = 0; trial < 20000; ++trial) {
			const std::size_t dnn = 2 + g() % 20;
			const std::size_t m = 1 + g() % 12;
			std::vector<digit> d(dnn), a(dnn + m);
			int style = g() % 5;
			auto fill = [&](std::vector<digit>& v) {
				for (auto& x : v) switch (style) {
					case 0: x = g(); break;
					case 1: x = ~digit(0); break;
					case 2: x = (g() & 1) ? ~digit(0) : 0; break;
					case 3: x = g() % 3 ? ~digit(0) - (g() % 3) : g(); break;
					default: x = g() >> (g() % 64); break;
				}
			};
			fill(d); fill(a);
			d.back() |= digit(1) << 63;
			// the top dnn limbs of a must be below d: mostly a random block, or d - 1 (top limbs equal d's)
			if (g() % 3 == 0) {
				for (std::size_t k = 0; k < dnn; ++k) a[m + k] = d[k];
				for (std::size_t k = 0; k < dnn; ++k) { if (a[m + k]--) break; }
			} else {
				a.back() = static_cast<digit>(g()) % d.back();
			}
			std::vector<digit> n0(a), n1(a), n2(a), n3(a), n4(a), q0(m), q1(m), q2(m), q3(m), q4(m);
			U::k_div_sb(q0.data(), n0.data(), a.size(), d.data(), dnn);
			div_sb_A(q1.data(), n1.data(), a.size(), d.data(), dnn);
			div_sb_B(q2.data(), n2.data(), a.size(), d.data(), dnn);
			div_sb_C<false>(q3.data(), n3.data(), a.size(), d.data(), dnn);
			div_sb_C<true>(q4.data(), n4.data(), a.size(), d.data(), dnn);
			bool ok = q0 == q1 && q0 == q2 && q0 == q3 && q0 == q4;
			for (std::size_t k = 0; k < dnn; ++k) ok = ok && n0[k] == n1[k] && n0[k] == n2[k] && n0[k] == n3[k] && n0[k] == n4[k];
			if (!ok) { std::printf("MISMATCH dn=%zu m=%zu style=%d trial=%d\n", dnn, m, style, trial); return 1; }
		}
		{
			int bad2 = 0, bad4 = 0;
			for (int trial = 0; trial < 20000; ++trial) {
				std::vector<digit> d(dn), a(nn);
				for (auto& x : d) x = g();
				for (auto& x : a) x = g();
				d.back() |= digit(1) << 63;
				a.back() = static_cast<digit>(g()) % d.back();
				std::vector<digit> n0(a), n1(a), n2(a), q0(nn), q1(nn), q2(nn);
				U::k_div_sb(q0.data(), n0.data(), nn, d.data(), dn);
				div_sb_T<2>(q1.data(), n1.data(), nn, d.data(), dn);
				div_sb_T<4>(q2.data(), n2.data(), nn, d.data(), dn);
				bad2 += !std::equal(q0.begin(), q0.begin() + (nn - dn), q1.begin());
				bad4 += !std::equal(q0.begin(), q0.begin() + (nn - dn), q2.begin());
			}
			std::printf("dn=%d: quotient mismatches over 20000 random trials: G=2 %d, G=4 %d\n", dn, bad2, bad4);
		}
		std::vector<std::vector<digit>> N(K, std::vector<digit>(nn)), D(K, std::vector<digit>(dn));
		for (int k = 0; k < K; ++k) {
			for (auto& w : D[k]) w = g();
			D[k].back() |= digit(1) << 63;
			for (auto& w : N[k]) w = g();
			N[k].back() = g() % D[k].back();
		}
		std::vector<digit> work(nn), q(nn);
		auto run = [&](auto&& f) {
			return bench([&](std::size_t i) {
				std::copy(N[i % K].begin(), N[i % K].end(), work.begin());
				f(q.data(), work.data(), nn, D[i % K].data(), static_cast<std::size_t>(dn));
				dsink = q[0];
			});
		};
		double copy = bench([&](std::size_t i) { std::copy(N[i % K].begin(), N[i % K].end(), work.begin()); dsink = work[0]; });
		double tl = run(U::k_div_sb) - copy;
		double ta = run(div_sb_A) - copy;
		double tb = run(div_sb_B) - copy;
		double tc = run(div_sb_C<false>) - copy;
		double td = run(div_sb_C<true>) - copy;
		double te = run(div_sb_T<2>) - copy;
		double tf = run(div_sb_T<4>) - copy;
		std::printf("%-5d %10.1f %10.1f %10.1f %10.1f %10.1f   T(G=2) %10.1f T(G=4) %10.1f\n", dn, tl, ta, tb, tc, td, te, tf);
	}
}

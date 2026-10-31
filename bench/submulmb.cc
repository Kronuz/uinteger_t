// Microbenchmark of submul_1 variants against the library's and GMP's, ns per call and per limb.
// Build: c++ -std=c++17 -O2 -I. bench/submulmb.cc -I/usr/local/include -L/usr/local/lib -lgmp -o submulmb
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

// s1: one carry word through the chain; the borrow of r - lo and the high limb are off the chain.
static digit submul_s1(digit* r, const digit* a, std::size_t n, digit m) {
	digit c = 0;
	for (std::size_t i = 0; i < n; ++i) {
		digit lo;
		digit hi = U::_mult(a[i], m, &lo);
		digit x = r[i];
		digit s = static_cast<digit>(x - lo);
		hi += x < lo;
		digit t = static_cast<digit>(s - c);
		hi += s < c;
		r[i] = t;
		c = hi;
	}
	return c;
}

// s2: the same with the builtin borrow.
static digit submul_s2(digit* r, const digit* a, std::size_t n, digit m) {
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

// s3: s1 unrolled by two.
static digit submul_s3(digit* r, const digit* a, std::size_t n, digit m) {
	digit c = 0;
	std::size_t i = 0;
	for (; i + 2 <= n; i += 2) {
		digit lo0, lo1;
		digit hi0 = U::_mult(a[i], m, &lo0);
		digit hi1 = U::_mult(a[i + 1], m, &lo1);
		digit x0 = r[i], x1 = r[i + 1];
		digit s0 = static_cast<digit>(x0 - lo0);
		hi0 += x0 < lo0;
		digit t0 = static_cast<digit>(s0 - c);
		hi0 += s0 < c;
		r[i] = t0;
		digit s1 = static_cast<digit>(x1 - lo1);
		hi1 += x1 < lo1;
		digit t1 = static_cast<digit>(s1 - hi0);
		hi1 += s1 < hi0;
		r[i + 1] = t1;
		c = hi1;
	}
	for (; i < n; ++i) {
		digit lo;
		digit hi = U::_mult(a[i], m, &lo);
		digit x = r[i];
		digit s = static_cast<digit>(x - lo);
		hi += x < lo;
		digit t = static_cast<digit>(s - c);
		hi += s < c;
		r[i] = t;
		c = hi;
	}
	return c;
}

// s4: multiply into a temporary through k_mul_1 (the same carry-last chain), then one subtraction.
static digit submul_s4(digit* r, const digit* a, std::size_t n, digit m) {
	digit tmp[512];
	digit c = U::k_mul_1(tmp, a, n, m);
	return static_cast<digit>(c + U::k_sub_n(r, r, tmp, n));
}

// s5: r - a*m = ~(~r + a*m); the carry-out of the complemented addmul is the borrow word.
static digit submul_s5(digit* r, const digit* a, std::size_t n, digit m) {
	digit c = 0;
	for (std::size_t i = 0; i < n; ++i) {
		digit lo;
		c = U::_multadd(a[i], m, static_cast<digit>(~r[i]), c, &lo);
		r[i] = static_cast<digit>(~lo);
	}
	return c;
}

int main(int argc, char** argv) {
	std::mt19937_64 g(7);
	std::vector<int> sizes;
	for (int i = 1; i < argc; ++i) sizes.push_back(std::atoi(argv[i]));
	if (sizes.empty()) sizes = {7, 9, 17, 33, 65, 129, 257};
	constexpr int K = 16;
	std::printf("%-5s %9s %9s %9s %9s %9s %9s %9s %9s %9s  (ns per limb)\n", "n", "ours", "s1", "s2", "s3", "s4", "s5", "mul_1", "addmul_1", "gmp sub");
	for (int n : sizes) {
		std::vector<std::vector<digit>> A(K, std::vector<digit>(n));
		std::vector<digit> M(K);
		for (int k = 0; k < K; ++k) { for (auto& w : A[k]) w = g(); M[k] = g(); }
		std::vector<digit> r(n + 1, 12345), r0(n + 1);
		// identity: all variants against the library kernel
		for (int k = 0; k < K; ++k) {
			for (auto& w : r0) w = g();
			std::vector<digit> r0b(r0), r1(r0), r2(r0), r3(r0), r4(r0);
			digit c0 = U::k_submul_1(r0.data(), A[k].data(), n, M[k]);
			digit c1 = submul_s1(r1.data(), A[k].data(), n, M[k]);
			digit c2 = submul_s2(r2.data(), A[k].data(), n, M[k]);
			digit c3 = submul_s3(r3.data(), A[k].data(), n, M[k]);
			digit c4 = submul_s4(r4.data(), A[k].data(), n, M[k]);
			std::vector<digit> r5(r0b); digit c5 = submul_s5(r5.data(), A[k].data(), n, M[k]);
			if (c0 != c1 || c0 != c2 || c0 != c3 || c0 != c4 || c0 != c5 || r0 != r5 || r0 != r1 || r0 != r2 || r0 != r3 || r0 != r4) { std::printf("MISMATCH n=%d\n", n); return 1; }
		}
		auto t0 = bench([&](std::size_t i) { dsink = U::k_submul_1(r.data(), A[i % K].data(), n, M[i % K]); });
		auto t1 = bench([&](std::size_t i) { dsink = submul_s1(r.data(), A[i % K].data(), n, M[i % K]); });
		auto t2 = bench([&](std::size_t i) { dsink = submul_s2(r.data(), A[i % K].data(), n, M[i % K]); });
		auto t3 = bench([&](std::size_t i) { dsink = submul_s3(r.data(), A[i % K].data(), n, M[i % K]); });
		auto t4 = bench([&](std::size_t i) { dsink = submul_s4(r.data(), A[i % K].data(), n, M[i % K]); });
		auto t5 = bench([&](std::size_t i) { dsink = submul_s5(r.data(), A[i % K].data(), n, M[i % K]); });
		auto tm = bench([&](std::size_t i) { dsink = U::k_mul_1(r.data(), A[i % K].data(), n, M[i % K]); });
		auto ta = bench([&](std::size_t i) { dsink = U::k_addmul_1(r.data(), A[i % K].data(), n, M[i % K]); });
		auto tg = bench([&](std::size_t i) { dsink = mpn_submul_1(reinterpret_cast<mp_limb_t*>(r.data()), reinterpret_cast<const mp_limb_t*>(A[i % K].data()), n, M[i % K]); });
		std::printf("%-5d %9.3f %9.3f %9.3f %9.3f %9.3f %9.3f %9.3f %9.3f %9.3f\n", n, t0 / n, t1 / n, t2 / n, t3 / n, t4 / n, t5 / n, tm / n, ta / n, tg / n);
	}
}

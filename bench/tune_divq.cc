// Quotient-only division, `a / d`, over divisor and quotient sizes: the sweep behind the dispatch of
// the truncated schoolbook kernel (div_trunc_threshold) and its comparison with GMP's mpz_tdiv_q.
// Build: c++ -std=c++17 -O2 -I. bench/tune_divq.cc [-DHAVE_GMP -lgmp] -o tune_divq
//   -DUINTEGER_T_DIV_TRUNC_THRESHOLD=0 never takes the truncated kernel; a large value (100000) takes it
//   for every quotient no longer than the divisor.
// Run: ./tune_divq r1 8 16 32 64     quotient as long as the divisor (ratio r<x> of divisor limbs)
//      ./tune_divq q8 64 256 1024    a quotient of 8 limbs for each divisor size
//      ./tune_divq xr1 16 64         the dividend is an exact multiple of the divisor (a flagged digit
//                                    makes the truncated kernel redo the division exactly)
// Prints the divisor limbs, the quotient limbs and ns per division (GMP's alongside when built).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#ifdef HAVE_GMP
#include <gmp.h>
#endif

#define UINTEGER_T_PUBLIC_IMPLEMENTATION
#include "uinteger_t.hh"

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
			if (us > 20000) {
				best = std::min(best, us / n);
				break;
			}
			n *= 2;
		}
	}
	return best * 1000.0;
}

static volatile std::size_t sink;

static U from_limbs(const std::vector<digit>& v) {
	U x;
	x._value.assign(v.data(), v.data() + v.size());
	x.trim();
	return x;
}

int main(int argc, char** argv) {
	if (argc < 3) {
		std::printf("usage: %s r<ratio>|q<count> divisor-limbs...\n", argv[0]);
		return 1;
	}
	const bool exact = argv[1][0] == 'x';
	const char* spec = argv[1] + (exact ? 1 : 0);
	const char kind = spec[0];
	const double x = std::atof(spec + 1);
	std::mt19937_64 g(17);
	constexpr int K = 16;
	std::printf("%-6s %-6s %12s", "dn", "qn", "ours ns");
#ifdef HAVE_GMP
	std::printf(" %12s %8s", "gmp ns", "ours/gmp");
#endif
	std::printf("\n");
	for (int i = 2; i < argc; ++i) {
		const std::size_t dn = static_cast<std::size_t>(std::atoi(argv[i]));
		const std::size_t qn = std::max<std::size_t>(1, kind == 'r' ? static_cast<std::size_t>(x * dn + 0.5) : static_cast<std::size_t>(x));
		const std::size_t nn = dn + qn;
		std::vector<U> A(K), D(K);
		for (int k = 0; k < K; ++k) {
			std::vector<digit> a(nn), d(dn);
			for (auto& w : a) w = static_cast<digit>(g());
			for (auto& w : d) w = static_cast<digit>(g());
			a.back() |= 1;
			d.back() |= 1;
			A[k] = from_limbs(a);
			D[k] = from_limbs(d);
			if (exact) {
				a.resize(qn);
				A[k] = D[k] * from_limbs(a);
			}
		}
		const double ours = bench([&](std::size_t j) { sink = (A[j % K] / D[j % K]).size(); });
		std::printf("%-6zu %-6zu %12.1f", dn, qn, ours);
#ifdef HAVE_GMP
		std::vector<__mpz_struct> ZA(K), ZD(K);
		mpz_t zq;
		mpz_init(zq);
		for (int k = 0; k < K; ++k) {
			mpz_init(&ZA[k]);
			mpz_init(&ZD[k]);
			mpz_import(&ZA[k], A[k].size(), -1, sizeof(digit), 0, 0, A[k].data());
			mpz_import(&ZD[k], D[k].size(), -1, sizeof(digit), 0, 0, D[k].data());
		}
		const double gmp = bench([&](std::size_t j) { mpz_tdiv_q(zq, &ZA[j % K], &ZD[j % K]); sink = mpz_size(zq); });
		std::printf(" %12.1f %8.2f", gmp, ours / gmp);
		for (int k = 0; k < K; ++k) {
			mpz_clear(&ZA[k]);
			mpz_clear(&ZD[k]);
		}
		mpz_clear(zq);
#endif
		std::printf("\n");
		std::fflush(stdout);
	}
	return 0;
}

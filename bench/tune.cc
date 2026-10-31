// Times each multiplication and squaring algorithm at the top level (the recursion below it takes
// the library's own cutoffs), over a sweep of limb counts, to find where one overtakes the next.
// Build: c++ -std=c++17 -O2 -I. bench/tune.cc -o tune   (cutoff macros as for the tests, e.g.
// -DUINTEGER_T_MUL_KARATSUBA_THRESHOLD=32 -DUINTEGER_T_MUL_TOOM3_THRESHOLD=100)
// Run: ./tune [min max step]  (default 8 400 8); columns are ns per call, "-" where an algorithm does not apply.
#define UINT_T_PUBLIC_IMPLEMENTATION
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include "uinteger_t.hh"

using U = uinteger_t;
using digit = U::digit;
using Clock = std::chrono::steady_clock;

// Best of several rounds, in nanoseconds per call.
template <typename F>
static double time_ns(F&& f) {
	double best = 1e300;
	for (int round = 0; round < 5; ++round) {
		std::size_t n = 1;
		for (;;) {
			auto s = Clock::now();
			for (std::size_t i = 0; i < n; ++i) f();
			double ns = std::chrono::duration<double, std::nano>(Clock::now() - s).count();
			if (ns > 4e6) { best = std::min(best, ns / n); break; }
			n *= 2;
		}
	}
	return best;
}

static void cell(double v) {
	v < 0 ? std::printf(" %9s", "-") : std::printf(" %9.0f", v);
}

int main(int argc, char** argv) {
	std::size_t lo = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 8;
	std::size_t hi = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 400;
	std::size_t step = argc > 3 ? std::strtoul(argv[3], nullptr, 10) : 8;
	std::mt19937_64 g(7);
	std::printf("cutoffs (limbs): mul karatsuba %zu toom3 %zu, sqr karatsuba %zu toom3 %zu\n",
		U::mul_karatsuba_threshold, U::mul_toom3_threshold, U::sqr_karatsuba_threshold, U::sqr_toom3_threshold);
	std::printf("%6s | %9s %9s %9s %9s | %9s %9s %9s %9s\n", "limbs", "mul base", "mul kara", "mul toom", "mul k_mul", "sqr base", "sqr kara", "sqr toom", "sqr k_sqr");
	for (std::size_t n = lo; n <= hi; n += step) {
		std::vector<digit> a(n), b(n), r(2 * n), s(16 * n + 1024);
		for (auto& x : a) x = g();
		for (auto& x : b) x = g();
		a.back() |= 1; b.back() |= 1;
		const bool toom = n > 2 * ((n + 2) / 3);
		std::printf("%6zu |", n);
		cell(time_ns([&] { U::k_mul_basecase(r.data(), a.data(), n, b.data(), n); }));
		cell(n >= 2 ? time_ns([&] { U::k_mul_karatsuba(r.data(), a.data(), n, b.data(), n, s.data()); }) : -1);
		cell(toom ? time_ns([&] { U::k_mul_toom3(r.data(), a.data(), n, b.data(), n, s.data()); }) : -1);
		cell(time_ns([&] { U::k_mul(r.data(), a.data(), n, b.data(), n, s.data()); }));
		std::printf(" |");
		cell(time_ns([&] { U::k_sqr_basecase(r.data(), a.data(), n); }));
		cell(n >= 2 ? time_ns([&] { U::k_sqr_karatsuba(r.data(), a.data(), n, s.data()); }) : -1);
		cell(toom ? time_ns([&] { U::k_sqr_toom3(r.data(), a.data(), n, s.data()); }) : -1);
		cell(time_ns([&] { U::k_sqr(r.data(), a.data(), n, s.data()); }));
		std::printf("\n");
		std::fflush(stdout);
	}
}

// Times each multiplication and squaring algorithm at the top level (the recursion below it takes
// the library's own cutoffs), over a sweep of limb counts, to find where one overtakes the next.
// Build: c++ -std=c++17 -O2 -I. bench/tune.cc -o tune   (cutoff macros as for the tests, e.g.
// -DUINTEGER_T_MUL_KARATSUBA_THRESHOLD=32 -DUINTEGER_T_MUL_TOOM3_THRESHOLD=100)
// Run: ./tune [min max step]  (default 8 400 8); columns are ns per call, "-" where an algorithm does not apply.
//      ./tune -r an  times Karatsuba, Toom-3 and the transform on an x bn for bn from an / 2 to an.
//      ./tune -l bn  times lopsided products an x bn (an = 2, 3, 4, 6, 8, 12, 16 bn): the transform over the whole
//      product against k_mul (which chunks the longer operand).
//      ./tune -t n...  times only Toom-3, Toom-4 and the transform at each limb count n (for the sizes around their crossovers);
//      with TUNE_NTT_ONLY set in the environment, only the transform.
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
	if (argc > 2 && std::string(argv[1]) == "-r") {
		// Unbalanced shapes: an x bn for bn from an / 2 up, Karatsuba against Toom-3 and the transform where they apply.
		const std::size_t an = std::strtoul(argv[2], nullptr, 10);
		std::printf("%6s %6s | %9s %9s %9s %9s %9s\n", "an", "bn", "mul kara", "mul toom3", "mul toom4", "mul ntt", "mul k_mul");
		for (std::size_t bn = an / 2; bn <= an; bn += std::max<std::size_t>(1, an / 40)) {
			std::vector<digit> a(an), b(bn), r(an + bn), s(16 * an + 1024 + U::k_ntt_scratch_size(an, bn, false));
			for (auto& x : a) x = g();
			for (auto& x : b) x = g();
			a.back() |= 1; b.back() |= 1;
			std::printf("%6zu %6zu |", an, bn);
			cell(time_ns([&] { U::k_mul_karatsuba(r.data(), a.data(), an, b.data(), bn, s.data()); }));
			cell(bn > 2 * ((an + 2) / 3) ? time_ns([&] { U::k_mul_toom3(r.data(), a.data(), an, b.data(), bn, s.data()); }) : -1);
			cell(bn > 3 * ((an + 3) / 4) ? time_ns([&] { U::k_mul_toom4(r.data(), a.data(), an, b.data(), bn, s.data()); }) : -1);
			cell(time_ns([&] { U::k_ntt_mul(r.data(), a.data(), an, b.data(), bn, false, s.data()); }));
			cell(time_ns([&] { U::k_mul(r.data(), a.data(), an, b.data(), bn, s.data()); }));
			std::printf("\n");
			std::fflush(stdout);
		}
		return 0;
	}
	if (argc > 2 && std::string(argv[1]) == "-l") {
		const std::size_t bn = std::strtoul(argv[2], nullptr, 10);
		std::printf("%8s %8s | %11s %11s\n", "an", "bn", "mul ntt", "mul k_mul");
		for (std::size_t ratio : {2, 3, 4, 6, 8, 12, 16}) {
			const std::size_t an = ratio * bn;
			std::vector<digit> a(an), b(bn), r(an + bn), s(16 * an + 1024 + U::k_ntt_scratch_size(an, bn, false) + U::k_mul_scratch_size(an, bn));
			for (auto& x : a) x = g();
			for (auto& x : b) x = g();
			a.back() |= 1; b.back() |= 1;
			std::printf("%8zu %8zu |", an, bn);
			std::printf(" %11.0f", time_ns([&] { U::k_ntt_mul(r.data(), a.data(), an, b.data(), bn, false, s.data()); }));
			std::printf(" %11.0f\n", time_ns([&] { U::k_mul(r.data(), a.data(), an, b.data(), bn, s.data()); }));
			std::fflush(stdout);
		}
		return 0;
	}
	if (argc > 2 && std::string(argv[1]) == "-t") {
		// Mid and large sizes: Toom-3 and Toom-4 against the transform, products and squares.
		std::printf("%8s | %11s %11s %11s | %11s %11s %11s\n", "limbs", "mul toom3", "mul toom4", "mul ntt", "sqr toom3", "sqr toom4", "sqr ntt");
		for (int i = 2; i < argc; ++i) {
			const std::size_t n = std::strtoul(argv[i], nullptr, 10);
			std::vector<digit> a(n), b(n), r(2 * n), s(16 * n + 1024 + U::k_ntt_scratch_size(n, n, false));
			for (auto& x : a) x = g();
			for (auto& x : b) x = g();
			a.back() |= 1; b.back() |= 1;
			const bool toom = !std::getenv("TUNE_NTT_ONLY");
			std::printf("%8zu |", n);
			std::printf(" %11.0f", toom ? time_ns([&] { U::k_mul_toom3(r.data(), a.data(), n, b.data(), n, s.data()); }) : 0.0);
			std::printf(" %11.0f", toom ? time_ns([&] { U::k_mul_toom4(r.data(), a.data(), n, b.data(), n, s.data()); }) : 0.0);
			std::printf(" %11.0f |", time_ns([&] { U::k_ntt_mul(r.data(), a.data(), n, b.data(), n, false, s.data()); }));
			std::printf(" %11.0f", toom ? time_ns([&] { U::k_sqr_toom3(r.data(), a.data(), n, s.data()); }) : 0.0);
			std::printf(" %11.0f", toom ? time_ns([&] { U::k_sqr_toom4(r.data(), a.data(), n, s.data()); }) : 0.0);
			std::printf(" %11.0f\n", time_ns([&] { U::k_ntt_mul(r.data(), a.data(), n, a.data(), n, true, s.data()); }));
			std::fflush(stdout);
		}
		return 0;
	}
	std::printf("cutoffs (limbs): mul karatsuba %zu toom3 %zu toom4 %zu, sqr karatsuba %zu toom3 %zu toom4 %zu\n",
		U::mul_karatsuba_threshold, U::mul_toom3_threshold, U::mul_toom4_threshold, U::sqr_karatsuba_threshold, U::sqr_toom3_threshold, U::sqr_toom4_threshold);
	std::printf("%6s | %9s %9s %9s %9s %9s | %9s %9s %9s %9s %9s\n", "limbs", "mul base", "mul kara", "mul toom3", "mul toom4", "mul k_mul",
		"sqr base", "sqr kara", "sqr toom3", "sqr toom4", "sqr k_sqr");
	for (std::size_t n = lo; n <= hi; n += step) {
		std::vector<digit> a(n), b(n), r(2 * n), s(16 * n + 1024 + U::k_ntt_scratch_size(n, n, false));
		for (auto& x : a) x = g();
		for (auto& x : b) x = g();
		a.back() |= 1; b.back() |= 1;
		const bool toom = n > 2 * ((n + 2) / 3);
		const bool toom4 = n > 3 * ((n + 3) / 4);
		std::printf("%6zu |", n);
		cell(time_ns([&] { U::k_mul_basecase(r.data(), a.data(), n, b.data(), n); }));
		cell(n >= 2 ? time_ns([&] { U::k_mul_karatsuba(r.data(), a.data(), n, b.data(), n, s.data()); }) : -1);
		cell(toom ? time_ns([&] { U::k_mul_toom3(r.data(), a.data(), n, b.data(), n, s.data()); }) : -1);
		cell(toom4 ? time_ns([&] { U::k_mul_toom4(r.data(), a.data(), n, b.data(), n, s.data()); }) : -1);
		cell(time_ns([&] { U::k_mul(r.data(), a.data(), n, b.data(), n, s.data()); }));
		std::printf(" |");
		cell(time_ns([&] { U::k_sqr_basecase(r.data(), a.data(), n); }));
		cell(n >= 2 ? time_ns([&] { U::k_sqr_karatsuba(r.data(), a.data(), n, s.data()); }) : -1);
		cell(toom ? time_ns([&] { U::k_sqr_toom3(r.data(), a.data(), n, s.data()); }) : -1);
		cell(toom4 ? time_ns([&] { U::k_sqr_toom4(r.data(), a.data(), n, s.data()); }) : -1);
		cell(time_ns([&] { U::k_sqr(r.data(), a.data(), n, s.data()); }));
		std::printf("\n");
		std::fflush(stdout);
	}
}

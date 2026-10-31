// Times the recursive division (k_div_dc below the Newton threshold) against the Newton division forced to
// the same shape, over divisor sizes, to find where one overtakes the other.
// Build: c++ -std=c++17 -O2 -I. bench/tune_div.cc -o tune_div   (threshold macros as for the tests; with
// -DUINTEGER_T_DIV_NEWTON_THRESHOLD=100000000 the first column is the recursive division at every size)
// Run: ./tune_div ratio dn...   times each divisor size dn with a quotient of ratio * dn limbs (ratio 1 is a
//      balanced division, 0.25 a short quotient, 8 a long one); columns are ns per division, "-" where Newton
//      does not apply (a divisor under two limbs or a quotient under two).
//      ./tune_div -b dn...  times the reciprocal alone (ns) for n-limb divisors.
#define UINT_T_PUBLIC_IMPLEMENTATION
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
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
			if (ns > 20e6) { best = std::min(best, ns / n); break; }
			n *= 2;
		}
	}
	return best;
}

static std::mt19937_64 rng(20261031);

static std::vector<digit> random_limbs(std::size_t n) {
	std::vector<digit> v(n);
	for (auto& x : v) x = static_cast<digit>(rng());
	return v;
}

int main(int argc, char** argv) {
	if (argc < 3) {
		std::printf("usage: tune_div ratio dn...   |   tune_div -b dn...\n");
		return 1;
	}
	if (std::string(argv[1]) == "-b") {
		for (int i = 2; i < argc; ++i) {
			const std::size_t n = static_cast<std::size_t>(std::atol(argv[i]));
			auto t = random_limbs(n);
			t[n - 1] |= digit(1) << (U::digit_bits - 1);
			std::vector<digit> w(n + 1), s(U::k_div_newton_inv_scratch_size(n));
			const double ns = time_ns([&] { U::k_div_newton_inv(w.data(), t.data(), n, s.data()); });
			std::printf("%-8zu %12.0f\n", n, ns);
		}
		return 0;
	}
	const double ratio = std::atof(argv[1]);
	std::printf("%-8s %-8s %12s %12s\n", "dn", "m", "dc", "newton");
	for (int i = 2; i < argc; ++i) {
		const std::size_t dn = static_cast<std::size_t>(std::atol(argv[i]));
		const std::size_t m = std::max<std::size_t>(1, static_cast<std::size_t>(ratio * static_cast<double>(dn)));
		const std::size_t nn = dn + m;
		auto d = random_limbs(dn);
		d[dn - 1] |= digit(1) << (U::digit_bits - 1);
		auto a = random_limbs(nn);
		a[nn - 1] = static_cast<digit>(rng() % d[dn - 1]);
		std::vector<digit> n_(nn), q(m);
		std::vector<digit> s_dc(U::k_div_dc_scratch_size(nn, dn));
		const double dc = time_ns([&] {
			std::copy(a.begin(), a.end(), n_.begin());
			U::k_div_dc(q.data(), n_.data(), nn, d.data(), dn, s_dc.data());
		});
		double nt = -1;
		if (dn >= 2 && m >= 2) {
			std::size_t need = 0;
			if (U::_div_newton_plan<std::size_t>(nn, dn, &need)) {
				std::vector<digit> s_nt(need);
				nt = time_ns([&] {
					std::copy(a.begin(), a.end(), n_.begin());
					U::k_div_newton(q.data(), n_.data(), nn, d.data(), dn, s_nt.data());
				});
			}
		}
		std::printf("%-8zu %-8zu %12.0f", dn, m, dc);
		nt >= 0 ? std::printf(" %12.0f\n", nt) : std::printf(" %12s\n", "-");
		std::fflush(stdout);
	}
	return 0;
}

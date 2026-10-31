// Compares uinteger_t with GMP and Boost.Multiprecision cpp_int (when found).
// Build: c++ -std=c++17 -O2 -I. bench/bench.cc [-DHAVE_GMP -lgmp] [-DHAVE_BOOST]
// Run: ./bench [limb counts...] [ops...], e.g. ./bench 64 256 1024 mul sqr
// (ops: add mul sqr div str10 parse10 str16; the default is every size and op).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include "uinteger_t.hh"
#ifdef HAVE_GMP
#include <gmp.h>
#endif
#ifdef HAVE_BOOST
#include <boost/multiprecision/cpp_int.hpp>
#endif

using Clock = std::chrono::steady_clock;

// Best of several rounds, in microseconds per call.
template <typename F>
static double bench(F&& f) {
	double best = 1e300;
	for (int round = 0; round < 5; ++round) {
		std::size_t n = 1;
		for (;;) {
			auto s = Clock::now();
			for (std::size_t i = 0; i < n; ++i) f();
			double us = std::chrono::duration<double, std::micro>(Clock::now() - s).count();
			if (us > 20000) { best = std::min(best, us / n); break; }
			n *= 2;
		}
	}
	return best;
}

static volatile std::size_t sink;

int main(int argc, char** argv) {
	std::mt19937_64 g(1);
	std::vector<int> sizes;
	std::vector<std::string> ops;
	for (int i = 1; i < argc; ++i) {
		if (argv[i][0] >= '0' && argv[i][0] <= '9') {
			sizes.push_back(std::atoi(argv[i]));
		} else {
			ops.push_back(argv[i]);
		}
	}
	if (sizes.empty()) sizes = {2, 4, 16, 64, 256, 1024};
	auto want = [&](const char* op) {
		return ops.empty() || std::find(ops.begin(), ops.end(), op) != ops.end();
	};
	std::printf("%-6s %-10s %10s %10s %10s\n", "limbs", "op", "uinteger_t", "gmp", "boost");
	for (int L : sizes) {
		std::vector<std::uint64_t> wa(L), wb(L), wd(L / 2 + 1);
		for (auto& w : wa) w = g();
		for (auto& w : wb) w = g();
		for (auto& w : wd) w = g();
		wa.back() |= 1; wb.back() |= 1; wd.back() |= 1;
		auto from = [](const std::vector<std::uint64_t>& w) {
			uinteger_t r;
			for (auto i = w.size(); i--;) { r <<= 64; r |= uinteger_t(w[i]); }
			return r;
		};
		uinteger_t a = from(wa), b = from(wb), d = from(wd);
		std::string dec = a.str(10), hex = a.str(16);
		auto row = [&](const char* op, double u, double gm, double bo) {
			std::printf("%-6d %-10s %10.3f", L, op, u);
			gm >= 0 ? std::printf(" %10.3f", gm) : std::printf(" %10s", "-");
			bo >= 0 ? std::printf(" %10.3f\n", bo) : std::printf(" %10s\n", "-");
		};
		double u_add = -1, u_mul = -1, u_sqr = -1, u_div = -1, u_s10 = -1, u_p10 = -1, u_s16 = -1;
		if (want("add")) u_add = bench([&] { sink = (a + b).size(); });
		if (want("mul")) u_mul = bench([&] { sink = (a * b).size(); });
		if (want("sqr")) u_sqr = bench([&] { sink = (a * a).size(); });
		if (want("div")) u_div = bench([&] { sink = (a / d).size(); });
		if (want("str10")) u_s10 = bench([&] { sink = a.str(10).size(); });
		if (want("parse10")) u_p10 = bench([&] { sink = uinteger_t(dec, 10).size(); });
		if (want("str16")) u_s16 = bench([&] { sink = a.str(16).size(); });
		double g_add = -1, g_mul = -1, g_sqr = -1, g_div = -1, g_s10 = -1, g_p10 = -1, g_s16 = -1;
		double b_add = -1, b_mul = -1, b_sqr = -1, b_div = -1, b_s10 = -1, b_p10 = -1, b_s16 = -1;
#ifdef HAVE_GMP
		{
			mpz_t za, zb, zd, zr, zq;
			mpz_inits(za, zb, zd, zr, zq, nullptr);
			mpz_import(za, L, -1, 8, 0, 0, wa.data());
			mpz_import(zb, L, -1, 8, 0, 0, wb.data());
			mpz_import(zd, wd.size(), -1, 8, 0, 0, wd.data());
			if (want("add")) g_add = bench([&] { mpz_add(zr, za, zb); sink = mpz_size(zr); });
			if (want("mul")) g_mul = bench([&] { mpz_mul(zr, za, zb); sink = mpz_size(zr); });
			if (want("sqr")) g_sqr = bench([&] { mpz_mul(zr, za, za); sink = mpz_size(zr); });
			if (want("div")) g_div = bench([&] { mpz_tdiv_q(zq, za, zd); sink = mpz_size(zq); });
			std::vector<char> buf(mpz_sizeinbase(za, 10) + 2);
			if (want("str10")) g_s10 = bench([&] { mpz_get_str(buf.data(), 10, za); sink = buf[0]; });
			if (want("str16")) g_s16 = bench([&] { mpz_get_str(buf.data(), 16, za); sink = buf[0]; });
			if (want("parse10")) g_p10 = bench([&] { mpz_set_str(zr, dec.c_str(), 10); sink = mpz_size(zr); });
			mpz_clears(za, zb, zd, zr, zq, nullptr);
		}
#endif
#ifdef HAVE_BOOST
		{
			namespace mp = boost::multiprecision;
			using I = mp::cpp_int;
			I ba, bb, bd;
			mp::import_bits(ba, wa.begin(), wa.end(), 64, false);
			mp::import_bits(bb, wb.begin(), wb.end(), 64, false);
			mp::import_bits(bd, wd.begin(), wd.end(), 64, false);
			if (want("add")) b_add = bench([&] { I r = ba + bb; sink = r.backend().size(); });
			if (want("mul")) b_mul = bench([&] { I r = ba * bb; sink = r.backend().size(); });
			if (want("sqr")) b_sqr = bench([&] { I r = ba * ba; sink = r.backend().size(); });
			if (want("div")) b_div = bench([&] { I r = ba / bd; sink = r.backend().size(); });
			if (want("str10")) b_s10 = bench([&] { sink = ba.str().size(); });
			if (want("str16")) b_s16 = bench([&] { std::stringstream s; s << std::hex << ba; sink = s.str().size(); });
			if (want("parse10")) b_p10 = bench([&] { I r(dec); sink = r.backend().size(); });
		}
#endif
		if (want("add")) row("add", u_add, g_add, b_add);
		if (want("mul")) row("mul", u_mul, g_mul, b_mul);
		if (want("sqr")) row("sqr", u_sqr, g_sqr, b_sqr);
		if (want("div")) row("div n/2", u_div, g_div, b_div);
		if (want("str10")) row("str10", u_s10, g_s10, b_s10);
		if (want("parse10")) row("parse10", u_p10, g_p10, b_p10);
		if (want("str16")) row("str16", u_s16, g_s16, b_s16);
	}
	// Unbalanced division: the quotient is several divisor-size blocks (an eighth of the dividend is
	// the divisor): 2048 / 256 limbs, and each requested size of at least 4096 limbs.
	std::vector<int> unbalanced = {2048};
	for (int L : sizes) if (L >= 4096) unbalanced.push_back(L);
	for (int NL : unbalanced) {
		if (!want("div")) break;
		const int DL = NL / 8;
		std::vector<std::uint64_t> wn(NL), wd(DL);
		for (auto& w : wn) w = g();
		for (auto& w : wd) w = g();
		wn.back() |= 1; wd.back() |= 1;
		auto from = [](const std::vector<std::uint64_t>& w) {
			uinteger_t r;
			for (auto i = w.size(); i--;) { r <<= 64; r |= uinteger_t(w[i]); }
			return r;
		};
		uinteger_t n = from(wn), d = from(wd);
		double u = bench([&] { sink = (n / d).size(); });
		double gm = -1, bo = -1;
#ifdef HAVE_GMP
		{
			mpz_t zn, zd, zq;
			mpz_inits(zn, zd, zq, nullptr);
			mpz_import(zn, NL, -1, 8, 0, 0, wn.data());
			mpz_import(zd, DL, -1, 8, 0, 0, wd.data());
			gm = bench([&] { mpz_tdiv_q(zq, zn, zd); sink = mpz_size(zq); });
			mpz_clears(zn, zd, zq, nullptr);
		}
#endif
#ifdef HAVE_BOOST
		{
			namespace mp = boost::multiprecision;
			using I = mp::cpp_int;
			I bn, bd;
			mp::import_bits(bn, wn.begin(), wn.end(), 64, false);
			mp::import_bits(bd, wd.begin(), wd.end(), 64, false);
			bo = bench([&] { I r = bn / bd; sink = r.backend().size(); });
		}
#endif
		std::printf("%-6d %-10s %10.3f", NL, "div n/8", u);
		gm >= 0 ? std::printf(" %10.3f", gm) : std::printf(" %10s", "-");
		bo >= 0 ? std::printf(" %10.3f\n", bo) : std::printf(" %10s\n", "-");
	}
}

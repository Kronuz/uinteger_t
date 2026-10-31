// Division microbenchmark: where the time of `a / d` goes (n/2 shape: L-limb dividend, L/2+1-limb divisor).
// Build: c++ -std=c++17 -O2 -I. bench/divmb.cc -DHAVE_GMP -lgmp -o divmb
// Run: ./divmb [limbs...]
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
			if (us > 30000) { best = std::min(best, us / n); break; }
			n *= 2;
		}
	}
	return best * 1000.0; // ns
}

static volatile std::size_t sink;
static volatile digit dsink;

int main(int argc, char** argv) {
	std::mt19937_64 g(7);
	std::vector<int> sizes;
	for (int i = 1; i < argc; ++i) sizes.push_back(std::atoi(argv[i]));
	if (sizes.empty()) sizes = {4, 8, 16, 32, 64, 128, 256};
	constexpr int K = 32; // rotating inputs
	std::printf("%-5s %-34s %10s %10s\n", "L", "what", "ours ns", "gmp ns");
	for (int L : sizes) {
		const std::size_t nn = L, dn = L / 2 + 1;
		std::vector<std::vector<digit>> A(K, std::vector<digit>(nn)), D(K, std::vector<digit>(dn));
		for (int k = 0; k < K; ++k) {
			for (auto& w : A[k]) w = g();
			for (auto& w : D[k]) w = g();
			A[k].back() |= 1;
			D[k].back() |= 1;
		}
		std::vector<U> UA(K), UD(K);
		for (int k = 0; k < K; ++k) {
			for (std::size_t i = nn; i--;) { UA[k] <<= 64; UA[k] |= U(A[k][i]); }
			for (std::size_t i = dn; i--;) { UD[k] <<= 64; UD[k] |= U(D[k][i]); }
		}
		// 1. the library call
		double lib = bench([&](std::size_t i) { sink = (UA[i % K] / UD[i % K]).size(); });
		std::vector<__mpz_struct> ZA(K), ZD(K);
		for (int k = 0; k < K; ++k) {
			mpz_init(&ZA[k]); mpz_init(&ZD[k]);
			mpz_import(&ZA[k], nn, -1, 8, 0, 0, A[k].data());
			mpz_import(&ZD[k], dn, -1, 8, 0, 0, D[k].data());
		}
		mpz_t zq;
		mpz_init(zq);
		double gl = bench([&](std::size_t i) { mpz_tdiv_q(zq, &ZA[i % K], &ZD[i % K]); sink = mpz_size(zq); });
		std::printf("%-5d %-34s %10.1f %10.1f\n", L, "a / d (library vs mpz_tdiv_q)", lib, gl);

		// 2. raw kernels on preallocated buffers: normalized operands, k_div_dc vs mpn_tdiv_qr
		std::vector<std::vector<digit>> N(K, std::vector<digit>(nn + 1)), Dn(K, std::vector<digit>(dn));
		for (int k = 0; k < K; ++k) {
			const unsigned sh = static_cast<unsigned>(U::digit_bits - U::_bits(D[k].back()));
			if (sh) {
				U::k_lshift(Dn[k].data(), D[k].data(), dn, sh);
				N[k][nn] = U::k_lshift(N[k].data(), A[k].data(), nn, sh);
			} else {
				std::copy(D[k].begin(), D[k].end(), Dn[k].begin());
				std::copy(A[k].begin(), A[k].end(), N[k].begin());
				N[k][nn] = 0;
			}
		}
		const std::size_t qn = nn + 1 - dn;
		const std::size_t sn = U::k_div_dc_scratch_size(nn + 1, dn);
		std::vector<digit> work(nn + 1), q(qn + 1), s(sn + 1);
		double kern = bench([&](std::size_t i) {
			std::copy(N[i % K].begin(), N[i % K].end(), work.begin());
			U::k_div_dc(q.data(), work.data(), nn + 1, Dn[i % K].data(), dn, s.data());
			dsink = q[0];
		});
		double kcopy = bench([&](std::size_t i) {
			std::copy(N[i % K].begin(), N[i % K].end(), work.begin());
			dsink = work[0];
		});
		std::vector<mp_limb_t> gq(nn + 1), gr(dn + 1), gwork(nn + 1);
		double gk = bench([&](std::size_t i) {
			mpn_tdiv_qr(gq.data(), gr.data(), 0, reinterpret_cast<const mp_limb_t*>(A[i % K].data()), nn, reinterpret_cast<const mp_limb_t*>(D[i % K].data()), dn);
			dsink = gq[0];
		});
		std::printf("%-5d %-34s %10.1f %10.1f\n", L, "kernel k_div_dc vs mpn_tdiv_qr", kern - kcopy, gk);

		// 3. schoolbook only
		double sb = bench([&](std::size_t i) {
			std::copy(N[i % K].begin(), N[i % K].end(), work.begin());
			U::k_div_sb(q.data(), work.data(), nn + 1, Dn[i % K].data(), dn);
			dsink = q[0];
		});
		std::printf("%-5d %-34s %10.1f %10s\n", L, "kernel k_div_sb (all sizes)", sb - kcopy, "-");

		// 4. submul_1 over dn limbs
		std::vector<digit> r(dn + 1);
		std::vector<mp_limb_t> gr2(dn + 1);
		double sm = bench([&](std::size_t i) {
			dsink = U::k_submul_1(r.data(), Dn[i % K].data(), dn, N[(i + 1) % K][3]);
		});
		double gsm = bench([&](std::size_t i) {
			dsink = mpn_submul_1(gr2.data(), reinterpret_cast<const mp_limb_t*>(Dn[i % K].data()), dn, N[(i + 1) % K][3]);
		});
		std::printf("%-5d %-34s %10.1f %10.1f   (%.3f / %.3f ns per limb)\n", L, "submul_1 over dn limbs", sm, gsm, sm / dn, gsm / dn);

		// 5. 3by2 step, dependent chain: the remainder feeds the next dividend
		const digit d1 = Dn[0][dn - 1], d0 = Dn[0][dn - 2];
		const digit v = U::k_inv_3by2(d1, d0);
		double t32 = bench([&](std::size_t i) {
			digit n2 = d1 >> 1, n1 = static_cast<digit>(i * 0x9E3779B97F4A7C15ull), n0 = static_cast<digit>(i);
			digit r1, r0;
			for (int j = 0; j < 8; ++j) {
				digit qd = U::k_div_3by2(n2, n1, n0, d1, d0, v, &r1, &r0);
				n2 = r1; n1 = r0 ^ qd; n0 = qd;
			}
			dsink = n2;
		});
		std::printf("%-5d %-34s %10.1f\n", L, "k_div_3by2 x8 chained", t32);
		mpz_clear(zq);
		for (int k = 0; k < K; ++k) { mpz_clear(&ZA[k]); mpz_clear(&ZD[k]); }
	}
}

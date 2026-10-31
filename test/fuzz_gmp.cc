// Differential fuzz of uinteger_t against GMP.
// Build: c++ -std=c++17 -O2 -I. test/fuzz_gmp.cc -lgmp -o fuzz_gmp && ./fuzz_gmp [iterations] [seed]
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
#include <gmp.h>
#include "uinteger_t.hh"

static std::mt19937_64 g;

static std::vector<std::uint64_t> random_limbs(std::size_t n) {
	std::vector<std::uint64_t> w(n);
	int style = static_cast<int>(g() % 6);
	for (auto& x : w) {
		switch (style) {
			case 0: x = g(); break;
			case 1: x = ~0ull; break;
			case 2: x = g() % 3 ? 0 : g(); break;
			case 3: x = g() >> (g() % 64); break;
			case 4: x = (g() & 1) ? ~0ull : 0; break;
			default: x = g(); break;
		}
	}
	if (n && !w.back()) w.back() = 1 + g() % 5;
	return w;
}

static uinteger_t to_u(const std::vector<std::uint64_t>& w) {
	uinteger_t r;
	for (auto i = w.size(); i--;) { r <<= 64; r |= uinteger_t(w[i]); }
	return r;
}

static void to_z(mpz_t z, const std::vector<std::uint64_t>& w) {
	mpz_import(z, w.size(), -1, 8, 0, 0, w.data());
}

static std::string z_str(const mpz_t z, int base) {
	if (!mpz_sgn(z)) return "0";
	std::string s(mpz_sizeinbase(z, base) + 2, '\0');
	mpz_get_str(&s[0], base, z);
	s.resize(std::char_traits<char>::length(s.c_str()));
	return s;
}

static std::size_t pick_size() {
	switch (g() % 5) {
		case 0: return 1 + g() % 4;
		case 1: return 1 + g() % 40;
		case 2: return 1 + g() % 130;
		case 3: return 1 + g() % 400;
		default: return 1 + g() % 1100;
	}
}

#define CHECK(cond, what) do { if (!(cond)) { std::printf("FAIL %s (iter %d, na=%zu nb=%zu)\n", what, iter, na, nb); return 1; } } while (0)

int main(int argc, char** argv) {
	int iters = argc > 1 ? std::atoi(argv[1]) : 2000;
	g.seed(argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 12345);
	mpz_t za, zb, zr, zq, zs;
	mpz_inits(za, zb, zr, zq, zs, nullptr);
	for (int iter = 0; iter < iters; ++iter) {
		std::size_t na = pick_size(), nb = pick_size();
		if (g() % 4 == 0) nb = 1 + g() % 3;
		auto wa = random_limbs(na), wb = random_limbs(nb);
		to_z(za, wa); to_z(zb, wb);
		uinteger_t a = to_u(wa), b = to_u(wb);

		mpz_add(zr, za, zb);
		CHECK((a + b).str(16) == z_str(zr, 16), "add");
		mpz_mul(zr, za, zb);
		CHECK((a * b).str(16) == z_str(zr, 16), "mul");
		if (mpz_cmp(za, zb) >= 0) {
			mpz_sub(zr, za, zb);
			CHECK((a - b).str(16) == z_str(zr, 16), "sub");
		}
		mpz_tdiv_qr(zq, zr, za, zb);
		auto qr = a.divmod(b);
		CHECK(qr.first.str(16) == z_str(zq, 16), "div quotient");
		CHECK(qr.second.str(16) == z_str(zr, 16), "div remainder");

		unsigned sh = static_cast<unsigned>(g() % 300);
		mpz_mul_2exp(zr, za, sh);
		CHECK((a << sh).str(16) == z_str(zr, 16), "lshift");
		mpz_tdiv_q_2exp(zr, za, sh);
		CHECK((a >> sh).str(16) == z_str(zr, 16), "rshift");

		int base = 2 + static_cast<int>(g() % 35);
		auto s = a.str(base);
		CHECK(s == z_str(za, base), "str");
		CHECK(uinteger_t(s, base) == a, "parse");
		mpz_set_str(zs, s.c_str(), base);
		CHECK(mpz_cmp(zs, za) == 0, "gmp parse");
		CHECK((a < b) == (mpz_cmp(za, zb) < 0), "compare");
	}
	std::printf("fuzz_gmp OK (%d iterations)\n", iters);
	return 0;
}

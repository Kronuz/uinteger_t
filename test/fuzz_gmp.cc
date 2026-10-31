// Differential fuzz of uinteger_t against GMP.
// Build: c++ -std=c++17 -O2 -I. test/fuzz_gmp.cc -lgmp -o fuzz_gmp && ./fuzz_gmp [iterations] [seed] [large rounds] [mid rounds] [large division rounds]
// (GMP from Homebrew: -I/opt/homebrew/include -L/opt/homebrew/lib.) The mid rounds multiply and square operands
// of 100 to 5000 limbs, the sizes of Toom-3 and Toom-4 (default 600; 0 skips them). The large rounds do the same
// for 1K to 1M limbs, the sizes where the transform takes over (default 6 rounds; 0 skips them). The large
// division rounds (default 8; 0 skips them) divide by divisors of 1K to 500K limbs with quotients shorter than,
// equal to and several times longer than the divisor, where Newton division runs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
#include <gmp.h>
// The large rounds read the transform cutoff, which is private.
#define UINTEGER_T_PUBLIC_IMPLEMENTATION
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

// Big-endian bytes of `limbs` 64-bit limbs whose top byte is not zero (the base-256 parser keeps a leading
// zero limb, so the value would not be trimmed), in one of several patterns.
static std::string random_bytes(std::size_t limbs) {
	std::string s(limbs * 8, '\0');
	const int style = static_cast<int>(g() % 5);
	std::size_t run = 0;
	char value = 0;
	for (auto& c : s) {
		switch (style) {
			case 0: c = static_cast<char>(g()); break;
			case 1: c = static_cast<char>(0xff); break;
			case 2: c = g() % 3 ? 0 : static_cast<char>(g()); break;
			case 3:
				if (!run) { run = 1 + g() % 40; value = (g() & 1) ? static_cast<char>(0xff) : 0; }
				c = value;
				--run;
				break;
			default: c = static_cast<char>(g() >> (g() % 8)); break;
		}
	}
	if (!s.empty() && !s[0]) s[0] = static_cast<char>(1 + g() % 255);
	return s;
}

static bool check_large(std::size_t na, std::size_t nb, bool square) {
	std::string ba = random_bytes(na), bb = square ? ba : random_bytes(nb);
	mpz_t za, zb, zr;
	mpz_inits(za, zb, zr, nullptr);
	mpz_import(za, ba.size(), 1, 1, 0, 0, ba.data());
	mpz_import(zb, bb.size(), 1, 1, 0, 0, bb.data());
	mpz_mul(zr, za, zb);
	size_t count = 0;
	void* raw = mpz_export(nullptr, &count, 1, 1, 0, 0, zr);
	std::string want(static_cast<char*>(raw), count);
	void (*freefn)(void*, size_t);
	mp_get_memory_functions(nullptr, nullptr, &freefn);
	freefn(raw, count);
	mpz_clears(za, zb, zr, nullptr);
	uinteger_t a(ba, 256);
	uinteger_t b(bb, 256);
	std::string got = square ? (a * a).str(256) : (a * b).str(256);
	if (got != want) {
		std::printf("FAIL large %s %zu x %zu limbs\n", square ? "square" : "mul", na, square ? na : nb);
		return false;
	}
	if (square) {
		// the same value through two objects takes the general multiply
		uinteger_t c(ba, 256);
		if ((a * c).str(256) != want) {
			std::printf("FAIL large square as product of copies %zu limbs\n", na);
			return false;
		}
	}
	return true;
}

// Sizes from 100 to 5000 limbs, log-uniform, with the shapes of the Toom-3 and Toom-4 dispatch: balanced, the
// fourth-piece boundary (bn = 3 ceil(an / 4) + 0, 1, 2), the third-piece boundary, anywhere from two thirds up,
// and the neighbourhood of each cutoff.
static int mid_rounds(int rounds) {
	const std::size_t cut[] = {uinteger_t::mul_karatsuba_threshold, uinteger_t::mul_toom3_threshold, uinteger_t::mul_toom4_threshold, uinteger_t::sqr_toom3_threshold};
	for (int r = 0; r < rounds; ++r) {
		const double lo = std::log(100.0), hi = std::log(5000.0);
		std::size_t n = static_cast<std::size_t>(std::exp(lo + (hi - lo) * (g() % 1000000) / 1e6));
		if (r % 5 == 4) {
			n = cut[g() % (sizeof(cut) / sizeof(cut[0]))] * (1 + g() % 3) + g() % 5;
			if (n > 3 && (g() & 1)) n -= 3;
		}
		std::size_t m = n;
		switch (r % 6) {
			case 1: m = 3 * ((n + 3) / 4) + g() % 3; break;
			case 2: m = 2 * ((n + 2) / 3) + g() % 3; break;
			case 3: m = 2 * n / 3 + g() % (n / 3 + 1); break;
			case 4: m = n - g() % 4; break;
			default: break;
		}
		m = std::min(std::max<std::size_t>(m, 1), n);
		if (!check_large(n, m, false)) return 1;
		if (!check_large(n, n, true)) return 1;
	}
	std::printf("mid rounds (%d, 100 to 5000 limbs) OK\n", rounds);
	return 0;
}

// Sizes from 1K to 1M limbs, log-uniform, with the shapes of the transform dispatch: balanced, within a
// factor of two, and the cutoff's neighbourhood.
static int large_rounds(int rounds) {
	for (int r = 0; r < rounds; ++r) {
		const double lo = std::log(1000.0), hi = std::log(1000000.0);
		std::size_t n = static_cast<std::size_t>(std::exp(lo + (hi - lo) * (g() % 1000000) / 1e6));
		std::size_t m = (r % 3 == 0) ? n : n / 2 + 1 + g() % (n / 2);
		if (r % 4 == 1) n = uinteger_t::mul_ntt_threshold + g() % 3 - 1, m = n - g() % 2;
		if (!check_large(n, m, false)) return 1;
		if (!check_large(n, n, true)) return 1;
		std::printf("large round %d: %zu x %zu limbs OK\n", r, n, m);
		std::fflush(stdout);
	}
	return 0;
}

#define CHECK(cond, what) do { if (!(cond)) { std::printf("FAIL %s (iter %d, na=%zu nb=%zu)\n", what, iter, na, nb); return 1; } } while (0)

// Divisions above the Newton thresholds against mpz_tdiv_qr: divisors of 1K to 500K limbs (random, the least
// and greatest of their size, a power of two, one above the least) with a quotient from half the divisor's
// length to many times it.
static int large_div_rounds(int rounds) {
	mpz_t za, zb, zq, zr;
	mpz_inits(za, zb, zq, zr, nullptr);
	for (int r = 0; r < rounds; ++r) {
		std::size_t dn, m;
		switch (r % 8) {
			case 0: dn = 1400 + g() % 2000; m = 5000 + g() % 3000; break;
			case 1: dn = 20000 + g() % 5000; m = dn; break;
			case 2: dn = 100000 + g() % 20000; m = dn / 2 + g() % (dn / 2); break;
			case 3: dn = 3000 + g() % 3000; m = 600000 + g() % 300000; break;
			case 4: dn = 500000; m = 500000; break;
			case 5: dn = 1400 + g() % 100; m = 5000 + g() % 50; break;
			case 6: dn = 40000 + g() % 9000; m = 2 * dn + 1 + g() % dn; break;
			default: dn = 8000 + g() % 4000; m = 40000 + g() % 20000; break;
		}
		auto wd = random_limbs(dn), wn = random_limbs(dn + m);
		const int kind = static_cast<int>(g() % 5);
		if (kind == 1) { std::fill(wd.begin(), wd.end(), 0); wd.back() = 1ull << 63; }
		else if (kind == 2) { std::fill(wd.begin(), wd.end(), ~0ull); }
		else if (kind == 3) { std::fill(wd.begin(), wd.end(), 0); wd.back() = 1ull << 63; wd.front() = 1; }
		else if (kind == 4) { std::fill(wd.begin(), wd.end(), 0); wd.back() = 1; }
		to_z(za, wn); to_z(zb, wd);
		mpz_tdiv_qr(zq, zr, za, zb);
		const auto qr = to_u(wn).divmod(to_u(wd));
		if (qr.first.str(16) != z_str(zq, 16) || qr.second.str(16) != z_str(zr, 16)) {
			std::printf("FAIL large div round %d: %zu limbs by %zu (kind %d)\n", r, dn + m, dn, kind);
			return 1;
		}
		std::printf("large div round %d: %zu by %zu limbs OK\n", r, dn + m, dn);
	}
	mpz_clears(za, zb, zq, zr, nullptr);
	return 0;
}

int main(int argc, char** argv) {
	int iters = argc > 1 ? std::atoi(argv[1]) : 2000;
	g.seed(argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 12345);
	mpz_t za, zb, zr, zq, zs;
	mpz_inits(za, zb, zr, zq, zs, nullptr);
	for (int iter = 0; iter < iters; ++iter) {
		std::size_t na = pick_size(), nb = pick_size();
		if (g() % 4 == 0) nb = 1 + g() % 3;
		else if (g() % 3 == 0) nb = na - g() % (na / 4 + 1);  // near balanced: the Toom-3 shapes
		auto wa = random_limbs(na), wb = random_limbs(nb);
		to_z(za, wa); to_z(zb, wb);
		uinteger_t a = to_u(wa), b = to_u(wb);

		mpz_add(zr, za, zb);
		CHECK((a + b).str(16) == z_str(zr, 16), "add");
		mpz_mul(zr, za, zb);
		CHECK((a * b).str(16) == z_str(zr, 16), "mul");
		mpz_mul(zr, za, za);
		CHECK((a * a).str(16) == z_str(zr, 16), "square");
		{
			uinteger_t t = a;
			t *= t;
			CHECK(t.str(16) == z_str(zr, 16), "square in place");
		}
		if (mpz_cmp(za, zb) >= 0) {
			mpz_sub(zr, za, zb);
			CHECK((a - b).str(16) == z_str(zr, 16), "sub");
		}
		mpz_tdiv_qr(zq, zr, za, zb);
		auto qr = a.divmod(b);
		CHECK(qr.first.str(16) == z_str(zq, 16), "div quotient");
		CHECK(qr.second.str(16) == z_str(zr, 16), "div remainder");

		// A division shaped for the recursive path: divisor and quotient lengths around and
		// above the threshold, the quotient shorter, equal and many times longer than the divisor.
		{
			const std::size_t t = DIV_DC_THRESHOLD;
			std::size_t dn = g() % 3 ? 2 + g() % (6 * t) : t - 1 + g() % 3;
			std::size_t m = 1 + g() % (g() % 2 ? dn : 4 * dn);
			if (g() % 4 == 0) m = t - 1 + g() % 3;
			auto wd = random_limbs(dn), wn = random_limbs(dn + m);
			to_z(za, wn); to_z(zb, wd);
			mpz_tdiv_qr(zq, zr, za, zb);
			auto sh = to_u(wn).divmod(to_u(wd));
			na = dn + m; nb = dn;
			CHECK(sh.first.str(16) == z_str(zq, 16), "dc div quotient");
			CHECK(sh.second.str(16) == z_str(zr, 16), "dc div remainder");
			to_z(za, wa); to_z(zb, wb);
			na = wa.size(); nb = wb.size();
		}

		// Quotient-only division (`/`, `/=`) against mpz_tdiv_q, on dividends built as Q D + T with
		// a remainder T of 0, 1, D - 1, D - a few limbs or random, a divisor whose low limbs are
		// ones, and quotients from one limb up past the cutoffs of the truncated schoolbook kernel.
		{
			const std::size_t dn = g() % 3 ? 2 + g() % 40 : 2 + g() % 1500;
			std::size_t m = 1 + g() % (g() % 3 ? dn + 2 : 3 * dn);
			if (g() % 5 == 0) m = 1 + g() % 1100;
			auto wd = random_limbs(dn);
			if (g() % 3 == 0) std::fill(wd.begin(), wd.end() - 1, ~0ull);
			if (g() % 4 == 0) wd.back() |= 1ull << 63;
			auto wq = random_limbs(m);
			mpz_t zd, zqq, zt, zn;
			mpz_inits(zd, zqq, zt, zn, nullptr);
			to_z(zd, wd);
			to_z(zqq, wq);
			switch (g() % 6) {
				case 0: mpz_set_ui(zt, 0); break;
				case 1: mpz_set_ui(zt, 1); break;
				case 2: mpz_sub_ui(zt, zd, 1); break;
				case 3: {
					mpz_set_ui(zt, 1 + g() % 1000);
					mpz_mul_2exp(zt, zt, 64 * (g() % dn));
					if (mpz_cmp(zt, zd) >= 0) mpz_sub_ui(zt, zd, 1); else mpz_sub(zt, zd, zt);
					break;
				}
				default: {
					auto wt = random_limbs(dn);
					to_z(zt, wt);
					mpz_mod(zt, zt, zd);
					break;
				}
			}
			mpz_mul(zn, zqq, zd);
			mpz_add(zn, zn, zt);
			mpz_tdiv_q(zq, zn, zd);
			std::vector<std::uint64_t> wn(mpz_size(zn));
			std::size_t cnt = 0;
			mpz_export(wn.data(), &cnt, -1, 8, 0, 0, zn);
			wn.resize(cnt);
			const uinteger_t un = to_u(wn), ud = to_u(wd);
			CHECK((un / ud).str(16) == z_str(zq, 16), "quotient-only division");
			uinteger_t qq = un;
			qq /= ud;
			CHECK(qq.str(16) == z_str(zq, 16), "quotient-only division in place");
			mpz_clears(zd, zqq, zt, zn, nullptr);
		}

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

		// Large conversions, up to 64K limbs: the digit tree against mpz_get_str and mpz_set_str.
		if (iter % 25 == 0) {
			std::size_t nl = (static_cast<std::size_t>(1) << (7 + g() % 10)) + g() % 200;
			nl = std::min<std::size_t>(nl, 65536);
			na = nl; nb = 0;
			auto wl = random_limbs(nl);
			std::string bytes;
			bytes.reserve(nl * 8);
			for (auto i = nl; i--;) for (int k = 56; k >= 0; k -= 8) bytes.push_back(static_cast<char>(wl[i] >> k));
			uinteger_t big(bytes, 256);
			to_z(za, wl);
			int bb;
			do { bb = 3 + static_cast<int>(g() % 60); } while ((bb & (bb - 1)) == 0);
			std::vector<unsigned char> ds;
			big.digits(ds, bb);
			const std::string zs_str = z_str(za, bb);
			bool same = ds.size() == zs_str.size();
			for (std::size_t i = 0; same && i < ds.size(); ++i) {
				const char c = zs_str[i];
				const int dv = c <= '9' ? c - '0' : (bb <= 36 ? (c >= 'a' ? c - 'a' + 10 : c - 'A' + 10) : (c <= 'Z' ? c - 'A' + 10 : c - 'a' + 36));
				same = ds[i] == dv;
			}
			CHECK(same, "large digits");
			CHECK(uinteger_t::from_digits(ds.data(), ds.size(), bb) == big, "large from_digits");
			mpz_set_str(zs, zs_str.c_str(), bb);
			CHECK(mpz_cmp(zs, za) == 0, "large gmp parse");
			if (bb <= 36) {
				CHECK(uinteger_t(zs_str, bb) == big, "large parse");
			}
		}
	}
	if (mid_rounds(argc > 4 ? std::atoi(argv[4]) : 600)) return 1;
	if (large_rounds(argc > 3 ? std::atoi(argv[3]) : 6)) return 1;
	if (large_div_rounds(argc > 5 ? std::atoi(argv[5]) : 8)) return 1;
	std::printf("fuzz_gmp OK (%d iterations)\n", iters);
	return 0;
}

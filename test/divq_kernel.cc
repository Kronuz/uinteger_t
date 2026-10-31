// The quotient-only schoolbook division k_div_sb_trunc on raw limbs and through knuth_divmod,
// against the exact k_div_sb and against Q * D + R == N. The kernel skips part of every digit's
// product and flags the digits it cannot vouch for (1: the window's top limbs reach the divisor's,
// 2: the remainder's top two limbs are zero); the cases here are built to force those digits:
// an exact multiple (remainder 0), a remainder just below the divisor (D - s for s up to
// B^(dn-3)) with a divisor whose low limbs are all ones, a remainder of D - 1 in the middle
// of the division, and the saturated estimate (N = B^qn D - 1, top limbs equal at every digit).
// It includes the header with `private` opened up.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#define private public
#include "uinteger_t.hh"
#undef private

using U = uinteger_t;
using digit = U::digit;

static std::mt19937_64 rng(20261031);
static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); if (++failures > 10) std::exit(1); } } while (0)

static constexpr digit ones = static_cast<digit>(~static_cast<digit>(0));
static constexpr digit top_bit = static_cast<digit>(digit(1) << (U::digit_bits - 1));

static U from_limbs(const std::vector<digit>& v) {
	U x;
	x._value.assign(v.data(), v.data() + v.size());
	x.trim();
	return x;
}

static std::vector<digit> to_limbs(const U& x, std::size_t n) {
	std::vector<digit> v(n, 0);
	assert(x.size() <= n);
	std::copy(x.data(), x.data() + x.size(), v.begin());
	return v;
}

static digit random_limb(int style) {
	switch (style) {
		case 0: return static_cast<digit>(rng());
		case 1: return ones;
		case 2: return rng() % 4 ? 0 : static_cast<digit>(rng());
		case 3: return (rng() & 1) ? ones : 0;
		case 4: return static_cast<digit>(ones - rng() % 3);
		default: return static_cast<digit>(rng() >> (rng() % 64));
	}
}

static std::vector<digit> random_limbs(std::size_t n, int style) {
	std::vector<digit> v(n);
	for (auto& x : v) {
		x = random_limb(style);
	}
	return v;
}

// A normalized divisor of dn limbs.
static std::vector<digit> make_divisor(std::size_t dn, int kind) {
	std::vector<digit> d;
	switch (kind) {
		case 0: d = random_limbs(dn, 0); break;
		case 1: d.assign(dn, ones); break;
		case 2: d = random_limbs(dn, 0); std::fill(d.begin(), d.end() - 2, ones); break;  // low limbs all ones
		case 3: d = random_limbs(dn, 0); std::fill(d.begin(), d.end() - 2, digit(0)); break;  // low limbs zero
		case 4: d = random_limbs(dn, 0); d[dn - 1] = top_bit; d[dn - 2] = 0; break;  // the smallest top limbs
		case 5: d.assign(dn, ones); d[dn - 1] = top_bit; d[dn - 2] = 0; break;
		default: d = random_limbs(dn, 5); break;
	}
	d[dn - 1] |= top_bit;
	return d;
}

struct Stats {
	std::size_t cases = 0;
	std::size_t status[3] = {0, 0, 0};
	std::size_t zero_flagged = 0;  // T = 0 cases with a truncated digit that were flagged
	std::size_t zero_cases = 0;
};
static Stats stats;

// Divides n (qn + dn limbs, top dn limbs below d) by d three ways and compares. `exact_multiple` marks
// a remainder of zero with truncated digits present, which must flag.
static void check(const std::vector<digit>& d, const std::vector<digit>& n, const char* what, bool exact_multiple = false) {
	const std::size_t dn = d.size();
	const std::size_t nn = n.size();
	const std::size_t qn = nn - dn;
	assert(qn >= 1 && dn >= 2);

	std::vector<digit> ne(n), qe(qn);
	U::k_div_sb(qe.data(), ne.data(), nn, d.data(), dn);
	// The exact result against Q * D + R == N, independent of the kernel under test.
	const U D = from_limbs(d);
	const U N = from_limbs(n);
	const U Q = from_limbs(qe);
	const U R = from_limbs(std::vector<digit>(ne.begin(), ne.begin() + dn));
	CHECK(Q * D + R == N && R < D, "%s: exact kernel breaks Q D + R = N (dn=%zu qn=%zu)", what, dn, qn);

	std::vector<digit> nt(n), qt(qn);
	const unsigned st = U::k_div_sb_trunc(qt.data(), nt.data(), nn, d.data(), dn);
	++stats.cases;
	++stats.status[st];
	CHECK(st <= 2, "%s: status %u", what, st);
	if (st == 0) {
		CHECK(qt == qe, "%s: unflagged quotient differs from the exact one (dn=%zu qn=%zu)", what, dn, qn);
	}
	if (exact_multiple && dn >= 2 + U::div_trunc_window + 1) {
		++stats.zero_cases;
		stats.zero_flagged += st != 0;
		CHECK(st != 0, "%s: an exact multiple with truncated digits was not flagged (dn=%zu qn=%zu)", what, dn, qn);
	}

	// Through the library: the quotient-only path (a flagged run redone), against the quotient
	// that comes with the remainder.
	{
		U q;
		U::divide(&q, nullptr, N, D);
		CHECK(q == Q, "%s: quotient-only division differs (dn=%zu qn=%zu status %u)", what, dn, qn, st);
		U qq, rr;
		U::divide(&qq, &rr, N, D);
		CHECK(qq == Q && rr == R, "%s: division with remainder differs", what);
		U a = N;
		a /= D;
		CHECK(a == Q, "%s: a /= d differs", what);
	}
}

// N = Q D + T, with the quotient limbs q (qn of them) and a remainder T < D.
static std::vector<digit> make_dividend(const std::vector<digit>& d, const std::vector<digit>& q, const U& T) {
	const U D = from_limbs(d);
	const U N = from_limbs(q) * D + T;
	return to_limbs(N, d.size() + q.size());
}

static U small_u(std::uint64_t v) {
	U x;
	std::vector<digit> limbs;
	for (std::size_t i = 0; i * U::digit_bits < 64 && v; ++i) {
		limbs.push_back(static_cast<digit>(v));
		v = U::digit_bits < 64 ? v >> U::digit_bits : 0;
	}
	return from_limbs(limbs);
}

static void run_shape(std::size_t dn, std::size_t qn) {
	for (int dk = 0; dk < 7; ++dk) {
		const auto d = make_divisor(dn, dk);
		const U D = from_limbs(d);
		for (int qs : {0, 1, 4, 3, 2}) {
			auto q = random_limbs(qn, qs);
			if (qs != 1 && rng() % 4 == 0) {
				q.back() = 0;
			}
			// A remainder of zero: an exact multiple.
			check(d, make_dividend(d, q, U()), "T = 0", true);
			// A remainder of one, and a small one.
			check(d, make_dividend(d, q, small_u(1)), "T = 1");
			check(d, make_dividend(d, q, small_u(rng() % 1000)), "T small");
			// A remainder just below the divisor: D - s, s from 1 up to a power of the base.
			check(d, make_dividend(d, q, D - small_u(1)), "T = D - 1");
			for (std::size_t j = 0; j + 2 < dn; ++j) {
				// s below B^j (every size of the failing window), and j limbs of ones.
				std::vector<digit> sv = random_limbs(j + 1, 0);
				U s = from_limbs(sv);
				if (!s) {
					s = small_u(1);
				}
				check(d, make_dividend(d, q, D - s), "T = D - s");
				check(d, make_dividend(d, q, D - (small_u(1) << (j * U::digit_bits))), "T = D - B^j");
			}
			// A random remainder.
			U r = from_limbs(random_limbs(dn, 0));
			r = r % D;
			check(d, make_dividend(d, q, r), "T random");
		}
		// The saturated estimate: N = B^qn D - 1, every window's top limbs equal the divisor's.
		check(d, to_limbs((D << (qn * U::digit_bits)) - small_u(1), dn + qn), "N = B^qn D - 1");
		// A remainder of D - 1 (or D - small) in the middle: floor(N / B^s) = Q' D + D - 1.
		for (int rep = 0; rep < 4; ++rep) {
			const std::size_t sh = rng() % qn;
			const auto q2 = random_limbs(qn - sh, rep < 2 ? 0 : 1);
			U T = D - small_u(1 + rng() % 3);
			U top = from_limbs(q2) * D + T;
			U N = (top << (sh * U::digit_bits)) + from_limbs(random_limbs(sh, 0));
			check(d, to_limbs(N, dn + qn), "mid remainder D - 1");
		}
		// Random dividends (top dn limbs below d).
		auto n = random_limbs(dn + qn, static_cast<int>(rng() % 6));
		const std::size_t top = dn + qn - dn;
		for (std::size_t k = 0; k < dn; ++k) {
			n[top + k] = rng() % 3 ? d[k] : n[top + k];
		}
		// Keep the top block below d: reduce it.
		U block = from_limbs(std::vector<digit>(n.begin() + top, n.end())) % D;
		auto bl = to_limbs(block, dn);
		std::copy(bl.begin(), bl.end(), n.begin() + top);
		check(d, n, "random");
	}
}

int main(int argc, char** argv) {
	const int rounds = argc > 1 ? std::atoi(argv[1]) : 3;
	const std::size_t big = argc > 2 ? static_cast<std::size_t>(std::atoi(argv[2])) : 40;
	for (std::size_t dn = 2; dn <= 12; ++dn) {
		for (std::size_t qn = 1; qn <= 14; ++qn) {
			for (int r = 0; r < rounds; ++r) {
				run_shape(dn, qn);
			}
		}
	}
	// Larger random shapes, short and long quotients.
	for (int it = 0; it < rounds * 12; ++it) {
		const std::size_t dn = 2 + rng() % big;
		const std::size_t qn = 1 + rng() % (big + 10);
		run_shape(dn, qn);
	}
	// A quotient of 2^16 limbs is past the bound's range at 16-bit limbs: the kernel is exact there.
	if (U::digit_bits == 16) {
		const std::size_t dn = 8;
		for (std::size_t qn : {static_cast<std::size_t>(65535), static_cast<std::size_t>(65536), static_cast<std::size_t>(65537)}) {
			const auto d = make_divisor(dn, 2);
			std::vector<digit> n = random_limbs(dn + qn, 0);
			n.back() = 0;
			check(d, n, "quotient of B limbs");
		}
	}
	std::printf("divq_kernel: %zu cases, status 0: %zu, flag 1: %zu, flag 2: %zu; exact multiples flagged: %zu of %zu\n",
		stats.cases, stats.status[0], stats.status[1], stats.status[2], stats.zero_flagged, stats.zero_cases);
	CHECK(stats.status[1] > 0, "no case forced flag 1");
	CHECK(stats.status[2] > 0, "no case forced flag 2");
	if (failures) {
		std::printf("divq_kernel: %d failures\n", failures);
		return 1;
	}
	std::printf("divq_kernel OK\n");
	return 0;
}

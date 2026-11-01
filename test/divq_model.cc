// An independent model of the quotient-only schoolbook division k_div_sb_trunc, with its repair, over a
// run-time base B (limbs are uint32_t values below B, B from 4 to 65536). It follows the kernel's control
// flow (the exact digits, the truncated digits with their flags, the repair, the exact digits after it) and
// has its own limb arithmetic: the exact reference divides by trial digit and a limb-array compare, with
// none of the 3-by-2 theory. It does not include the header. At base 4 every divisor and dividend of a small
// size can be enumerated, so the lemmas of docs/design/quotient-only-division.md are checked on all of them,
// and the cases that break the bound can be found.
//
//   divq_model [light]                            the checks CTest runs (light: a smaller sweep, for unoptimized builds)
//   divq_model exh B dn qn G guard [part parts]   every normalized D and every N with its top dn limbs below D
//   divq_model rnd B dn qn G guard iters [seed]   random and structured adversarial dividends
//   MUT=n divq_model ...                          one mutation of the repair (see trunc_model), to show the model has teeth
//
// For every input the status must not be 3, the quotient must be the reference quotient (flagged or not), and
// after a flag np[0..dn) must be the reference remainder. G is the window (div_trunc_window, 2 in the header);
// guard 1 sends a quotient of B limbs or more to the exact division, as the header does.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

typedef std::uint64_t u64;
typedef std::uint32_t u32;
typedef std::vector<u32> V;

static u64 B;
static int MUT = 0;
static std::mt19937_64 rng(777);

// w[0..dn+1) / D[0..dn): the digit by trial and compare, leaving the remainder in w[0..dn) and 0 in w[dn].
static u32 exact_digit(u32* w, const V& D) {
	const size_t dn = D.size();
	const u64 top3 = (u64(w[dn]) * B + w[dn - 1]) * B + w[dn - 2];
	const u64 Dp = u64(D[dn - 1]) * B + D[dn - 2];
	u64 qh = std::min(top3 / Dp, B - 1);
	for (;;) {
		V P(dn + 1);
		u64 c = 0;
		for (size_t k = 0; k < dn; ++k) {
			const u64 t = qh * D[k] + c;
			P[k] = u32(t % B);
			c = t / B;
		}
		P[dn] = u32(c);
		int cmp = 0;
		for (size_t k = dn + 1; k--;) {
			if (P[k] != w[k]) {
				cmp = P[k] < w[k] ? -1 : 1;
				break;
			}
		}
		if (cmp > 0) {
			--qh;
			continue;
		}
		long borrow = 0;
		for (size_t k = 0; k <= dn; ++k) {
			long t = long(w[k]) - long(P[k]) - borrow;
			borrow = t < 0;
			if (t < 0) {
				t += long(B);
			}
			w[k] = u32(t);
		}
		return u32(qh);
	}
}

// The reference quotient, leaving the remainder in N[0..dn).
static V ref_div(V& N, const V& D) {
	const size_t dn = D.size(), qn = N.size() - dn;
	V q(qn);
	for (size_t i = qn; i--;) {
		q[i] = exact_digit(N.data() + i, D);
	}
	return q;
}

// p[0..n) -= m * D[off..off+n), returning the borrow limb.
static u32 submul_1(u32* p, const V& D, size_t off, size_t n, u64 m) {
	u64 c = 0;
	for (size_t k = 0; k < n; ++k) {
		const u64 t = m * D[off + k] + c;
		const u64 lo = t % B;
		c = t / B;
		long s = long(p[k]) - long(lo);
		if (s < 0) {
			s += long(B);
			++c;
		}
		p[k] = u32(s);
	}
	return u32(c);
}

// p[0..n) -= b, returning the borrow out.
static u32 sub_1(u32* p, size_t n, u32 b) {
	for (size_t k = 0; k < n && b; ++k) {
		long s = long(p[k]) - long(b);
		b = s < 0;
		if (s < 0) {
			s += long(B);
		}
		p[k] = u32(s);
	}
	return b;
}

// p[0..n) += D[off..off+n), the carry dropped.
static void add_n(u32* p, const V& D, size_t off, size_t n) {
	u64 c = 0;
	for (size_t k = 0; k < n; ++k) {
		const u64 s = u64(p[k]) + D[off + k] + c;
		p[k] = u32(s % B);
		c = s / B;
	}
}

static int cmp_n(const u32* a, const V& D) {
	for (size_t k = D.size(); k--;) {
		if (a[k] != D[k]) {
			return a[k] < D[k] ? -1 : 1;
		}
	}
	return 0;
}

struct Out {
	int st;  // 0 no flag, 1 or 2 the flag the repair ran on, 3 the repair's own checks failed
	V q;
	V np;
	size_t at;     // the flagged digit
	int wrap;      // the repair's state wrapped (a negative remainder)
	int terms;     // the products the repair subtracted
	int deepripple;  // a borrow that rippled through a limb of B - 1
};

static Out trunc_model(V np, const V& D, size_t G, bool guard) {
	const size_t dn = D.size(), nn = np.size(), qn = nn - dn;
	Out o{0, V(qn, u32(0xDEADBEEF % B)), V(), 0, 0, 0, 0};
	if (guard && qn > B - 1) {
		o.q = ref_div(np, D);
		o.np = np;
		return o;
	}
	const u64 d1 = D[dn - 1], d0 = D[dn - 2];
	const size_t exact = dn - 2 > G ? dn - 2 - G : 0;
	size_t i = qn;
	for (; i > exact; --i) {
		o.q[i - 1] = exact_digit(np.data() + i - 1, D);
	}
	unsigned flag = 0;
	size_t lo = 0;  // the lowest digit applied to the limbs
	while (i--) {
		u32* w = np.data() + i;
		if (w[dn] == d1 && w[dn - 1] == d0) {
			flag = 1;
			lo = i + 1;
			break;
		}
		const u64 n2 = w[dn], n1 = w[dn - 1], n0 = w[dn - 2];
		const u64 top3 = (n2 * B + n1) * B + n0;
		const u64 Dp = d1 * B + d0;
		u64 qd = top3 / Dp;
		if (qd > B - 1) {
			std::fprintf(stderr, "premise broken: the 3-by-2 quotient overflows\n");
			std::exit(3);
		}
		const u64 rem = top3 - qd * Dp;
		const u64 r1 = rem / B, r0 = rem % B;
		const size_t len = i + G, k0 = dn - 2 - len;
		const u32 cy = submul_1(w + k0, D, k0, len, qd);
		long t0 = long(r0) - long(cy);
		const long b = t0 < 0;
		if (t0 < 0) {
			t0 += long(B);
		}
		w[dn - 2] = u32(t0);
		long t1 = long(r1) - b;
		const long bor = t1 < 0;
		if (t1 < 0) {
			t1 += long(B);
		}
		w[dn - 1] = u32(t1);
		if (bor) {
			--qd;
			add_n(w + k0, D, k0, dn - k0);
		} else if (!(w[dn - 1] | w[dn - 2])) {
			o.q[i] = u32(qd);
			flag = 2;
			lo = i;
			if (MUT == 6) {
				lo = i + 1;  // the repair starts one digit too high
			}
			break;
		}
		o.q[i] = u32(qd);
	}
	if (!flag) {
		o.np = np;
		return o;
	}
	o.at = i;

	// The repair: subtract the products the truncated digits lo.. left out; `wrap` collects the borrows out of
	// the top of the state.
	const size_t top = lo + dn;
	u32 wrap = 0;
	size_t mend = std::min(qn, exact);
	if (MUT == 9 && mend > lo) {
		--mend;  // the last truncated term is dropped
	}
	for (size_t m = lo; m < mend; ++m) {
		size_t k = dn - 2 - G - m;
		if (MUT == 8) {
			--k;  // a term over k - 1 limbs
		}
		u32* p = np.data() + m;
		const u32 cy = submul_1(p, D, 0, k, o.q[m]);
		const long s = long(p[k]) - long(cy);
		const u32 b = s < 0;
		p[k] = u32(s < 0 ? s + long(B) : s);
		size_t rl = top - (m + k + 1);
		if (MUT == 2) {
			rl = 0;  // no ripple
		}
		if (MUT == 3 && rl > 0) {
			--rl;  // the ripple one limb short
		}
		if (MUT == 10 && rl > 1) {
			rl = 1;  // the ripple over one limb
		}
		const u32 wr = sub_1(p + k + 1, rl, b);
		if (b && rl > 1 && p[k + 1] == B - 1) {
			++o.deepripple;
		}
		++o.terms;
		wrap = MUT == 1 ? wr : (wrap | wr);  // MUT 1: only the last term's wrap counts
	}
	o.wrap = int(wrap);
	if (flag == 1) {
		if (wrap) {
			o.st = 3;
			o.np = np;
			return o;
		}
	} else if (wrap) {
		if (MUT != 4) {
			--o.q[lo];  // MUT 4: the digit is not lowered
		}
		if (MUT != 5) {
			add_n(np.data() + lo, D, 0, dn);  // MUT 5: the divisor is not added back
		}
	}
	if (MUT != 7 && cmp_n(np.data() + lo, D) >= 0) {  // MUT 7: no check (the safety net)
		o.st = 3;
		o.np = np;
		return o;
	}
	for (size_t j = flag == 1 ? i + 1 : i; j--;) {
		o.q[j] = exact_digit(np.data() + j, D);
	}
	o.st = int(flag);
	o.np = np;
	return o;
}

struct Stats {
	long long cases = 0, flags[4] = {}, wrong = 0, wrongrem = 0, wrap = 0, multi = 0, deep = 0;
	long long first[3] = {}, middle[3] = {}, last[3] = {};  // where the flags fall among the truncated digits
	bool show = false;                                          // print the first counterexamples
	bool clean() const { return !wrong && !wrongrem && !flags[3]; }
};
static Stats stats;

static void dump(const char* kind, const V& N, const V& D, const V& got, const V& ref, int st) {
	std::printf("%s: B=%llu dn=%zu qn=%zu st=%d\n  D=", kind, (unsigned long long)B, D.size(), N.size() - D.size(), st);
	for (size_t k = D.size(); k--;) {
		std::printf("%u ", D[k]);
	}
	std::printf("\n  N=");
	for (size_t k = N.size(); k--;) {
		std::printf("%u ", N[k]);
	}
	std::printf("\n  got=");
	for (size_t k = got.size(); k--;) {
		std::printf("%u ", got[k]);
	}
	std::printf("\n  ref=");
	for (size_t k = ref.size(); k--;) {
		std::printf("%u ", ref[k]);
	}
	std::printf("\n");
}

static void run(const V& N, const V& D, size_t G, bool guard) {
	const Out o = trunc_model(N, D, G, guard);
	++stats.cases;
	++stats.flags[o.st];
	V Nref = N;
	const V qref = ref_div(Nref, D);
	if (o.st == 3) {
		if (stats.show && stats.flags[3] <= 5) {
			dump("the repair's own checks failed", N, D, o.q, qref, 3);
		}
		return;
	}
	if (o.q != qref && ++stats.wrong <= 5 && stats.show) {
		dump("wrong quotient", N, D, o.q, qref, o.st);
	}
	if (o.st) {
		const V gr(o.np.begin(), o.np.begin() + D.size()), rr(Nref.begin(), Nref.begin() + D.size());
		if (gr != rr && ++stats.wrongrem <= 5 && stats.show) {
			dump("wrong remainder", N, D, gr, rr, o.st);
		}
		const size_t exact = D.size() - 2 > G ? D.size() - 2 - G : 0;
		const size_t trunc = std::min(N.size() - D.size(), exact);
		const int where = trunc == 1 || o.at == 0 ? 2 : o.at + 1 == trunc ? 0 : 1;
		(where == 0 ? stats.first : where == 1 ? stats.middle : stats.last)[o.st]++;
		stats.wrap += o.st == 2 && o.wrap;
		stats.multi += o.terms >= 2;
		stats.deep += o.deepripple;
	}
}

static bool less_top(const V& N, const V& D) {
	const size_t dn = D.size(), nn = N.size();
	for (size_t k = dn; k--;) {
		if (N[nn - dn + k] != D[k]) {
			return N[nn - dn + k] < D[k];
		}
	}
	return false;
}

static V add(V a, const V& b) {
	if (a.size() < b.size()) {
		a.resize(b.size());
	}
	u64 c = 0;
	for (size_t k = 0; k < a.size(); ++k) {
		const u64 s = a[k] + (k < b.size() ? b[k] : 0) + c;
		a[k] = u32(s % B);
		c = s / B;
	}
	if (c) {
		a.push_back(u32(c));
	}
	return a;
}

static V mul(const V& a, const V& b) {
	V r(a.size() + b.size() + 1, 0);
	for (size_t i = 0; i < b.size(); ++i) {
		u64 c = 0;
		for (size_t k = 0; k < a.size(); ++k) {
			const u64 t = u64(r[i + k]) + a[k] * u64(b[i]) + c;
			r[i + k] = u32(t % B);
			c = t / B;
		}
		size_t k = a.size() + i;
		while (c) {
			const u64 t = r[k] + c;
			r[k] = u32(t % B);
			c = t / B;
			++k;
		}
	}
	return r;
}

static V sub(V a, const V& b) {
	long br = 0;
	for (size_t k = 0; k < a.size(); ++k) {
		long t = long(a[k]) - long(k < b.size() ? b[k] : 0) - br;
		br = t < 0;
		if (t < 0) {
			t += long(B);
		}
		a[k] = u32(t);
	}
	return a;
}

static V fit(V a, size_t n) {
	a.resize(n, 0);
	return a;
}

// Every normalized divisor D (its top limb at least B/2) and every dividend N with its top dn limbs below D.
// `part` of `parts` takes every parts-th divisor, to split a large run.
static void exhaustive(size_t dn, size_t qn, size_t G, bool guard, int part = 0, int parts = 1) {
	V D(dn);
	u64 total_D = 1;
	for (size_t k = 0; k < dn; ++k) {
		total_D *= B;
	}
	u64 lowmax = 1;
	for (size_t k = 0; k < qn; ++k) {
		lowmax *= B;
	}
	V N(dn + qn);
	for (u64 dv = total_D / 2 + u64(part); dv < total_D; dv += u64(parts)) {
		u64 t = dv;
		for (size_t k = 0; k < dn; ++k) {
			D[k] = u32(t % B);
			t /= B;
		}
		for (u64 tb = 0; tb < dv; ++tb) {
			u64 x = tb;
			for (size_t k = 0; k < dn; ++k) {
				N[qn + k] = u32(x % B);
				x /= B;
			}
			for (u64 lv = 0; lv < lowmax; ++lv) {
				u64 y = lv;
				for (size_t k = 0; k < qn; ++k) {
					N[k] = u32(y % B);
					y /= B;
				}
				run(N, D, G, guard);
			}
		}
	}
}

static V rand_limbs(size_t n, int style) {
	V v(n);
	for (auto& x : v) {
		switch (style) {
			case 0: x = u32(rng() % B); break;
			case 1: x = u32(B - 1); break;
			case 2: x = (rng() % 4) ? 0 : u32(rng() % B); break;
			case 3: x = (rng() & 1) ? u32(B - 1) : 0; break;
			default: x = u32(B - 1 - rng() % 3 % B); break;
		}
	}
	return v;
}

// Random divisors (all ones below the top, zero below them, B/2 over zero) and dividends Q D + T with T of 0,
// 1, D - 1, D - s, D - B^j, random, or the top limbs of D with random or zero low limbs, some of them moved
// into the middle of the division over junk limbs.
static void random_run(size_t dn, size_t qn, size_t G, bool guard, long iters) {
	for (long it = 0; it < iters; ++it) {
		V D = rand_limbs(dn, int(rng() % 5));
		switch (rng() % 4) {
			case 0: break;
			case 1: for (size_t k = 0; k + 2 < dn; ++k) D[k] = u32(B - 1); break;
			case 2: for (size_t k = 0; k + 2 < dn; ++k) D[k] = rng() % 8 ? u32(B - 1) : u32(rng() % B); break;
			case 3: D[dn - 2] = 0; for (size_t k = 0; k + 2 < dn; ++k) D[k] = u32(B - 1); break;
		}
		D[dn - 1] |= u32(B / 2);
		if (D[dn - 1] >= B) {
			D[dn - 1] = u32(B - 1);
		}
		if (rng() % 8 == 0) {
			D[dn - 1] = u32(B / 2);
			D[dn - 2] = 0;
		}
		const V Q = rand_limbs(qn, int(rng() % 5));
		V T;
		switch (rng() % 7) {
			case 0: T = V(dn, 0); break;
			case 1: T = sub(D, fit(V{1}, dn)); break;
			case 2: {
				const V s = rand_limbs(1 + rng() % (dn > 3 ? dn - 2 : 1), 0);
				T = sub(D, fit(s, dn));
			} break;
			case 3: {
				V s(dn, 0);
				s[rng() % dn] = 1;
				T = sub(D, s);
			} break;
			case 4: T = fit(V{u32(rng() % 3)}, dn); break;
			case 5:
				T = D;
				for (size_t k = 0; k + 2 < dn; ++k) {
					T[k] = rng() % 2 ? u32(rng() % B) : 0;
				}
				break;
			default:
				T = rand_limbs(dn, 0);
				T[dn - 1] = D[dn - 1] > 0 ? u32(rng() % D[dn - 1]) : 0;
				break;
		}
		bool lt = false;
		for (size_t k = dn; k--;) {
			if (T[k] != D[k]) {
				lt = T[k] < D[k];
				break;
			}
		}
		if (!lt) {
			T = V(dn, 0);
		}
		V N = add(mul(Q, D), T);
		N.resize(dn + qn, 0);
		if (rng() % 2 == 0) {
			const size_t s = rng() % qn;
			V Nt(dn + qn, 0);
			for (size_t k = s; k < N.size(); ++k) {
				Nt[k] = N[k];
			}
			const int js = int(rng() % 3);
			for (size_t k = 0; k < s; ++k) {
				Nt[k] = js == 0 ? u32(B - 1) : js == 1 ? 0 : u32(rng() % B);
			}
			N = Nt;
		}
		if (less_top(N, D)) {
			run(N, D, G, guard);
		}
	}
}

static void print(const char* mode, size_t dn, size_t qn, size_t G, bool guard) {
	std::printf("%s B=%llu dn=%zu qn=%zu G=%zu guard=%d MUT=%d: cases %lld (no flag %lld, flag 1 %lld, flag 2 %lld), STATUS3 %lld, "
	            "WRONG quotient %lld, WRONG remainder %lld; flag 2 wrapped %lld, repairs of 2 or more terms %lld, deep ripples %lld; "
	            "flag 1 at first/middle/last %lld/%lld/%lld, flag 2 %lld/%lld/%lld\n",
	            mode, (unsigned long long)B, dn, qn, G, int(guard), MUT, stats.cases, stats.flags[0], stats.flags[1], stats.flags[2],
	            stats.flags[3], stats.wrong, stats.wrongrem, stats.wrap, stats.multi, stats.deep, stats.first[1], stats.middle[1],
	            stats.last[1], stats.first[2], stats.middle[2], stats.last[2]);
}

// The checks CTest runs, at base 4.
static int checks(bool light) {
	int failed = 0;
	auto expect = [&](bool ok, const char* what) {
		if (!ok) {
			std::printf("FAIL: %s\n", what);
			++failed;
		}
	};
	B = 4;

	// The repair and the flags are exact: every case of dn = 5 and qn = 2 (the digit above the window and the
	// truncated digit below it; qn = 1 in light mode), both flags at the truncated digit, with wrapped states.
	const size_t qn = light ? 1 : 2;
	stats = Stats();
	exhaustive(5, qn, 2, true);
	expect(stats.clean(), "dn = 5, every case: the quotient and the remainder are exact");
	expect(stats.flags[1] && stats.flags[2] && stats.wrap, "dn = 5: both flags and a wrapped state occur");

	// Adversarial dividends where the repair subtracts several terms and the borrow ripples through limbs of B - 1.
	stats = Stats();
	random_run(7, 3, 2, true, light ? 20000 : 60000);
	expect(stats.clean(), "dn = 7, qn = 3, adversarial dividends: the quotient and the remainder are exact");
	expect(stats.multi && stats.deep && stats.first[1] && stats.middle[1] && stats.last[1] && stats.first[2] && stats.middle[2] &&
	           stats.last[2],
	       "dn = 7, qn = 3: repairs of several terms, deep ripples, and both flags at the first, a middle and the last digit");

	// The window is needed: one limb gives wrong digits no flag catches.
	stats = Stats();
	random_run(5, 3, 1, true, light ? 5000 : 20000);
	expect(stats.wrong > 0, "a window of one limb gives a wrong quotient");

	// The model catches a broken repair (mutation 7, the check the proof makes unreachable, is the one it survives).
	for (int m : {1, 2, 3, 4, 5, 6, 8, 9, 10}) {
		MUT = m;
		stats = Stats();
		random_run(7, 3, 2, true, light ? 5000 : 20000);
		char what[64];
		std::snprintf(what, sizeof what, "mutation %d of the repair is caught", m);
		expect(!stats.clean(), what);
	}
	MUT = 0;

	// The bound qn < B is needed: at base 4 with a quotient of B + 2 limbs the kernel gives a wrong digit no flag
	// catches, and with the guard (the exact division for a quotient of B limbs or more) the division is exact.
	// The kernel's quotient is 3 3 2 3 3 3 and the exact one 3 3 2 3 3 2 (top limb first, like D and N below).
	V D = {3, 3, 0, 1, 3, 3, 3, 3, 3, 3};
	V N = {3, 2, 3, 2, 0, 2, 2, 0, 3, 1, 0, 0, 1, 0, 0, 0};
	std::reverse(D.begin(), D.end());
	std::reverse(N.begin(), N.end());
	stats = Stats();
	run(N, D, 2, false);
	expect(stats.wrong == 1 && stats.flags[0] == 1, "dn = 10, qn = 6, no guard: an unflagged wrong digit");
	stats = Stats();
	run(N, D, 2, true);
	expect(stats.clean() && stats.flags[0] == 1, "dn = 10, qn = 6, with the guard: exact");

	if (!failed) {
		std::printf("divq_model OK: the repair, the flags and the window at base 4, and the bound qn < B\n");
	}
	return failed != 0;
}

int main(int argc, char** argv) {
	if (const char* m = std::getenv("MUT")) {
		MUT = std::atoi(m);
	}
	if (argc == 1 || (argc == 2 && !std::strcmp(argv[1], "light"))) {
		return checks(argc == 2);
	}
	if (argc < 7) {
		std::printf("usage: divq_model [exh|rnd B dn qn G guard ...]\n");
		return 2;
	}
	const char* mode = argv[1];
	B = std::strtoull(argv[2], nullptr, 10);
	const size_t dn = size_t(std::atoi(argv[3])), qn = size_t(std::atoi(argv[4])), G = size_t(std::atoi(argv[5]));
	const bool guard = std::atoi(argv[6]) != 0;
	stats.show = true;
	if (!std::strcmp(mode, "exh")) {
		exhaustive(dn, qn, G, guard, argc > 7 ? std::atoi(argv[7]) : 0, argc > 8 ? std::atoi(argv[8]) : 1);
	} else {
		rng.seed(argc > 8 ? std::strtoull(argv[8], nullptr, 10) : 777);
		random_run(dn, qn, G, guard, std::atol(argv[7]));
	}
	print(mode, dn, qn, G, guard);
	return stats.clean() ? 0 : 1;
}

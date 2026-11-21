// Multiplication cutoffs and scratch sizes, Toom-3, Toom-4 and the squaring dispatch. Class body of uinteger_t, included inside it by uinteger_t.hh; not a header.

	// Algorithm cutoffs in limbs: the multiplication ones compare against the smaller operand. They
	// are limb counts, not bit counts: the crossovers are set by the cost of a limb product against
	// a limb add, which is the same for 64- and 32-bit limbs (swept on Apple M4 with bench/tune.cc).
	// Each can be set by macro; the tests force every path at small sizes with tiny values.
#ifdef UINTEGER_T_MUL_KARATSUBA_THRESHOLD
	static constexpr std::size_t mul_karatsuba_threshold = UINTEGER_T_MUL_KARATSUBA_THRESHOLD;
#else
	static constexpr std::size_t mul_karatsuba_threshold = 24;
#endif
#ifdef UINTEGER_T_MUL_TOOM3_THRESHOLD
	static constexpr std::size_t mul_toom3_threshold = UINTEGER_T_MUL_TOOM3_THRESHOLD;
#else
	static constexpr std::size_t mul_toom3_threshold = 140;
#endif
#ifdef UINTEGER_T_SQR_KARATSUBA_THRESHOLD
	static constexpr std::size_t sqr_karatsuba_threshold = UINTEGER_T_SQR_KARATSUBA_THRESHOLD;
#else
	static constexpr std::size_t sqr_karatsuba_threshold = 40;
#endif
#ifdef UINTEGER_T_SQR_TOOM3_THRESHOLD
	static constexpr std::size_t sqr_toom3_threshold = UINTEGER_T_SQR_TOOM3_THRESHOLD;
#else
	static constexpr std::size_t sqr_toom3_threshold = 300;
#endif
#ifdef UINTEGER_T_MUL_TOOM4_THRESHOLD
	static constexpr std::size_t mul_toom4_threshold = UINTEGER_T_MUL_TOOM4_THRESHOLD;
#else
	static constexpr std::size_t mul_toom4_threshold = 400;
#endif
	static_assert(mul_karatsuba_threshold >= 2 && sqr_karatsuba_threshold >= 2, "a Karatsuba threshold below 2 limbs would recurse forever");

	// The transform (three-prime number-theoretic) multiplication takes over from Toom-4 at these
	// limb counts of the smaller operand; see the transform section below.
#ifdef UINTEGER_T_MUL_NTT_THRESHOLD
	static constexpr std::size_t mul_ntt_threshold = UINTEGER_T_MUL_NTT_THRESHOLD;
#else
	static constexpr std::size_t mul_ntt_threshold = 1400;
#endif
#ifdef UINTEGER_T_SQR_NTT_THRESHOLD
	static constexpr std::size_t sqr_ntt_threshold = UINTEGER_T_SQR_NTT_THRESHOLD;
#else
	static constexpr std::size_t sqr_ntt_threshold = 1400;
#endif

	// Toom-3 splits the larger operand into thirds of k limbs, and needs a smaller operand with
	// a third piece (bn > 2k) and enough limbs to pay for it.
	static bool k_mul_uses_toom3(std::size_t an, std::size_t bn) {
		return bn >= mul_toom3_threshold && bn > 2 * ((an + 2) / 3);
	}

	static bool k_sqr_uses_toom3(std::size_t n) {
		return n >= sqr_toom3_threshold && n > 2 * ((n + 2) / 3);
	}

	// Toom-4 splits the larger operand into quarters of k limbs and needs a smaller operand with a
	// fourth piece (bn > 3k). Its scratch, with that of the products below it, is under 16 limbs per
	// limb of the larger operand (the property test checks the bound); capping that operand at
	// SIZE_MAX / (32 sizeof(digit)) limbs keeps the scratch in bytes, and every sum of scratch sizes,
	// below the range of std::size_t. A larger product takes Toom-3.
	static constexpr std::size_t toom4_max_limbs = std::numeric_limits<std::size_t>::max() / (32 * sizeof(digit));

	static bool k_mul_uses_toom4(std::size_t an, std::size_t bn) {
		return an <= toom4_max_limbs && bn >= mul_toom4_threshold && bn > 3 * ((an + 3) / 4);
	}

	// The transform takes a product of at least twice the threshold's limbs in all, whose smaller operand has
	// two thirds of the threshold and whose larger one has under ntt_lopsided times as many: one transform
	// over the whole product costs less than chunking the longer operand, and its scratch stays a few times
	// the product (a longer operand is chunked into products the transform takes). A balanced product of
	// the threshold's limbs is the edge. It needs the transform to be sizable; see _ntt_plan.
	static constexpr std::size_t ntt_lopsided = 16;

	static bool k_mul_uses_ntt(std::size_t an, std::size_t bn) {
		std::size_t len = 0, scratch = 0;
		return bn >= mul_ntt_threshold - mul_ntt_threshold / 3 && an / ntt_lopsided < bn && an + bn >= 2 * mul_ntt_threshold &&
			_ntt_plan<std::size_t>(an, bn, false, &len, &scratch);
	}

	static bool k_sqr_uses_ntt(std::size_t n) {
		std::size_t len = 0, scratch = 0;
		return n >= sqr_ntt_threshold && _ntt_plan<std::size_t>(n, n, true, &len, &scratch);
	}

	// Scratch limbs k_mul needs for operands of an and bn limbs; zero below the Karatsuba threshold.
	// It follows the same choices as k_mul, level by level.
	static std::size_t k_mul_scratch_size(std::size_t an, std::size_t bn) {
		if (an < bn) {
			std::swap(an, bn);
		}
		if (bn < mul_karatsuba_threshold) {
			return 0;
		}
		if (k_mul_uses_ntt(an, bn)) {
			return k_ntt_scratch_size(an, bn, false);
		}
		if (an >= 2 * bn) {
			std::size_t need = k_mul_scratch_size(bn, bn);
			if (an % bn) {
				need = std::max(need, k_mul_scratch_size(bn, an % bn));
			}
			return 2 * bn + need;
		}
		if (k_mul_uses_toom4(an, bn)) {
			const std::size_t k = (an + 3) / 4;
			const std::size_t m = k + 1;
			std::size_t need = std::max(k_mul_scratch_size(k, k), k_mul_scratch_size(an - 3 * k, bn - 3 * k));
			return std::max(need, 15 * m + k_mul_scratch_size(m, m));
		}
		if (k_mul_uses_toom3(an, bn)) {
			const std::size_t k = (an + 2) / 3;
			const std::size_t m = k + 1;
			std::size_t need = std::max(k_mul_scratch_size(k, k), k_mul_scratch_size(an - 2 * k, bn - 2 * k));
			return std::max(need, 13 * m + k_mul_scratch_size(m, m));
		}
		const std::size_t h = (an + 1) >> 1;
		if (bn <= h) {
			return an + bn + std::max(k_mul_scratch_size(h, bn), k_mul_scratch_size(an - h, bn));
		}
		std::size_t need = std::max(k_mul_scratch_size(h, h), k_mul_scratch_size(an - h, bn - h));
		return std::max(need, 4 * h + k_mul_scratch_size(h, h));
	}

	// Scratch limbs k_sqr needs for an n-limb operand; zero below the squaring Karatsuba threshold.
	static std::size_t k_sqr_scratch_size(std::size_t n) {
		if (n < sqr_karatsuba_threshold) {
			return 0;
		}
		if (k_sqr_uses_ntt(n)) {
			return k_ntt_scratch_size(n, n, true);
		}
		if (k_sqr_uses_toom3(n)) {
			const std::size_t k = (n + 2) / 3;
			const std::size_t m = k + 1;
			std::size_t need = std::max(k_sqr_scratch_size(k), k_sqr_scratch_size(n - 2 * k));
			return std::max(need, 10 * m + k_sqr_scratch_size(m));
		}
		const std::size_t h = (n + 1) >> 1;
		std::size_t need = std::max(k_sqr_scratch_size(h), k_sqr_scratch_size(n - h));
		return std::max(need, 3 * h + k_sqr_scratch_size(h));
	}

	// With x = x0 + x1 B^k + x2 B^2k (x0 and x1 of k limbs, x2 of x2n <= k limbs, x2n >= 1), the
	// evaluations of the Toom-3 polynomial at 1, -1 and -2, as m = k + 1 limbs each: p1 = x(1),
	// pm1 = |x(-1)|, pm2 = |x(-2)|. `w` is m limbs of temporary space. Returns the signs: bit 0 is set
	// when x(-1) is negative, bit 1 when x(-2) is.
	static unsigned k_toom3_eval(digit* p1, digit* pm1, digit* pm2, digit* w, const digit* x, std::size_t k, std::size_t x2n) {
		const std::size_t m = k + 1;
		const digit* x1 = x + k;
		const digit* x2 = x + 2 * k;
		// pm1 = x0 + x2, p1 = pm1 + x1, pm1 = |pm1 - x1|
		digit c = k_add_n(pm1, x, x2, x2n);
		if (x2n < k) {
			c = k_add_1(pm1 + x2n, x + x2n, k - x2n, c);
		}
		pm1[k] = c;
		c = k_add_n(p1, pm1, x1, k);
		p1[k] = static_cast<digit>(pm1[k] + c);
		unsigned neg = static_cast<unsigned>(k_absdiff(pm1, pm1, m, x1, k));
		// pm2 = |x0 + 4 x2 - 2 x1|
		pm2[x2n] = k_lshift(pm2, x2, x2n, 2);
		std::fill(pm2 + x2n + 1, pm2 + m, digit(0));
		k_add_at(pm2, m, 0, x, k);
		w[k] = k_lshift(w, x1, k, 1);
		neg |= static_cast<unsigned>(k_absdiff(pm2, pm2, m, w, m)) << 1;
		return neg;
	}

	// Toom-3 interpolation (Bodrato and Zanoni, "Integer and Polynomial Multiplication: Towards Optimal
	// Toom-Cook Matrices", ISSAC 2007), for a product c0 + c1 B^k + c2 B^2k + c3 B^3k + c4 B^4k. On entry
	// r[0..2k) holds c0 and r[4k..4k + vn) holds c4, with r[2k..4k) zero; v1, vm1 and vm2 are the
	// N = 2k + 2 limb products at 1, -1 and -2, vm1 and vm2 meant negative when m1neg and m2neg are
	// set. They are consumed: the middle coefficients are added into r (of rn limbs) at k, 2k and 3k.
	// Every step is exact on the non-negative coefficients; the intermediates may be negative, so they
	// are taken mod B^N in two's complement and the divisions by 2 are only done on non-negative values.
	static void k_toom3_interp(digit* r, std::size_t rn, std::size_t k, std::size_t vn, digit* v1, digit* vm1, digit* vm2, bool m1neg, bool m2neg) {
		const std::size_t n = 2 * k + 2;
		const digit* v0 = r;
		const digit* vinf = r + 4 * k;
		if (m1neg) {
			k_neg_n(vm1, vm1, n);
		}
		if (m2neg) {
			k_neg_n(vm2, vm2, n);
		}
		k_sub_n(vm2, vm2, v1, n);               // vm2 - v1 = 3 (-c1 + c2 - 3 c3 + 5 c4)
		k_divexact_3(vm2, vm2, n);              // t3 = -c1 + c2 - 3 c3 + 5 c4
		k_sub_n(v1, v1, vm1, n);                // v1 - vm1 = 2 (c1 + c3)
		k_rshift(v1, v1, n, 1);                 // t1 = c1 + c3
		k_sub_wrap(vm1, n, v0, 2 * k);          // t2 = vm1 - c0 = -c1 + c2 - c3 + c4
		k_sub_n(vm2, vm1, vm2, n);              // t2 - t3 = 2 c3 - 4 c4
		digit c = k_addmul_1(vm2, vinf, vn, 4); // + 4 c4 = 2 c3
		c = _addcarry(vm2[vn], c, 0, &vm2[vn]);
		k_add_1(vm2 + vn + 1, vm2 + vn + 1, n - vn - 1, c);
		k_rshift(vm2, vm2, n, 1);               // c3
		k_add_n(vm1, vm1, v1, n);               // t2 + t1 = c2 + c4
		k_sub_wrap(vm1, n, vinf, vn);           // c2
		k_sub_n(v1, v1, vm2, n);                // c1 = t1 - c3
		auto add = [&](const digit* p, std::size_t at) {
			std::size_t pn = n;
			while (pn && !p[pn - 1]) {
				--pn;
			}
			k_add_at(r, rn, at, p, pn);
		};
		add(v1, k);
		add(vm1, 2 * k);
		add(vm2, 3 * k);
	}

	// r[0..an+bn) = a * b by Toom-3, for an >= bn > 2k with k = ceil(an / 3): both operands split
	// into three pieces, the product read off five products at 0, 1, -1, -2 and infinity.
	static void k_mul_toom3(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s) {
		const std::size_t k = (an + 2) / 3;
		const std::size_t m = k + 1;
		const std::size_t n = 2 * m;
		const std::size_t a2n = an - 2 * k;
		const std::size_t b2n = bn - 2 * k;
		assert(an >= bn && b2n >= 1);

		// c0 = a0 b0 and c4 = a2 b2 land directly in r.
		k_mul(r, a, k, b, k, s);
		k_mul(r + 4 * k, a + 2 * k, a2n, b + 2 * k, b2n, s);
		std::fill(r + 2 * k, r + 4 * k, digit(0));

		digit* v1 = s;
		digit* vm1 = v1 + n;
		digit* vm2 = vm1 + n;
		digit* a1 = vm2 + n;
		digit* am1 = a1 + m;
		digit* am2 = am1 + m;
		digit* b1 = am2 + m;
		digit* bm1 = b1 + m;
		digit* bm2 = bm1 + m;
		digit* w = bm2 + m;
		s = w + m;

		unsigned sa = k_toom3_eval(a1, am1, am2, w, a, k, a2n);
		unsigned sb = k_toom3_eval(b1, bm1, bm2, w, b, k, b2n);
		k_mul(v1, a1, m, b1, m, s);
		k_mul(vm1, am1, m, bm1, m, s);
		k_mul(vm2, am2, m, bm2, m, s);
		unsigned sign = sa ^ sb;
		k_toom3_interp(r, an + bn, k, a2n + b2n, v1, vm1, vm2, sign & 1, sign & 2);
	}

	// r[0..2n) = a^2 by Toom-3, for n > 2k with k = ceil(n / 3).
	static void k_sqr_toom3(digit* r, const digit* a, std::size_t n, digit* s) {
		const std::size_t k = (n + 2) / 3;
		const std::size_t m = k + 1;
		const std::size_t pn = 2 * m;
		const std::size_t a2n = n - 2 * k;
		assert(a2n >= 1);

		k_sqr(r, a, k, s);
		k_sqr(r + 4 * k, a + 2 * k, a2n, s);
		std::fill(r + 2 * k, r + 4 * k, digit(0));

		digit* v1 = s;
		digit* vm1 = v1 + pn;
		digit* vm2 = vm1 + pn;
		digit* a1 = vm2 + pn;
		digit* am1 = a1 + m;
		digit* am2 = am1 + m;
		digit* w = am2 + m;
		s = w + m;

		k_toom3_eval(a1, am1, am2, w, a, k, a2n);
		k_sqr(v1, a1, m, s);
		k_sqr(vm1, am1, m, s);
		k_sqr(vm2, am2, m, s);
		k_toom3_interp(r, 2 * n, k, 2 * a2n, v1, vm1, vm2, false, false);
	}

	// With x = x0 + x1 B^k + x2 B^2k + x3 B^3k (x0, x1 and x2 of k limbs, x3 of x3n <= k limbs, x3n >= 1),
	// the Toom-4 evaluations at 1 and -1 as m = k + 1 limbs each: p1 = x(1), pm1 = |x(-1)|. `w` is m limbs
	// of temporary space. Returns 1 when x(-1) is negative.
	static unsigned k_toom4_eval1(digit* p1, digit* pm1, digit* w, const digit* x, std::size_t k, std::size_t x3n) {
		const std::size_t m = k + 1;
		const digit* x1 = x + k;
		const digit* x2 = x + 2 * k;
		const digit* x3 = x + 3 * k;
		// The even part pm1 = x0 + x2 and the odd part w = x1 + x3 give x(1) = pm1 + w and x(-1) = pm1 - w.
		pm1[k] = k_add_n(pm1, x, x2, k);
		digit c = k_add_n(w, x1, x3, x3n);
		if (x3n < k) {
			c = k_add_1(w + x3n, x1 + x3n, k - x3n, c);
		}
		w[k] = c;
		k_add_n(p1, pm1, w, m);
		return static_cast<unsigned>(k_absdiff(pm1, pm1, m, w, m));
	}

	// The evaluations at 2 and -2 in the same form: x(+-2) = (x0 + 4 x2) +- (2 x1 + 8 x3). Each part is
	// below 10 B^k, so it fits m limbs.
	static unsigned k_toom4_eval2(digit* p2, digit* pm2, digit* w, const digit* x, std::size_t k, std::size_t x3n) {
		const std::size_t m = k + 1;
		pm2[k] = k_addlsh_n(pm2, x, x + 2 * k, k, 2);
		w[x3n] = k_lshift(w, x + 3 * k, x3n, 3);
		std::fill(w + x3n + 1, w + m, digit(0));
		w[k] = static_cast<digit>(w[k] + k_addlsh_n(w, w, x + k, k, 1));
		k_add_n(p2, pm2, w, m);
		return static_cast<unsigned>(k_absdiff(pm2, pm2, m, w, m));
	}

	// The evaluation at 1/2 scaled to an integer, 8 x(1/2) = 8 x0 + 4 x1 + 2 x2 + x3 (below 15 B^k), as m limbs.
	static void k_toom4_evalh(digit* ph, const digit* x, std::size_t k, std::size_t x3n) {
		const std::size_t m = k + 1;
		ph[k] = k_lshift(ph, x, k, 3);
		ph[k] = static_cast<digit>(ph[k] + k_addlsh_n(ph, ph, x + k, k, 2));
		ph[k] = static_cast<digit>(ph[k] + k_addlsh_n(ph, ph, x + 2 * k, k, 1));
		k_add_at(ph, m, 0, x + 3 * k, x3n);
	}

	// Toom-4 interpolation for a product c0 + c1 B^k + ... + c6 B^6k, from the values at 0 and infinity
	// (c0 and c6) and at 1, -1, 2, -2 and 1/2. The values at p and -p give the even and odd parts of the
	// product at p, as in Toom-Cook with symmetric points (Knuth, TAOCP vol. 2, 4.3.3; Bodrato and
	// Zanoni, ISSAC 2007; Bodrato, WAIFI 2007). On entry r[0..2k) holds c0 and r[6k..6k + vn) holds c6,
	// with r[2k..6k) zero; v1, vm1, v2, vm2 and vh are the N = 2k + 2 limb products at 1, -1, 2, -2 and
	// 1/2 (the last scaled by 2^6), vm1 and vm2 meant negative when m1neg and m2neg are set; x is N limbs
	// of temporary space. All five are consumed: c1 to c5 are added into r (of rn limbs) at k to 5k.
	// The steps run on values mod B^N, so the one that can be negative (c1 - c5) needs no sign branch;
	// the divisions are exact, and the halvings and shifts only act on non-negative values.
	static void k_toom4_interp(digit* r, std::size_t rn, std::size_t k, std::size_t vn, digit* v1, digit* vm1, digit* v2, digit* vm2, digit* vh, digit* x, bool m1neg, bool m2neg) {
		const std::size_t n = 2 * k + 2;
		const digit* c0 = r;
		const digit* c6 = r + 6 * k;
		// t -= mul * p[0..pn) mod B^n.
		auto submul = [&](digit* t, const digit* p, std::size_t pn, digit mul) {
			digit b = k_submul_1(t, p, pn, mul);
			if (pn < n) {
				b = _subborrow(t[pn], b, 0, &t[pn]);
				k_sub_1(t + pn + 1, t + pn + 1, n - pn - 1, b);
			}
		};
		if (m1neg) {
			k_neg_n(vm1, vm1, n);
		}
		if (m2neg) {
			k_neg_n(vm2, vm2, n);
		}
		k_sub_n(x, v1, vm1, n);                 // v(1) - v(-1) = 2 (c1 + c3 + c5)
		k_add_n(v1, v1, vm1, n);                // v(1) + v(-1) = 2 (c0 + c2 + c4 + c6)
		k_sub_n(vm1, v2, vm2, n);               // v(2) - v(-2) = 4 (c1 + 4 c3 + 16 c5)
		k_add_n(v2, v2, vm2, n);                // v(2) + v(-2) = 2 (c0 + 4 c2 + 16 c4 + 64 c6)
		k_rshift(v1, v1, n, 1);
		k_sub_wrap(v1, n, c0, 2 * k);
		k_sub_wrap(v1, n, c6, vn);              // p = c2 + c4
		k_rshift(x, x, n, 1);                   // q = c1 + c3 + c5
		k_rshift(v2, v2, n, 1);
		k_sub_wrap(v2, n, c0, 2 * k);
		submul(v2, c6, vn, 64);
		k_rshift(v2, v2, n, 2);                 // t2 = c2 + 4 c4
		k_rshift(vm1, vm1, n, 2);               // t1 = c1 + 4 c3 + 16 c5
		k_sub_n(v2, v2, v1, n);                 // t2 - p = 3 c4
		k_divexact_3(v2, v2, n);                // c4
		k_sub_n(v1, v1, v2, n);                 // c2 = p - c4
		submul(vh, c0, 2 * k, 64);              // v(1/2) - 64 c0 - 16 c2 - 4 c4 - c6 = 2 (16 c1 + 4 c3 + c5)
		submul(vh, v1, n, 16);
		submul(vh, v2, n, 4);
		k_sub_wrap(vh, n, c6, vn);
		k_rshift(vh, vh, n, 1);                 // h = 16 c1 + 4 c3 + c5
		k_add_n(vm2, vh, vm1, n);               // h + t1 = 17 (c1 + c5) + 8 c3
		submul(vm2, x, n, 17);                  // h + t1 - 17 q = -9 c3
		k_neg_n(vm2, vm2, n);
		k_divexact_1(vm2, vm2, n, 9);           // c3
		k_sub_n(vh, vh, vm1, n);                // h - t1 = 15 (c1 - c5)
		k_divexact_1(vh, vh, n, 15);            // d = c1 - c5, negative when c1 < c5
		k_sub_n(x, x, vm2, n);                  // c1 + c5 = q - c3
		k_add_n(vm1, x, vh, n);                 // 2 c1
		k_rshift(vm1, vm1, n, 1);               // c1
		k_sub_n(x, x, vm1, n);                  // c5
		auto add = [&](const digit* p, std::size_t at) {
			std::size_t pn = n;
			while (pn && !p[pn - 1]) {
				--pn;
			}
			k_add_at(r, rn, at, p, pn);
		};
		add(vm1, k);
		add(v1, 2 * k);
		add(vm2, 3 * k);
		add(v2, 4 * k);
		add(x, 5 * k);
	}

	// r[0..an+bn) = a * b by Toom-4, for an >= bn > 3k with k = ceil(an / 4): both operands split into
	// four pieces, the product read off seven products at 0, 1, -1, 2, -2, 1/2 and infinity.
	static void k_mul_toom4(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s) {
		const std::size_t k = (an + 3) / 4;
		const std::size_t m = k + 1;
		const std::size_t n = 2 * m;
		const std::size_t a3n = an - 3 * k;
		const std::size_t b3n = bn - 3 * k;
		assert(an >= bn && b3n >= 1);

		// c0 = a0 b0 and c6 = a3 b3 land directly in r.
		k_mul(r, a, k, b, k, s);
		k_mul(r + 6 * k, a + 3 * k, a3n, b + 3 * k, b3n, s);
		std::fill(r + 2 * k, r + 6 * k, digit(0));

		digit* v1 = s;
		digit* vm1 = v1 + n;
		digit* v2 = vm1 + n;
		digit* vm2 = v2 + n;
		digit* vh = vm2 + n;
		digit* ap = vh + n;
		digit* am = ap + m;
		digit* bp = am + m;
		digit* bm = bp + m;
		digit* w = bm + m;
		s = w + m;

		unsigned sign = k_toom4_eval1(ap, am, w, a, k, a3n) ^ k_toom4_eval1(bp, bm, w, b, k, b3n);
		k_mul(v1, ap, m, bp, m, s);
		k_mul(vm1, am, m, bm, m, s);
		sign |= (k_toom4_eval2(ap, am, w, a, k, a3n) ^ k_toom4_eval2(bp, bm, w, b, k, b3n)) << 1;
		k_mul(v2, ap, m, bp, m, s);
		k_mul(vm2, am, m, bm, m, s);
		k_toom4_evalh(ap, a, k, a3n);
		k_toom4_evalh(bp, b, k, b3n);
		k_mul(vh, ap, m, bp, m, s);
		// The evaluation buffers are dead: the interpolation takes N limbs of them as its temporary.
		k_toom4_interp(r, an + bn, k, a3n + b3n, v1, vm1, v2, vm2, vh, ap, sign & 1, sign & 2);
	}

	// r[0..2n) = a^2 by Karatsuba: with a = a0 + a1 B^h, the middle term 2 a0 a1 is
	// a0^2 + a1^2 - (a0 - a1)^2, so the third square is of h limbs and has no carry limb.
	static void k_sqr_karatsuba(digit* r, const digit* a, std::size_t n, digit* s) {
		const std::size_t h = (n + 1) >> 1;
		const std::size_t a1n = n - h;

		k_sqr(r, a, h, s);
		k_sqr(r + 2 * h, a + h, a1n, s);

		digit* d = s;
		digit* p = d + h;
		s = p + 2 * h;
		k_absdiff(d, a, h, a + h, a1n);
		k_sqr(p, d, h, s);

		// p = z0 + z2 - p, in 2h limbs and a top carry limb.
		digit top = k_sub_n(p, r, p, 2 * h);
		digit c = k_add_n(p, p, r + 2 * h, 2 * a1n);
		if (2 * a1n < 2 * h) {
			c = k_add_1(p + 2 * a1n, p + 2 * a1n, 2 * (h - a1n), c);
		}
		top = static_cast<digit>(c - top);
		std::size_t pn = 2 * h;
		while (pn && !p[pn - 1]) {
			--pn;
		}
		k_add_at(r, 2 * n, h, p, pn);
		if (top) {
			digit t = k_add_1(r + 3 * h, r + 3 * h, 2 * n - 3 * h, top);
			assert(!t); (void)(t);
		}
	}

	// r[0..2n) = a^2 for n >= 1; r must not overlap a. `s` is scratch of k_sqr_scratch_size(n) limbs.
	static void k_sqr(digit* r, const digit* a, std::size_t n, digit* s) {
		if (n < sqr_karatsuba_threshold) {
			k_sqr_basecase(r, a, n);
		} else if (k_sqr_uses_ntt(n)) {
			k_ntt_mul(r, a, n, a, n, true, s);
		} else if (k_sqr_uses_toom3(n)) {
			k_sqr_toom3(r, a, n, s);
		} else {
			k_sqr_karatsuba(r, a, n, s);
		}
	}

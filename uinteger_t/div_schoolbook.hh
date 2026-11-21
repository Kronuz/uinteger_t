// Division kernels by invariant integers: reciprocals, the 3-by-2 step and schoolbook division. Included inside class uinteger_t by uinteger_t.hh.

	// Division by invariant integers follows Moller and Granlund, "Improved division by
	// invariant integers" (IEEE Trans. Computers, 2011): reciprocal of a normalized limb,
	// of a normalized two-limb divisor, and the 3-by-2 division step.

	// Reciprocal of a normalized limb: floor((B^2 - 1) / d) - B.
	static digit k_inv_1(digit d) {
		digit q;
		_divmod(static_cast<digit>(~d), static_cast<digit>(~static_cast<digit>(0)), d, &q);
		return q;
	}

	// Reciprocal of a normalized two-limb divisor: floor((B^3 - 1) / (d1 B + d0)) - B.
	static digit k_inv_3by2(digit d1, digit d0) {
		digit v = k_inv_1(d1);
		// Refine so that v is the reciprocal for (d1, d0): add d0 into the product d1 * v,
		// correcting the estimate on each wrap-around.
		digit p = _mullo(d1, v);
		digit sum = static_cast<digit>(p + d0);
		if (sum < p) {
			--v;
			if (sum >= d1) {
				--v;
				sum = static_cast<digit>(sum - d1);
			}
			sum = static_cast<digit>(sum - d1);
		}
		digit lo;
		digit hi = _mult(d0, v, &lo);
		digit top = static_cast<digit>(sum + hi);
		if (top < hi) {
			--v;
			if (top > d1 || (top == d1 && lo >= d0)) {
				--v;
			}
		}
		return v;
	}

	// Divides (n2,n1,n0) by the normalized (d1,d0) given v = k_inv_3by2(d1, d0), where
	// (n2,n1) < (d1,d0). Returns the quotient limb and the two-limb remainder.
	static digit k_div_3by2(digit n2, digit n1, digit n0, digit d1, digit d0, digit v, digit* rem1, digit* rem0) {
		digit q0;
		digit q1 = _mult(n2, v, &q0);
		q1 = static_cast<digit>(q1 + n2 + _addcarry(q0, n1, 0, &q0));

		// (r1, r0) = (n1 - d1 q1, n0) - (d1, d0) - d0 q1, all modulo B^2
		digit r1 = static_cast<digit>(n1 - _mullo(d1, q1));
		digit r0;
		r1 = static_cast<digit>(r1 - d1 - _subborrow(n0, d0, 0, &r0));
		digit t0;
		digit t1 = _mult(d0, q1, &t0);
		r1 = static_cast<digit>(r1 - t1 - _subborrow(r0, t0, 0, &r0));

		q1 = static_cast<digit>(q1 + 1);
		if (r1 >= q0) {
			q1 = static_cast<digit>(q1 - 1);
			r1 = static_cast<digit>(r1 + d1 + _addcarry(r0, d0, 0, &r0));
		}
		if (r1 > d1 || (r1 == d1 && r0 >= d0)) {
			q1 = static_cast<digit>(q1 + 1);
			r1 = static_cast<digit>(r1 - d1 - _subborrow(r0, d0, 0, &r0));
		}
		*rem1 = r1;
		*rem0 = r0;
		return q1;
	}

	// One quotient digit of schoolbook division: divides the window w[0..dn] (below dp * B) by the
	// normalized dp[0..dn) (dn >= 2), leaves the remainder in w[0..dn) and returns the digit.
	static digit k_div_sb_digit(digit* w, const digit* dp, std::size_t dn, digit d1, digit d0, digit dinv) {
		digit qd;
		if (w[dn] == d1 && w[dn - 1] == d0) {
			// The estimate is the largest limb; the 3-by-2 step does not apply.
			qd = static_cast<digit>(~static_cast<digit>(0));
			digit cy = k_submul_1(w, dp, dn, qd);
			digit bor = _subborrow(w[dn], cy, 0, &w[dn]);
			if (bor) {
				--qd;
				digit c = k_add_n(w, w, dp, dn);
				w[dn] = static_cast<digit>(w[dn] + c);
			}
			return qd;
		}
		// The 3-by-2 step already holds the top two limbs of the remainder, (r1, r0): the
		// product only subtracts from the low dn-2 limbs, and its borrow comes off (r1, r0).
		// The estimate is at most one too large: a borrow out of (r1, r0) adds the divisor back.
		digit r1, r0;
		qd = k_div_3by2(w[dn], w[dn - 1], w[dn - 2], d1, d0, dinv, &r1, &r0);
		const digit cy = k_submul_1(w, dp, dn - 2, qd);
		const digit b = _subborrow(r0, cy, 0, &w[dn - 2]);
		const digit bor = _subborrow(r1, 0, b, &w[dn - 1]);
		w[dn] = 0;
		if (bor) {
			--qd;
			k_add_n(w, w, dp, dn);
		}
		return qd;
	}

	// Schoolbook division of np[0..nn) by the normalized dp[0..dn) (dn >= 2, top bit set).
	// Requires the top dn limbs of np to be below dp; writes nn-dn quotient limbs to q
	// and leaves the remainder in np[0..dn).
	static void k_div_sb(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn) {
		const digit d1 = dp[dn - 1];
		const digit d0 = dp[dn - 2];
		const digit dinv = k_inv_3by2(d1, d0);
		for (std::size_t i = nn - dn; i--;) {
			q[i] = k_div_sb_digit(np + i, dp, dn, d1, d0, dinv);
		}
	}

	// Quotient-only schoolbook division, on the contract of k_div_sb without the remainder. The
	// digit that is `i` places above the last one subtracts its product only over the top
	// min(dn-2, i + G) limbs below the 3-by-2 window (G = div_trunc_window): the limbs under them
	// stay stale, since the digits that follow read nothing below limb dn-2-G. That is about
	// qn^2/2 limb products where k_div_sb takes qn * dn, and it makes the window every later
	// 3-by-2 step reads inexact. A digit decided on an inexact window is right unless its
	// remainder lies within the error of a decision edge. The kernel detects those digits (flag 1:
	// the window's top limbs equal the divisor's; flag 2: the remainder's top two limbs are zero)
	// and repairs the state: it subtracts the products the truncated digits left out, which makes
	// the limbs the exact remainder after the flagged digit, and finishes the division with the exact
	// digit step. The return value is 0 when no digit was flagged (np[0..dn) is not a remainder
	// then), the flag (1 or 2) when the repair ran (the quotient is exact and np[0..dn) is the
	// remainder), and 3 when the repair's own checks failed, which the proof below excludes: np and q
	// are then clobbered, and the caller redoes the division with the exact kernels on a fresh copy of
	// the dividend. `at`, when given, receives the flagged digit.
	//
	// The bound (ARCHITECTURE.md has the outline). B is the limb base,
	// D the divisor, D' its top two limbs, and qn < B^(G-1) (a longer quotient takes the exact
	// kernel). A digit m below dn-2-G leaves the low k = dn-2-m-G limbs of its product out: an
	// error q_m (D mod B^k) B^m < B^(dn-1-G), so at digit i the window W is the true one plus
	// delta <= qn B^(dn-1-G-i) + 1. With q^ the estimate (floor(W / D) or one more) and
	// Y = W - q^ (D - D mod B^k), the true remainder is Y - u, u = q^ (D mod B^k) < B^(dn-1-G-i),
	// so the computed digit q^ - [Y < 0] differs from the true one only for 0 <= Y < u + delta,
	// and u + delta < B^(dn-2). Y < B^(dn-2) means its top two limbs are zero: flag 2. A step
	// without a borrow leaves Y below D' B^(dn-2), and one with a borrow leaves it below D, so the
	// next window's top limbs are at most those of D: only their equality (the case k_div_sb
	// handles apart) can break the 3-by-2 step, flag 1. Digits with i >= dn-2-G subtract the
	// whole product, are exact, and run k_div_sb_digit unchanged.
	//
	// The repair. Up to a flag every digit recorded is the true one, so the limbs hold the exact
	// state plus e, the sum of q_m (D mod B^k_m) B^m over the truncated digits m applied so far, and
	// nothing else (every limb below the lowest digit applied, and every limb the truncated digits
	// skipped, is a dividend limb). Subtracting those terms leaves the exact state: the limbs below
	// the state's top are then the remainder after the last digit applied, mod B^top. At flag 1 that
	// is the remainder after digit i+1, nonnegative. At flag 2 digit i is applied with q^ (the true
	// digit is q^ or q^ - 1), and the remainder after it lies in [-D, D); it is negative exactly
	// when a subtraction wrapped (the integer is limbs minus one wrap times B^top, and at most one
	// wrap happens because the integer exceeds -B^top), and then D is added back and q^ lowered.
	// The exact digit step finishes digits i (flag 1) or i - 1 (flag 2) down to 0.
	static constexpr std::size_t div_trunc_window = 2;

	// The longest quotient the bound covers: B - 1 limbs, the largest digit (`~digit(0)` would
	// promote a 16-bit limb to a negative int). No size reaches it at 32- and 64-bit limbs; a test
	// lowers it to check the guard there.
#ifdef UINTEGER_T_DIV_TRUNC_MAX_QUOTIENT
	static constexpr std::size_t div_trunc_max_quotient = UINTEGER_T_DIV_TRUNC_MAX_QUOTIENT;
#else
	static constexpr std::size_t div_trunc_max_quotient = static_cast<std::size_t>(std::numeric_limits<digit>::max());
#endif

	static unsigned k_div_sb_trunc(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn, std::size_t* at = nullptr) {
		const std::size_t qn = nn - dn;
		// The bound needs qn < B; a longer quotient takes the exact kernel (and returns 0).
		if (qn > div_trunc_max_quotient) {
			k_div_sb(q, np, nn, dp, dn);
			return 0;
		}
		const digit d1 = dp[dn - 1];
		const digit d0 = dp[dn - 2];
		const digit dinv = k_inv_3by2(d1, d0);
		// Digits at or above `exact` subtract the whole product; those below it are truncated.
		const std::size_t exact = dn - 2 > div_trunc_window ? dn - 2 - div_trunc_window : 0;
		std::size_t i = qn;
		for (; i > exact; --i) {
			q[i - 1] = k_div_sb_digit(np + i - 1, dp, dn, d1, d0, dinv);
		}
		unsigned flag = 0;
		std::size_t lo = 0;  // the lowest digit applied to the limbs
		while (i--) {
			digit* w = np + i;
			if (w[dn] == d1 && w[dn - 1] == d0) {
				flag = 1;
				lo = i + 1;
				break;
			}
			digit r1, r0;
			digit qd = k_div_3by2(w[dn], w[dn - 1], w[dn - 2], d1, d0, dinv, &r1, &r0);
			const std::size_t len = i + div_trunc_window;
			const std::size_t k0 = dn - 2 - len;
			const digit cy = k_submul_1(w + k0, dp + k0, len, qd);
			const digit b = _subborrow(r0, cy, 0, &w[dn - 2]);
			const digit bor = _subborrow(r1, 0, b, &w[dn - 1]);
			// w[dn] stays stale: it is at or above the state's top, and nothing reads it.
			if (bor) {
				--qd;
				k_add_n(w + k0, w + k0, dp + k0, dn - k0);
			} else if (!(w[dn - 1] | w[dn - 2])) {
				q[i] = qd;
				flag = 2;
				lo = i;
				break;
			}
			q[i] = qd;
		}
		if (!flag) {
			return 0;
		}
		if (at) {
			*at = i;
		}

		// Subtract the products the truncated digits lo.. left out; `wrap` collects the borrows out of
		// the top of the state.
		const std::size_t top = lo + dn;
		digit wrap = 0;
		for (std::size_t m = lo; m < std::min(qn, exact); ++m) {
			const std::size_t k = dn - 2 - div_trunc_window - m;
			digit* p = np + m;
			const digit cy = k_submul_1(p, dp, k, q[m]);
			const digit b = _subborrow(p[k], cy, 0, &p[k]);
			wrap |= k_sub_1(p + k + 1, p + k + 1, top - (m + k + 1), b);
		}
		if (flag == 1) {
			if (wrap) {
				return 3;
			}
		} else if (wrap) {
			--q[lo];
			k_add_n(np + lo, np + lo, dp, dn);
		}
		if (k_cmp_n(np + lo, dp, dn) >= 0) {
			return 3;
		}
		for (std::size_t j = flag == 1 ? i + 1 : i; j--;) {
			q[j] = k_div_sb_digit(np + j, dp, dn, d1, d0, dinv);
		}
		return flag;
	}

	static int k_cmp_n(const digit* a, const digit* b, std::size_t n) {
		while (n--) {
			if (a[n] != b[n]) {
				return a[n] < b[n] ? -1 : 1;
			}
		}
		return 0;
	}

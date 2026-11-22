// Schoolbook division by invariant integers: reciprocals, the 3-by-2 step and the schoolbook kernels. Included by uinteger_t.hh.

#pragma once

#include "../uinteger_t.hh"

inline uinteger_t::digit uinteger_t::k_inv_1(digit d) {
	digit q;
	_divmod(static_cast<digit>(~d), static_cast<digit>(~static_cast<digit>(0)), d, &q);
	return q;
}

inline uinteger_t::digit uinteger_t::k_inv_3by2(digit d1, digit d0) {
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

inline uinteger_t::digit uinteger_t::k_div_3by2(digit n2, digit n1, digit n0, digit d1, digit d0, digit v, digit* rem1, digit* rem0) {
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

inline uinteger_t::digit uinteger_t::k_div_sb_digit(digit* w, const digit* dp, std::size_t dn, digit d1, digit d0, digit dinv) {
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

inline void uinteger_t::k_div_sb(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn) {
	const digit d1 = dp[dn - 1];
	const digit d0 = dp[dn - 2];
	const digit dinv = k_inv_3by2(d1, d0);
	for (std::size_t i = nn - dn; i--;) {
		q[i] = k_div_sb_digit(np + i, dp, dn, d1, d0, dinv);
	}
}

inline unsigned uinteger_t::k_div_sb_trunc(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn, std::size_t* at) {
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

inline int uinteger_t::k_cmp_n(const digit* a, const digit* b, std::size_t n) {
	while (n--) {
		if (a[n] != b[n]) {
			return a[n] < b[n] ? -1 : 1;
		}
	}
	return 0;
}

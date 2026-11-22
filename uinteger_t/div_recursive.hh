// Recursive (Burnikel-Ziegler) and Newton division kernels. Included by uinteger_t.hh.

#pragma once

#include "../uinteger_t.hh"

inline std::size_t uinteger_t::k_div_dc_bal_scratch_size(std::size_t n) {
	return n < div_dc_threshold ? 0 : k_div_dc_step_scratch_size(n, (n + 1) / 2);
}

inline std::size_t uinteger_t::k_div_dc_step_scratch_size(std::size_t dn, std::size_t c) {
	if (c < div_dc_step_threshold) {
		return 0;
	}
	const std::size_t l = dn - c;
	return std::max(k_div_dc_bal_scratch_size(c), dn + k_mul_scratch_size(std::max(c, l), std::min(c, l)));
}

inline std::size_t uinteger_t::k_div_dc_scratch_size(std::size_t nn, std::size_t dn) {
	const std::size_t m = nn - dn;
	if (k_div_uses_newton(nn, dn)) {
		return k_div_newton_scratch_size(nn, dn);
	}
	if (dn < div_dc_threshold || m < div_dc_step_threshold) {
		return 0;
	}
	const std::size_t r = m % dn;
	std::size_t size = r ? k_div_dc_step_scratch_size(dn, r) : 0;
	if (m >= dn) {
		size = std::max(size, k_div_dc_bal_scratch_size(dn));
	}
	return size;
}

inline void uinteger_t::k_div_dc_step(digit* q, digit* a, std::size_t dn, std::size_t c, const digit* b, digit* s) {
	if (c < div_dc_step_threshold) {
		k_div_sb(q, a, dn + c, b, dn);
		return;
	}

	// Divide the top 2c limbs by the top c limbs of b. The quotient q is then at least the
	// true one (and at most 2 too large), and a[0..dn) + a[dn] * B^dn holds the matching
	// partial remainder: (r1 B^l + low limbs of a), where r1 is the remainder of that division.
	const std::size_t l = dn - c;
	digit* top = a + l;
	const digit* bh = b + l;
	digit carry = 0;
	if (k_cmp_n(top + c, bh, c) == 0) {
		// The top limbs equal the divisor's, so q = B^c - 1 and r1 = low limbs + bh.
		std::fill(q, q + c, static_cast<digit>(~static_cast<digit>(0)));
		carry = k_add_n(top, top, bh, c);
	} else {
		k_div_dc_bal(q, top, c, bh, s);
	}
	a[dn] = carry;

	// Subtract q * (low l limbs of b). While the result is negative, add b back and lower q.
	digit* p = s;
	k_mul(p, q, c, b, l, s + dn);
	const digit borrow = k_sub_n(a, a, p, dn);
	a[dn] = static_cast<digit>(a[dn] - borrow);
	while (a[dn]) {
		const digit c1 = k_add_n(a, a, b, dn);
		a[dn] = static_cast<digit>(a[dn] + c1);
		const digit c2 = k_sub_1(q, q, c, 1);
		assert(!c2); (void)(c2);
	}
}

inline void uinteger_t::k_div_dc_bal(digit* q, digit* a, std::size_t n, const digit* b, digit* s) {
	if (n < div_dc_threshold) {
		k_div_sb(q, a, 2 * n, b, n);
		return;
	}
	// The high half of the quotient first, then the low half over the remainder so far.
	const std::size_t hi = (n + 1) / 2;
	const std::size_t lo = n - hi;
	k_div_dc_step(q + lo, a + lo, n, hi, b, s);
	k_div_dc_step(q, a, n, lo, b, s);
}

inline void uinteger_t::k_div_dc(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn, digit* s) {
	std::size_t m = nn - dn;
	if (k_div_uses_newton(nn, dn)) {
		k_div_newton(q, np, nn, dp, dn, s);
		return;
	}
	if (dn < div_dc_threshold || m < div_dc_step_threshold) {
		k_div_sb(q, np, nn, dp, dn);
		return;
	}
	const std::size_t r = m % dn;
	if (r) {
		m -= r;
		k_div_dc_step(q + m, np + m, dn, r, dp, s);
	}
	while (m) {
		m -= dn;
		k_div_dc_bal(q + m, np + m, dn, dp, s);
	}
}

inline std::size_t uinteger_t::k_div_newton_block(std::size_t m, std::size_t dn) {
	const std::size_t blocks = std::max<std::size_t>(2, (m - 1) / dn + 1);
	return (m - 1) / blocks + 1;
}

inline std::size_t uinteger_t::k_div_newton_inv_scratch_size(std::size_t n) {
	if (n < div_newton_inv_base) {
		return 2 * n;
	}
	const std::size_t l = (n - 1) / 2;
	const std::size_t h = n - l;
	return std::max(k_div_newton_inv_scratch_size(h), std::max((n + h + 1) + k_mul_scratch_size(n, h + 1),
		(2 * n + 4) + k_mul_scratch_size(h + 1, l + 2)));
}

template <typename Size>
inline bool uinteger_t::_div_newton_plan(Size nn, Size dn, Size* scratch) {
	typedef unsigned long long wide;
	const wide max = static_cast<wide>(std::numeric_limits<Size>::max());
	if (dn < 2 || nn <= dn || nn > max / 64) {
		return false;
	}
	const std::size_t m = static_cast<std::size_t>(nn - dn);
	const std::size_t d = static_cast<std::size_t>(dn);
	const std::size_t c = k_div_newton_block(m, d);
	const wide step = std::max(static_cast<wide>(2 * c) + k_mul_scratch_size(c, c), static_cast<wide>(d) + c + k_mul_scratch_size(d, c));
	const wide total = static_cast<wide>(c) + 1 + std::max(static_cast<wide>(k_div_newton_inv_scratch_size(c)), step);
	if (total > max / sizeof(digit) / 4) {
		return false;
	}
	*scratch = static_cast<Size>(total);
	return true;
}

inline bool uinteger_t::k_div_uses_newton(std::size_t nn, std::size_t dn) {
	std::size_t scratch = 0;
	const std::size_t m = nn - dn;
	return dn >= div_newton_threshold && m >= div_newton_quotient_threshold && 2 * m >= dn &&
		_div_newton_plan<std::size_t>(nn, dn, &scratch);
}

inline std::size_t uinteger_t::k_div_newton_scratch_size(std::size_t nn, std::size_t dn) {
	std::size_t scratch = 0;
	const bool ok = _div_newton_plan<std::size_t>(nn, dn, &scratch);
	assert(ok); (void)(ok);
	return scratch;
}

inline void uinteger_t::k_div_newton_inv(digit* w, const digit* t, std::size_t n, digit* s) {
	if (n < div_newton_inv_base) {
		if (n == 1) {
			w[0] = k_inv_1(t[0]);
		} else {
			std::fill(s, s + n, static_cast<digit>(~static_cast<digit>(0)));
			for (std::size_t i = 0; i < n; ++i) {
				s[n + i] = static_cast<digit>(~t[i]);
			}
			k_div_sb(w, s, 2 * n, t, n);
		}
		w[n] = 1;
		return;
	}
	const std::size_t l = (n - 1) / 2;
	const std::size_t h = n - l;
	digit* wh = w + l;
	k_div_newton_inv(wh, t + l, h, s);
	const digit borrow = k_sub_1(wh, wh, h + 1, 4);
	assert(!borrow); (void)(borrow);

	// E' = B^(n+h) - t W_h' as the negated low n + 1 limbs of the product.
	k_mul(s, t, n, wh, h + 1, s + n + h + 1);
	digit carry = 1;
	for (std::size_t i = 0; i <= n; ++i) {
		const digit v = static_cast<digit>(static_cast<digit>(~s[i]) + carry);
		carry = static_cast<digit>(v < carry);
		s[i] = v;
	}
	assert(s[n] < 6);

	// The correction floor(W_h' E'_t / B^(h+1)), E'_t the top l + 2 limbs of E': its low l limbs
	// are the low limbs of W, and its top two are added into W_h'.
	digit* p = s + n + 1;
	k_mul(p, wh, h + 1, s + (h - 1), l + 2, p + n + 3);
	std::copy(p + h + 1, p + h + 1 + l, w);
	digit c = k_add_n(wh, wh, p + h + 1 + l, 2);
	if (c) {
		c = k_add_1(wh + 2, wh + 2, h - 1, c);
	}
	assert(!c && w[n] == 1); (void)(c);
}

inline uinteger_t::k_div_corrections uinteger_t::k_div_newton_step(digit* q, digit* a, std::size_t dn, std::size_t c, const digit* b, const digit* w, digit* s) {
	const digit* ah = a + dn;
	k_mul(s, ah, c, w, c, s + 2 * c);
	const digit carry = k_add_n(q, ah, s + c, c);
	assert(!carry); (void)(carry);

	digit* p = s;
	k_mul(p, q, c, b, dn, s + dn + c);
	k_sub_n(a, a, p, dn + 1);

	unsigned down = 0, up = 0;
	while (a[dn] >> (digit_bits - 1)) {
		a[dn] = static_cast<digit>(a[dn] + k_add_n(a, a, b, dn));
		const digit under = k_sub_1(q, q, c, 1);
		assert(!under && down < div_newton_down); (void)(under);
		++down;
	}
	while (a[dn] || k_cmp_n(a, b, dn) >= 0) {
		a[dn] = static_cast<digit>(a[dn] - k_sub_n(a, a, b, dn));
		const digit over = k_add_1(q, q, c, 1);
		assert(!over && up < div_newton_up); (void)(over);
		++up;
	}
	return k_div_corrections{down, up};
}

inline void uinteger_t::k_div_newton(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn, digit* s) {
	std::size_t m = nn - dn;
	const std::size_t c = k_div_newton_block(m, dn);
	digit* w = s;
	digit* rest = s + c + 1;
	k_div_newton_inv(w, dp + (dn - c), c, rest);
	std::size_t cb = m % c ? m % c : c;
	while (m) {
		m -= cb;
		k_div_newton_step(q + m, np + m, dn, cb, dp, w + (c - cb), rest);
		cb = c;
	}
}

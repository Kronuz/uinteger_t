// The multiplication dispatch: algorithm choice, scratch sizes, Karatsuba and the squaring dispatch. Included by uinteger_t.hh.

#pragma once

#include "../uinteger_t.hh"

inline bool uinteger_t::k_mul_uses_toom3(std::size_t an, std::size_t bn) {
	return bn >= mul_toom3_threshold && bn > 2 * ((an + 2) / 3);
}

inline bool uinteger_t::k_sqr_uses_toom3(std::size_t n) {
	return n >= sqr_toom3_threshold && n > 2 * ((n + 2) / 3);
}

inline bool uinteger_t::k_mul_uses_toom4(std::size_t an, std::size_t bn) {
	return an <= toom4_max_limbs && bn >= mul_toom4_threshold && bn > 3 * ((an + 3) / 4);
}

inline bool uinteger_t::k_mul_uses_ntt(std::size_t an, std::size_t bn) {
	std::size_t len = 0, scratch = 0;
	return bn >= mul_ntt_threshold - mul_ntt_threshold / 3 && an / ntt_lopsided < bn && an + bn >= 2 * mul_ntt_threshold &&
		_ntt_plan<std::size_t>(an, bn, false, &len, &scratch);
}

inline bool uinteger_t::k_sqr_uses_ntt(std::size_t n) {
	std::size_t len = 0, scratch = 0;
	return n >= sqr_ntt_threshold && _ntt_plan<std::size_t>(n, n, true, &len, &scratch);
}

inline std::size_t uinteger_t::k_mul_scratch_size(std::size_t an, std::size_t bn) {
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

inline std::size_t uinteger_t::k_sqr_scratch_size(std::size_t n) {
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

inline void uinteger_t::k_sqr_karatsuba(digit* r, const digit* a, std::size_t n, digit* s) {
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

inline void uinteger_t::k_sqr(digit* r, const digit* a, std::size_t n, digit* s) {
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

inline void uinteger_t::k_mul_karatsuba(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s) {
	const std::size_t h = (an + 1) >> 1;
	if (bn <= h) {
		// b has no high half: a_lo * b + (a_hi * b << h).
		digit* t = s;
		s += an + bn;
		k_mul(r, a, h, b, bn, s);
		std::fill(r + h + bn, r + an + bn, digit(0));
		k_mul(t, a + h, an - h, b, bn, s);
		k_add_at(r, an + bn, h, t, an - h + bn);
		return;
	}

	// a0 b1 + a1 b0 = a0 b0 + a1 b1 - (a0 - a1)(b0 - b1), so the third product is of
	// two h-limb differences (taken as magnitudes, with the sign of the product kept apart).
	const std::size_t a1n = an - h;
	const std::size_t b1n = bn - h;

	// z0 = a0 * b0 and z2 = a1 * b1 land directly in r.
	k_mul(r, a, h, b, h, s);
	k_mul(r + 2 * h, a + h, a1n, b + h, b1n, s);

	digit* da = s;
	digit* db = da + h;
	digit* p = db + h;
	s = p + 2 * h;

	digit neg = k_absdiff(da, a, h, a + h, a1n) ^ k_absdiff(db, b, h, b + h, b1n);
	k_mul(p, da, h, db, h, s);

	// p = z0 + z2 -/+ p (minus when the differences have the same sign), as 2h limbs and a top limb.
	digit top;
	if (neg) {
		top = k_add_n(p, r, p, 2 * h);
	} else {
		top = static_cast<digit>(digit(0) - k_sub_n(p, r, p, 2 * h));
	}
	digit c = k_add_n(p, p, r + 2 * h, a1n + b1n);
	if (a1n + b1n < 2 * h) {
		c = k_add_1(p + a1n + b1n, p + a1n + b1n, 2 * h - a1n - b1n, c);
	}
	top = static_cast<digit>(top + c);

	std::size_t pn = 2 * h;
	while (pn && !p[pn - 1]) {
		--pn;
	}
	k_add_at(r, an + bn, h, p, pn);
	if (top) {
		digit t = k_add_1(r + 3 * h, r + 3 * h, an + bn - 3 * h, top);
		assert(!t); (void)(t);
	}
}

inline void uinteger_t::k_mul(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s) {
	if (an < bn) {
		std::swap(a, b);
		std::swap(an, bn);
	}
	if (bn < mul_karatsuba_threshold) {
		k_mul_basecase(r, a, an, b, bn);
		return;
	}

	if (k_mul_uses_ntt(an, bn)) {
		k_ntt_mul(r, a, an, b, bn, false, s);
	} else if (an >= 2 * bn) {
		// Lopsided: multiply b by successive bn-limb chunks of a.
		digit* t = s;
		s += 2 * bn;
		std::size_t i = 0;
		while (i < an) {
			std::size_t cn = std::min(bn, an - i);
			if (!i) {
				k_mul(r, a, cn, b, bn, s);
			} else {
				k_mul(t, a + i, cn, b, bn, s);
				digit c = k_add_n(r + i, r + i, t, bn);
				std::copy(t + bn, t + bn + cn, r + i + bn);
				if (c) {
					c = k_add_1(r + i + bn, r + i + bn, cn, c);
					assert(!c); (void)(c);
				}
			}
			i += cn;
		}
	} else if (k_mul_uses_toom4(an, bn)) {
		k_mul_toom4(r, a, an, b, bn, s);
	} else if (k_mul_uses_toom3(an, bn)) {
		k_mul_toom3(r, a, an, b, bn, s);
	} else {
		k_mul_karatsuba(r, a, an, b, bn, s);
	}
}

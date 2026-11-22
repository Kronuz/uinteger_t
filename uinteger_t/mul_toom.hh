// Toom-Cook multiplication: Toom-3 and Toom-4 evaluation, interpolation and products. Included by uinteger_t.hh.

#pragma once

#include "../uinteger_t.hh"

inline unsigned uinteger_t::k_toom3_eval(digit* p1, digit* pm1, digit* pm2, digit* w, const digit* x, std::size_t k, std::size_t x2n) {
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

inline void uinteger_t::k_toom3_interp(digit* r, std::size_t rn, std::size_t k, std::size_t vn, digit* v1, digit* vm1, digit* vm2, bool m1neg, bool m2neg) {
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

inline void uinteger_t::k_mul_toom3(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s) {
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

inline void uinteger_t::k_sqr_toom3(digit* r, const digit* a, std::size_t n, digit* s) {
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

inline unsigned uinteger_t::k_toom4_eval1(digit* p1, digit* pm1, digit* w, const digit* x, std::size_t k, std::size_t x3n) {
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

inline unsigned uinteger_t::k_toom4_eval2(digit* p2, digit* pm2, digit* w, const digit* x, std::size_t k, std::size_t x3n) {
	const std::size_t m = k + 1;
	pm2[k] = k_addlsh_n(pm2, x, x + 2 * k, k, 2);
	w[x3n] = k_lshift(w, x + 3 * k, x3n, 3);
	std::fill(w + x3n + 1, w + m, digit(0));
	w[k] = static_cast<digit>(w[k] + k_addlsh_n(w, w, x + k, k, 1));
	k_add_n(p2, pm2, w, m);
	return static_cast<unsigned>(k_absdiff(pm2, pm2, m, w, m));
}

inline void uinteger_t::k_toom4_evalh(digit* ph, const digit* x, std::size_t k, std::size_t x3n) {
	const std::size_t m = k + 1;
	ph[k] = k_lshift(ph, x, k, 3);
	ph[k] = static_cast<digit>(ph[k] + k_addlsh_n(ph, ph, x + k, k, 2));
	ph[k] = static_cast<digit>(ph[k] + k_addlsh_n(ph, ph, x + 2 * k, k, 1));
	k_add_at(ph, m, 0, x + 3 * k, x3n);
}

inline void uinteger_t::k_toom4_interp(digit* r, std::size_t rn, std::size_t k, std::size_t vn, digit* v1, digit* vm1, digit* v2, digit* vm2, digit* vh, digit* x, bool m1neg, bool m2neg) {
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

inline void uinteger_t::k_mul_toom4(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s) {
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

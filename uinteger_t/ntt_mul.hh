// Multiplication through the transform: size plan, load, pointwise products and the Chinese remainder step. Included by uinteger_t.hh.

#pragma once

#include "../uinteger_t.hh"

inline std::size_t uinteger_t::ntt_words(std::size_t n) {
	return n / ntt_limbs_per_word + (n % ntt_limbs_per_word != 0);
}

inline unsigned long long uinteger_t::ntt_length(unsigned long long cw) {
	const unsigned long long limit = 1ull << ntt_log_max;
	if (cw > limit) {
		return 0;
	}
	unsigned long long two = 1, three = 12;
	while (two < cw) {
		two <<= 1;
	}
	while (three < cw) {
		three <<= 1;
	}
	return three < two && three <= limit ? three : two;
}

inline unsigned long long uinteger_t::ntt_table_words(unsigned long long n) {
	return 2 * (n % 3 ? n / 2 : 2 * (n / 3)) + 2;
}

template <typename Size>
inline bool uinteger_t::_ntt_plan(Size an, Size bn, bool sqr, Size* len, Size* scratch) {
	typedef unsigned long long wide;
	const wide per = ntt_limbs_per_word;
	const wide limit = static_cast<wide>(1) << ntt_log_max;
	const wide max = static_cast<wide>(std::numeric_limits<Size>::max());
	const wide aw = static_cast<wide>(an) / per + (static_cast<wide>(an) % per != 0);
	const wide bw = sqr ? aw : static_cast<wide>(bn) / per + (static_cast<wide>(bn) % per != 0);
	if (aw == 0 || bw == 0 || aw > limit || bw > limit) {
		return false;
	}
	const wide n = ntt_length(aw + bw - 1);
	if (n == 0) {
		return false;
	}
	// 4 (3 for a square) arrays of n words, the table, one word to align.
	const wide bytes = ((sqr ? 3 : 4) * n + ntt_table_words(n) + 1) * 8;
	if (bytes > max / 4) {
		return false;
	}
	*len = static_cast<Size>(n);
	*scratch = static_cast<Size>(bytes / sizeof(digit));
	return true;
}

inline std::size_t uinteger_t::k_ntt_scratch_size(std::size_t an, std::size_t bn, bool sqr) {
	std::size_t len = 0, scratch = 0;
	const bool ok = _ntt_plan<std::size_t>(an, bn, sqr, &len, &scratch);
	assert(ok); (void)(ok);
	return scratch;
}

inline uinteger_t::ntt_word* uinteger_t::ntt_align(digit* s) {
	const auto addr = reinterpret_cast<std::uintptr_t>(s);
	digit* const aligned = s + ((8 - (addr & 7)) & 7) / sizeof(digit);
	assert((reinterpret_cast<std::uintptr_t>(aligned) & 7) == 0);
	return reinterpret_cast<ntt_word*>(aligned);
}

inline void uinteger_t::ntt_load(ntt_word* x, std::size_t n, const digit* a, std::size_t an, const ntt_mod& m) {
	const std::size_t aw = ntt_words(an);
	for (std::size_t i = 0; i < aw; ++i) {
		ntt_word w = 0;
		for (std::size_t q = 0; q < ntt_limbs_per_word && i * ntt_limbs_per_word + q < an; ++q) {
			w |= static_cast<ntt_word>(a[i * ntt_limbs_per_word + q]) << (q * digit_bits);
		}
		// mu = 2^64 / p - e with 0 <= e < 1, so w mu / 2^64 > w / p - 1 and the quotient is at least
		// floor(w / p) - 1: the remainder is below 2p.
		x[i] = static_cast<ntt_word>(w - _mulhi64(w, m.mu) * m.p);
	}
	std::fill(x + aw, x + n, ntt_word(0));
}

inline void uinteger_t::ntt_pointwise(ntt_word* x, const ntt_word* y, std::size_t n, const ntt_mod& m, ntt_word sc) {
	const ntt_word sq = ntt_quot(m, sc);
	for (std::size_t k = 0; k < n; ++k) {
		const ntt_word u = ntt_mulq(x[k], sc, sq, m.p);
		ntt_word lo;
		const ntt_word hi = _mul64(u, y[k], &lo);
		const ntt_word mh = _mulhi64(static_cast<ntt_word>(lo * m.pinv), m.p);
		x[k] = hi < mh ? hi - mh + m.p : hi - mh;
	}
}

inline void uinteger_t::ntt_put(digit* r, std::size_t rn, std::size_t k, ntt_word w) {
	for (std::size_t q = 0; q < ntt_limbs_per_word; ++q) {
		const std::size_t i = k * ntt_limbs_per_word + q;
		if (i < rn) {
			r[i] = static_cast<digit>(w >> (q * digit_bits));
		} else {
			assert((w >> (q * digit_bits)) == 0);
		}
	}
}

inline void uinteger_t::ntt_crt(digit* r, std::size_t rn, const ntt_word* const res[3], std::size_t cw, const ntt_mod mod[3]) {
	const ntt_word p0 = mod[0].p, p1 = mod[1].p, p2 = mod[2].p;
	const ntt_word inv01 = ntt_crt_inv01, c02 = ntt_crt_p0_mod_p2, inv012 = ntt_crt_inv012;
	const ntt_word p01h = ntt_crt_p01_hi, p01l = ntt_crt_p01_lo;
	const ntt_word inv01q = ntt_quot(mod[1], inv01), c02q = ntt_quot(mod[2], c02), inv012q = ntt_quot(mod[2], inv012);
	ntt_word acc0 = 0, acc1 = 0;
	for (std::size_t k = 0; k < cw; ++k) {
		// a0 must be exact; the products by constants below take a1 and a2 unreduced (below 2p).
		const ntt_word a0 = ntt_sub_if(res[0][k], p0), a1 = res[1][k], a2 = res[2][k];
		// t1 = (a1 - a0) / p0 mod p1, then y = a0 + p0 t1 (below p0 p1).
		const ntt_word t1 = ntt_sub_if(ntt_mulq(a1 + p1 - ntt_sub_if(a0, p1), inv01, inv01q, p1), p1);
		ntt_word yl;
		ntt_word yh = _mul64(p0, t1, &yl);
		yl += a0;
		yh += yl < a0;
		// t2 = (a2 - y) / (p0 p1) mod p2, with y mod p2 = a0 + (p0 mod p2) t1.
		const ntt_word e = ntt_sub_if(ntt_mulq(t1, c02, c02q, p2), p2);
		const ntt_word t2 = ntt_sub_if(ntt_mulq(a2 + 2 * p2 - ntt_sub_if(a0, p2) - e, inv012, inv012q, p2), p2);
		// x = y + p0 p1 t2 as three words.
		ntt_word zl, ml;
		const ntt_word zh = _mul64(p01l, t2, &zl);
		const ntt_word mh = _mul64(p01h, t2, &ml);
		const ntt_word w0 = yl + zl;
		ntt_word c = w0 < yl;
		// yh is below 2^60 (y < p0 p1) and zh below 2^62 (t2 < 2^62), so yh + zh does not wrap.
		ntt_word w1 = yh + zh;
		ntt_word c1 = 0;
		w1 += ml;
		c1 += w1 < ml;
		w1 += c;
		c1 += w1 < c;
		const ntt_word w2 = mh + c1;
		// Add x at word k: acc is what earlier words carry into positions k and k + 1.
		const ntt_word s0 = acc0 + w0;
		c = s0 < acc0;
		ntt_word s1 = acc1 + w1;
		c1 = s1 < acc1;
		s1 += c;
		c1 += s1 < c;
		ntt_put(r, rn, k, s0);
		acc0 = s1;
		acc1 = w2 + c1;
	}
	// The product is below 2^(64 ow) with ow <= cw + 1 words, so only acc0 can remain.
	ntt_put(r, rn, cw, acc0);
	assert(acc1 == 0);
}

inline void uinteger_t::k_ntt_mul(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, bool sqr, digit* s) {
	const std::size_t aw = ntt_words(an);
	const std::size_t bw = sqr ? aw : ntt_words(bn);
	const std::size_t cw = aw + bw - 1;
	const std::size_t n = static_cast<std::size_t>(ntt_length(cw));
	// n = 2^lg or 3 2^lg
	const bool three = n % 3 == 0;
	const std::size_t pow2 = three ? n / 3 : n;
	unsigned lg = 0;
	while ((static_cast<std::size_t>(1) << lg) < pow2) {
		++lg;
	}

	ntt_word* res[3];
	res[0] = ntt_align(s);
	res[1] = res[0] + n;
	res[2] = res[1] + n;
	ntt_word* y = res[2] + n;
	ntt_word* tw = y + (sqr ? 0 : n);

	ntt_mod mod[3];
	for (unsigned i = 0; i < 3; ++i) {
		const ntt_mod& m = mod[i] = ntt_make_mod(i);
		// The root of order n: the one of order 3 2^53 (cubed to leave 2^53) squared down to 2^lg.
		ntt_word w = ntt_root(i);
		if (!three) {
			w = ntt_mulmod(m, ntt_mulmod(m, w, w), w);
		}
		for (unsigned j = lg; j < ntt_log_max; ++j) {
			w = ntt_mulmod(m, w, w);
		}
		ntt_table(tw, three ? 2 * pow2 : n / 2, m, w);
		ntt_ctx c = {m.p, m.p2, tw, n, 0, 0};
		if (three) {
			// s = (z - z^2) / 2 for the cube root of unity z = w^(n/3), z^2 = w^(2n/3)
			const ntt_word d = tw[2 * pow2] + m.p - tw[4 * pow2];
			c.s = ntt_sub_if(ntt_half(d, m.p), m.p);
			c.sq = ntt_quot(m, c.s);
		}
		const ntt_word sc = ntt_unscale(m, lg, three);
		ntt_word* x = res[i];
		ntt_load(x, n, a, an, m);
		ntt_forward(x, c);
		if (sqr) {
			ntt_pointwise(x, x, n, m, sc);
		} else {
			ntt_load(y, n, b, bn, m);
			ntt_forward(y, c);
			ntt_pointwise(x, y, n, m, sc);
		}
		ntt_inverse(x, c);
	}
	const ntt_word* const out[3] = {res[0], res[1], res[2]};
	ntt_crt(r, an + (sqr ? an : bn), out, cw, mod);
}

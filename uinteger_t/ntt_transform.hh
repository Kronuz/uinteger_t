// The three-prime number-theoretic transform: primes, 64-bit word products, modular arithmetic and the transforms. Included by uinteger_t.hh.

#pragma once

#include "../uinteger_t.hh"

inline constexpr uinteger_t::ntt_word uinteger_t::ntt_prime(unsigned i) {
	return i == 0 ? 0x3ea0000000000001ull : i == 1 ? 0x3ae0000000000001ull : 0x3960000000000001ull;
}

inline constexpr uinteger_t::ntt_word uinteger_t::ntt_root(unsigned i) {
	return i == 0 ? 0x26bc11b475624031ull : i == 1 ? 0x0238519f4524a89full : 0x368ca63f7d7528a0ull;
}

inline uinteger_t::ntt_word uinteger_t::_mul64(ntt_word x, ntt_word y, ntt_word* lo) {
#if defined HAVE____INT128_T
	auto r = static_cast<__uint128_t>(x) * static_cast<__uint128_t>(y);
	*lo = static_cast<ntt_word>(r);
	return static_cast<ntt_word>(r >> 64);
#elif defined HAVE___UMUL128
	unsigned __int64 h;
	*lo = static_cast<ntt_word>(_umul128(static_cast<unsigned __int64>(x), static_cast<unsigned __int64>(y), &h));
	return static_cast<ntt_word>(h);
#elif defined HAVE___UMULH
	*lo = static_cast<ntt_word>(x * y);
	return static_cast<ntt_word>(__umulh(static_cast<unsigned __int64>(x), static_cast<unsigned __int64>(y)));
#else
	// Four half-word products; `mid` sums three values below 2^32, so it cannot wrap.
	const ntt_word mask = 0xffffffffu;
	const ntt_word x0 = x & mask, x1 = x >> 32;
	const ntt_word y0 = y & mask, y1 = y >> 32;
	const ntt_word p00 = x0 * y0, p01 = x0 * y1, p10 = x1 * y0, p11 = x1 * y1;
	const ntt_word mid = (p00 >> 32) + (p01 & mask) + (p10 & mask);
	*lo = (mid << 32) | (p00 & mask);
	return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
#endif
}

inline uinteger_t::ntt_word uinteger_t::_mulhi64(ntt_word x, ntt_word y) {
	ntt_word lo;
	return _mul64(x, y, &lo);
}

inline uinteger_t::ntt_word uinteger_t::_div64(ntt_word hi, ntt_word lo, ntt_word d, ntt_word* q) {
#if defined HAVE____INT128_T
	auto x = static_cast<__uint128_t>(hi) << 64 | static_cast<__uint128_t>(lo);
	*q = static_cast<ntt_word>(x / d);
	return static_cast<ntt_word>(x % d);
#elif defined HAVE___UDIV128
	unsigned __int64 r;
	*q = static_cast<ntt_word>(_udiv128(static_cast<unsigned __int64>(hi), static_cast<unsigned __int64>(lo), static_cast<unsigned __int64>(d), &r));
	return static_cast<ntt_word>(r);
#else
	// Restoring division, one quotient bit per step; a set top bit of the remainder means the shifted
	// remainder is at least 2^64 > d.
	ntt_word r = hi, quot = 0;
	for (int i = 63; i >= 0; --i) {
		const bool top = (r >> 63) != 0;
		r = (r << 1) | ((lo >> i) & 1);
		quot <<= 1;
		if (top || r >= d) {
			r -= d;
			quot |= 1;
		}
	}
	*q = quot;
	return r;
#endif
}

inline uinteger_t::ntt_mod uinteger_t::ntt_make_mod(unsigned i) {
	ntt_mod m;
	m.p = ntt_prime(i);
	m.p2 = 2 * m.p;
	_div64(1, 0, m.p, &m.mu);
	const ntt_word d = m.p << 2;
	_div64(static_cast<ntt_word>(~d), ~static_cast<ntt_word>(0), d, &m.v);
	m.r = static_cast<ntt_word>(0 - m.mu * m.p);
	// Newton's iteration for the inverse mod 2^64: p = 1 mod 2^53 makes p its own inverse to 53 bits
	// (p p = 1 mod 2^54), and one step squares the error.
	m.pinv = static_cast<ntt_word>(m.p * (2 - m.p * m.p));
	return m;
}

inline uinteger_t::ntt_word uinteger_t::ntt_sub_if(ntt_word x, ntt_word m) {
	return x >= m ? x - m : x;
}

inline uinteger_t::ntt_word uinteger_t::ntt_quot(const ntt_mod& m, ntt_word w) {
	const ntt_word d = m.p << 2;
	const ntt_word u1 = w << 2;
	ntt_word q0;
	ntt_word q1 = _mul64(m.v, u1, &q0);
	q1 += u1;
	q1 += 1;
	ntt_word r = static_cast<ntt_word>(0 - q1 * d);
	if (r > q0) {
		q1 -= 1;
		r += d;
	}
	if (r >= d) {
		q1 += 1;
	}
	return q1;
}

inline uinteger_t::ntt_word uinteger_t::ntt_mulq(ntt_word x, ntt_word w, ntt_word wq, ntt_word p) {
	return static_cast<ntt_word>(x * w - _mulhi64(x, wq) * p);
}

inline uinteger_t::ntt_word uinteger_t::ntt_mulmod(const ntt_mod& m, ntt_word x, ntt_word y) {
	return ntt_sub_if(ntt_mulq(x, y, ntt_quot(m, y), m.p), m.p);
}

inline uinteger_t::ntt_word uinteger_t::ntt_unscale(const ntt_mod& m, unsigned lg, bool three) {
	ntt_word x = m.r;
	for (unsigned j = 0; j < lg; ++j) {
		x = (x & 1) ? (x + m.p) >> 1 : x >> 1;
	}
	// 1/3 = (2p + 1) / 3 mod p, since p = 1 mod 3
	return three ? ntt_mulmod(m, x, (2 * m.p + 1) / 3) : x;
}

inline uinteger_t::ntt_word uinteger_t::ntt_half(ntt_word x, ntt_word p) {
	return (x & 1) ? (x + p) >> 1 : x >> 1;
}

inline void uinteger_t::ntt_table(ntt_word* tw, std::size_t last, const ntt_mod& m, ntt_word w) {
	tw[0] = 1;
	for (std::size_t k = 1; k <= last; k <<= 1) {
		const ntt_word wk = k == 1 ? w : ntt_mulmod(m, tw[k], tw[k]);
		tw[2 * k] = wk;
		const ntt_word wq = ntt_quot(m, wk);
		const std::size_t end = std::min(2 * k, last + 1);
		for (std::size_t j = k + 1; j < end; ++j) {
			tw[2 * j] = ntt_sub_if(ntt_mulq(tw[2 * (j - k)], wk, wq, m.p), m.p);
		}
	}
	for (std::size_t j = 0; j <= last; ++j) {
		tw[2 * j + 1] = ntt_quot(m, tw[2 * j]);
	}
}

inline void uinteger_t::ntt_fwd_layer(ntt_word* x, std::size_t h, const ntt_ctx& c, std::size_t step) {
	const ntt_word p = c.p, p2 = c.p2;
	for (std::size_t t = 0; t < h; ++t) {
		const ntt_word* w = c.tw + 2 * step * t;
		const ntt_word u = x[t], v = x[t + h];
		const ntt_word d = u - v + p2;
		x[t] = ntt_sub_if(u + v, p2);
		x[t + h] = ntt_mulq(d, w[0], w[1], p);
	}
}

inline void uinteger_t::ntt_inv_layer(ntt_word* x, std::size_t h, const ntt_ctx& c, std::size_t step) {
	const ntt_word p = c.p, p2 = c.p2;
	for (std::size_t t = 0; t < h; ++t) {
		const ntt_word* w = c.tw + 2 * step * (h - t);
		const ntt_word u = x[t];
		const ntt_word v = ntt_mulq(x[t + h], w[0], w[1], p);
		x[t] = ntt_sub_if(u - v + p2, p2);
		x[t + h] = ntt_sub_if(u + v, p2);
	}
}

inline void uinteger_t::ntt_fwd_last(ntt_word* x, std::size_t n, const ntt_ctx& c) {
	const ntt_word p = c.p, p2 = c.p2;
	const ntt_word w4 = c.tw[2 * (c.n / 4)], q4 = c.tw[2 * (c.n / 4) + 1];
	for (std::size_t i = 0; i < n; i += 4) {
		const ntt_word u0 = x[i], u1 = x[i + 1], u2 = x[i + 2], u3 = x[i + 3];
		const ntt_word a0 = ntt_sub_if(u0 + u2, p2), b0 = ntt_sub_if(u0 - u2 + p2, p2);
		const ntt_word a1 = ntt_sub_if(u1 + u3, p2), b1 = ntt_mulq(u1 - u3 + p2, w4, q4, p);
		x[i] = ntt_sub_if(a0 + a1, p2);
		x[i + 1] = ntt_sub_if(a0 - a1 + p2, p2);
		x[i + 2] = ntt_sub_if(b0 + b1, p2);
		x[i + 3] = ntt_sub_if(b0 - b1 + p2, p2);
	}
}

inline void uinteger_t::ntt_inv_first(ntt_word* x, std::size_t n, const ntt_ctx& c) {
	const ntt_word p = c.p, p2 = c.p2;
	const ntt_word w4 = c.tw[2 * (c.n / 4)], q4 = c.tw[2 * (c.n / 4) + 1];
	for (std::size_t i = 0; i < n; i += 4) {
		const ntt_word y0 = x[i], y1 = x[i + 1], y2 = x[i + 2], y3 = x[i + 3];
		const ntt_word a0 = ntt_sub_if(y0 + y1, p2), a1 = ntt_sub_if(y0 - y1 + p2, p2);
		const ntt_word b0 = ntt_sub_if(y2 + y3, p2), b1 = ntt_sub_if(y2 - y3 + p2, p2);
		const ntt_word v = ntt_mulq(b1, w4, q4, p);
		x[i] = ntt_sub_if(a0 + b0, p2);
		x[i + 1] = ntt_sub_if(a1 - v + p2, p2);
		x[i + 2] = ntt_sub_if(a0 - b0 + p2, p2);
		x[i + 3] = ntt_sub_if(a1 + v, p2);
	}
}

inline void uinteger_t::ntt_fwd_leaf(ntt_word* x, std::size_t n, const ntt_ctx& c) {
	if (n == 1) {
		return;
	}
	if (n == 2) {
		const ntt_word u = x[0], v = x[1];
		x[0] = ntt_sub_if(u + v, c.p2);
		x[1] = ntt_sub_if(u - v + c.p2, c.p2);
		return;
	}
	for (std::size_t h = n / 2; h >= 4; h >>= 1) {
		for (std::size_t b = 0; b < n; b += 2 * h) {
			ntt_fwd_layer(x + b, h, c, c.n / (2 * h));
		}
	}
	ntt_fwd_last(x, n, c);
}

inline void uinteger_t::ntt_inv_leaf(ntt_word* x, std::size_t n, const ntt_ctx& c) {
	if (n == 1) {
		return;
	}
	if (n == 2) {
		const ntt_word u = x[0], v = x[1];
		x[0] = ntt_sub_if(u + v, c.p2);
		x[1] = ntt_sub_if(u - v + c.p2, c.p2);
		return;
	}
	ntt_inv_first(x, n, c);
	for (std::size_t h = 4; h < n; h <<= 1) {
		for (std::size_t b = 0; b < n; b += 2 * h) {
			ntt_inv_layer(x + b, h, c, c.n / (2 * h));
		}
	}
}

inline void uinteger_t::ntt_fwd(ntt_word* x, std::size_t n, const ntt_ctx& c) {
	if (n <= ntt_leaf) {
		ntt_fwd_leaf(x, n, c);
		return;
	}
	const std::size_t h = n / 2;
	ntt_fwd_layer(x, h, c, c.n / n);
	ntt_fwd(x, h, c);
	ntt_fwd(x + h, h, c);
}

inline void uinteger_t::ntt_inv(ntt_word* x, std::size_t n, const ntt_ctx& c) {
	if (n <= ntt_leaf) {
		ntt_inv_leaf(x, n, c);
		return;
	}
	const std::size_t h = n / 2;
	ntt_inv(x, h, c);
	ntt_inv(x + h, h, c);
	ntt_inv_layer(x, h, c, c.n / n);
}

inline void uinteger_t::ntt_fwd3_layer(ntt_word* x, const ntt_ctx& c) {
	const ntt_word p = c.p, p2 = c.p2;
	const std::size_t m = c.n / 3;
	for (std::size_t t = 0; t < m; ++t) {
		const ntt_word a = x[t], b = x[t + m], d = x[t + 2 * m];
		const ntt_word sum = ntt_sub_if(b + d, p2);
		const ntt_word u = ntt_sub_if(a + p2 - ntt_half(sum, p), p2);
		const ntt_word v = ntt_mulq(b - d + p2, c.s, c.sq, p);
		const ntt_word* w1 = c.tw + 2 * t;
		const ntt_word* w2 = c.tw + 4 * t;
		x[t] = ntt_sub_if(a + sum, p2);
		x[t + m] = ntt_mulq(u + v, w1[0], w1[1], p);
		x[t + 2 * m] = ntt_mulq(u - v + p2, w2[0], w2[1], p);
	}
}

inline void uinteger_t::ntt_inv3_layer(ntt_word* x, const ntt_ctx& c) {
	const ntt_word p = c.p, p2 = c.p2;
	const std::size_t m = c.n / 3;
	const std::size_t half = c.n / 2;
	for (std::size_t t = 0; t < m; ++t) {
		const ntt_word z0 = x[t], z1 = x[t + m], z2 = x[t + 2 * m];
		const ntt_word* e1 = c.tw + 2 * (half - t);
		const ntt_word v1 = ntt_mulq(z1, p - e1[0], static_cast<ntt_word>(~e1[1]), p);
		ntt_word v2;
		if (2 * t <= half) {
			const ntt_word* e2 = c.tw + 2 * (half - 2 * t);
			v2 = ntt_mulq(z2, p - e2[0], static_cast<ntt_word>(~e2[1]), p);
		} else {
			const ntt_word* e2 = c.tw + 2 * (c.n - 2 * t);
			v2 = ntt_mulq(z2, e2[0], e2[1], p);
		}
		const ntt_word sum = ntt_sub_if(v1 + v2, p2);
		const ntt_word u = ntt_sub_if(z0 + p2 - ntt_half(sum, p), p2);
		const ntt_word w = ntt_mulq(v1 - v2 + p2, c.s, c.sq, p);
		x[t] = ntt_sub_if(z0 + sum, p2);
		x[t + m] = ntt_sub_if(u - w + p2, p2);
		x[t + 2 * m] = ntt_sub_if(u + w, p2);
	}
}

inline void uinteger_t::ntt_forward(ntt_word* x, const ntt_ctx& c) {
	if (c.n % 3) {
		ntt_fwd(x, c.n, c);
		return;
	}
	const std::size_t m = c.n / 3;
	ntt_fwd3_layer(x, c);
	for (std::size_t j = 0; j < 3; ++j) {
		ntt_fwd(x + j * m, m, c);
	}
}

inline void uinteger_t::ntt_inverse(ntt_word* x, const ntt_ctx& c) {
	if (c.n % 3) {
		ntt_inv(x, c.n, c);
		return;
	}
	const std::size_t m = c.n / 3;
	for (std::size_t j = 0; j < 3; ++j) {
		ntt_inv(x + j * m, m, c);
	}
	ntt_inv3_layer(x, c);
}

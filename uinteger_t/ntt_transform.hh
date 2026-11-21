// The three-prime number-theoretic transform: primes, modular products and the transform. Included inside class uinteger_t by uinteger_t.hh.

	// Transform multiplication. The operands are read as polynomials in 2^64 whose coefficients are 64-bit
	// words (one limb at 64-bit limbs, 64 / digit_bits limbs packed at narrower ones). The product's
	// coefficients are sums of at most min(aw, bw) products of two words, below N 2^128 for a transform of
	// N points, and three primes of 62 bits multiply to more than 2^185, so the cyclic convolutions of
	// length N modulo the three primes determine them exactly, and the Chinese remainder theorem
	// (Garner's form) gives them back (Pollard, "The fast Fourier transform in a finite field", Math.
	// Comp. 25, 1971; Knuth, TAOCP vol. 2, 4.3.3; Crandall and Pomerance, Prime Numbers, 9.5). Each prime
	// is c 2^53 + 1 with c divisible by 3, so N can be a power of two or three times one, up to 2^53: the
	// lengths 2^k and 3 2^(k-1) keep the padding of a product within a third of its size.
	//
	// Modular products by a constant are Shoup's: with the quotient floor(w 2^64 / p) stored next to w,
	// x w mod p takes one high product and two low ones and lands in [0, 2p) (Shoup, "New algorithms for
	// finding irreducible polynomials over finite fields", Math. Comp. 54, 1990; Harvey, "Faster arithmetic
	// for number-theoretic transforms", J. Symbolic Comput. 60, 2014). Since 4p < 2^64 the butterflies keep
	// their values in [0, 2p) with one conditional subtraction per sum, and reduce fully only at the end.
	// The transform is decimation in frequency from natural to bit-reversed order, the inverse
	// decimation in time back, so the pointwise product needs no permutation. A length 3M is one radix-3
	// layer (the three residues modulo x^M - z^j, z a cube root of unity, each twisted into a cyclic
	// convolution) followed by three transforms of length M.
	using ntt_word = std::uint64_t;

	static constexpr unsigned ntt_log_max = 53;

	// c 2^53 + 1 with c = 501, 471, 459 (each a multiple of 3), all prime and between 2^61 and 2^62.
	static constexpr ntt_word ntt_prime(unsigned i) {
		return i == 0 ? 0x3ea0000000000001ull : i == 1 ? 0x3ae0000000000001ull : 0x3960000000000001ull;
	}

	// A primitive 3 2^53-th root of unity modulo ntt_prime(i): x^(c / 3) for the least x that is neither a
	// square nor a cube (7, 11 and 7) and c = (p - 1) / 2^53.
	static constexpr ntt_word ntt_root(unsigned i) {
		return i == 0 ? 0x26bc11b475624031ull : i == 1 ? 0x0238519f4524a89full : 0x368ca63f7d7528a0ull;
	}

	// The cutoff in words under which a block is transformed layer by layer inside the cache; larger
	// blocks split into halves after one pass over them.
#ifdef UINTEGER_T_NTT_LEAF
	static constexpr std::size_t ntt_leaf = UINTEGER_T_NTT_LEAF;
#else
	static constexpr std::size_t ntt_leaf = 2048;
#endif
	static_assert(ntt_leaf >= 4 && (ntt_leaf & (ntt_leaf - 1)) == 0, "the transform leaf size is a power of two of at least 4 words");

	// 64 x 64 -> 128 bits, independent of the limb width (the transform words are 64 bits at every width).
	static ntt_word _mul64(ntt_word x, ntt_word y, ntt_word* lo) {
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

	static ntt_word _mulhi64(ntt_word x, ntt_word y) {
		ntt_word lo;
		return _mul64(x, y, &lo);
	}

	// (hi, lo) / d for hi < d: the quotient through *q, the remainder returned.
	static ntt_word _div64(ntt_word hi, ntt_word lo, ntt_word d, ntt_word* q) {
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

	// A prime and what its arithmetic needs.
	struct ntt_mod {
		ntt_word p;      // the prime
		ntt_word p2;     // 2p
		ntt_word mu;     // floor(2^64 / p), reduces a word to [0, 2p)
		ntt_word pinv;   // p^-1 mod 2^64, for Montgomery reduction
		ntt_word v;      // reciprocal of p << 2 (Moller and Granlund), for Shoup quotients
		ntt_word r;      // 2^64 mod p
	};

	static ntt_mod ntt_make_mod(unsigned i) {
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

	static ntt_word ntt_sub_if(ntt_word x, ntt_word m) {
		return x >= m ? x - m : x;
	}

	// floor(w 2^64 / p) for w < p, Shoup's quotient of w: a 2-by-1 division of w 2^64 by the normalized
	// d = p << 2 with its reciprocal (Moller and Granlund, "Improved division by invariant integers",
	// IEEE Trans. Computers 60(2), 2011, Algorithm 4, with the low dividend word zero).
	static ntt_word ntt_quot(const ntt_mod& m, ntt_word w) {
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

	// x w mod p in [0, 2p), for any word x and w < p with its quotient wq.
	static ntt_word ntt_mulq(ntt_word x, ntt_word w, ntt_word wq, ntt_word p) {
		return static_cast<ntt_word>(x * w - _mulhi64(x, wq) * p);
	}

	// x y mod p in [0, p) for x < 2^64 and y < p (the slow way, for constants).
	static ntt_word ntt_mulmod(const ntt_mod& m, ntt_word x, ntt_word y) {
		return ntt_sub_if(ntt_mulq(x, y, ntt_quot(m, y), m.p), m.p);
	}

	// 2^64 / n mod p for n = 2^lg, or 3 2^lg when `three`: the factor that turns a Montgomery product into
	// a product scaled by 1/n.
	static ntt_word ntt_unscale(const ntt_mod& m, unsigned lg, bool three) {
		ntt_word x = m.r;
		for (unsigned j = 0; j < lg; ++j) {
			x = (x & 1) ? (x + m.p) >> 1 : x >> 1;
		}
		// 1/3 = (2p + 1) / 3 mod p, since p = 1 mod 3
		return three ? ntt_mulmod(m, x, (2 * m.p + 1) / 3) : x;
	}

	// x / 2 mod p for x < 2p, below 1.5p.
	static ntt_word ntt_half(ntt_word x, ntt_word p) {
		return (x & 1) ? (x + p) >> 1 : x >> 1;
	}

	// The powers of the primitive n-th root w: tw[2t] = w^t and tw[2t + 1] its Shoup quotient, for t = 0..last
	// (w^(n/2) = -1 among them). The block of powers k..2k - 1 is the block 0..k - 1 times w^k, which keeps
	// the products independent of one another.
	static void ntt_table(ntt_word* tw, std::size_t last, const ntt_mod& m, ntt_word w) {
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

	// What the layers of one transform share: the prime and the table of powers of the primitive n-th root,
	// n the full length (a power of two, or three times one: then the table runs to 2n / 3 and s is
	// (z - z^2) / 2 for the cube root of unity z, with its quotient sq).
	struct ntt_ctx {
		ntt_word p;
		ntt_word p2;
		const ntt_word* tw;
		std::size_t n;
		ntt_word s;
		ntt_word sq;
	};

	// One layer of the forward transform over x[0..2h): (u, v) -> (u + v, (u - v) w^t), t = 0..h - 1,
	// with the twiddles w^t read from the table at a step of `step` entries. Values stay in [0, 2p).
	static void ntt_fwd_layer(ntt_word* x, std::size_t h, const ntt_ctx& c, std::size_t step) {
		const ntt_word p = c.p, p2 = c.p2;
		for (std::size_t t = 0; t < h; ++t) {
			const ntt_word* w = c.tw + 2 * step * t;
			const ntt_word u = x[t], v = x[t + h];
			const ntt_word d = u - v + p2;
			x[t] = ntt_sub_if(u + v, p2);
			x[t + h] = ntt_mulq(d, w[0], w[1], p);
		}
	}

	// One layer of the inverse transform: (u, v) -> (u - v w~, u + v w~) with w~ = w^(h - t) = -w^-t, so
	// the inverse twiddles come out of the same table, read backwards.
	static void ntt_inv_layer(ntt_word* x, std::size_t h, const ntt_ctx& c, std::size_t step) {
		const ntt_word p = c.p, p2 = c.p2;
		for (std::size_t t = 0; t < h; ++t) {
			const ntt_word* w = c.tw + 2 * step * (h - t);
			const ntt_word u = x[t];
			const ntt_word v = ntt_mulq(x[t + h], w[0], w[1], p);
			x[t] = ntt_sub_if(u - v + p2, p2);
			x[t + h] = ntt_sub_if(u + v, p2);
		}
	}

	// The last two forward layers (blocks of 4: the twiddles are 1 and w4, the 4th root of unity).
	static void ntt_fwd_last(ntt_word* x, std::size_t n, const ntt_ctx& c) {
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

	// The first two inverse layers, undoing ntt_fwd_last.
	static void ntt_inv_first(ntt_word* x, std::size_t n, const ntt_ctx& c) {
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

	// Forward transform of the n <= ntt_leaf words at x (n a power of two, at most the table's length):
	// layer by layer, the whole block staying in the cache.
	static void ntt_fwd_leaf(ntt_word* x, std::size_t n, const ntt_ctx& c) {
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

	static void ntt_inv_leaf(ntt_word* x, std::size_t n, const ntt_ctx& c) {
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

	// Natural order in, bit-reversed order out: x(w^t) for t in bit-reversed order, for the n words at x.
	static void ntt_fwd(ntt_word* x, std::size_t n, const ntt_ctx& c) {
		if (n <= ntt_leaf) {
			ntt_fwd_leaf(x, n, c);
			return;
		}
		const std::size_t h = n / 2;
		ntt_fwd_layer(x, h, c, c.n / n);
		ntt_fwd(x, h, c);
		ntt_fwd(x + h, h, c);
	}

	// Bit-reversed order in, natural order out, scaled by n.
	static void ntt_inv(ntt_word* x, std::size_t n, const ntt_ctx& c) {
		if (n <= ntt_leaf) {
			ntt_inv_leaf(x, n, c);
			return;
		}
		const std::size_t h = n / 2;
		ntt_inv(x, h, c);
		ntt_inv(x + h, h, c);
		ntt_inv_layer(x, h, c, c.n / n);
	}

	// The radix-3 layer for a length 3M, M = c.n / 3 (a multiple of 4): (a, b, d) = x[t], x[t + M], x[t + 2M]
	// become a + b + d, (a + z b + z^2 d) w^t and (a + z^2 b + z d) w^2t, with z = w^M the cube root of unity.
	// With 1 + z + z^2 = 0 the two sums are u +- s (b - d) for u = a - (b + d) / 2 and s = (z - z^2) / 2, so
	// the layer takes three products per three words.
	static void ntt_fwd3_layer(ntt_word* x, const ntt_ctx& c) {
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

	// The inverse of ntt_fwd3_layer, scaled by 3: the twists by w^-t and w^-2t first, which are -w^(n/2 - t)
	// and -w^(n/2 - 2t) (w^(n - 2t) once 2t passes n/2); the negated twiddle (p - w) has the quotient ~q.
	static void ntt_inv3_layer(ntt_word* x, const ntt_ctx& c) {
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

	// The whole transform of c.n words at x, whatever the length: bit-reversed order within each of the
	// three blocks after the radix-3 layer of a length 3M.
	static void ntt_forward(ntt_word* x, const ntt_ctx& c) {
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

	// The inverse, scaled by c.n.
	static void ntt_inverse(ntt_word* x, const ntt_ctx& c) {
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

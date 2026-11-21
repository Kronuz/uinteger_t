// Base conversion: digit emission, splitters and the encoding kernels. Included inside class uinteger_t by uinteger_t.hh.

	// The m digits of a piece v < base^m (base^m < 2^32), most significant first. f = v *
	// ceil(2^64 / base^m) is the fraction v / base^m to within base^-m, so each umulh(f, base)
	// is an exact digit (Lemire, Kaser and Kurz, "Faster remainder by direct computation",
	// 2019); two digits per step through the pair table when there is one.
	static void _emit_digits(const radix& rx, std::uint64_t v, unsigned m, unsigned char* out) {
		std::uint64_t f = v * rx.cinv[m];
		const std::uint64_t b = static_cast<std::uint64_t>(rx.base);
		if (const unsigned char* pair = rx.pair) {
			const std::uint64_t bb = b * b;
			if (m & 1) {
				*out++ = static_cast<unsigned char>(_umulh(f, b));
				f *= b;
			}
			for (unsigned i = m / 2; i--;) {
				std::memcpy(out, pair + 2 * _umulh(f, bb), 2);
				out += 2;
				f *= bb;
			}
		} else {
			for (unsigned i = 0; i < m; ++i) {
				out[i] = static_cast<unsigned char>(_umulh(f, b));
				f *= b;
			}
		}
	}

	// The k digits of a chunk r < D: pieces of h digits (at most three, k <= 2h + 1).
	static void _split_chunk_general(const radix& rx, digit r, unsigned char* out) {
		if (rx.k <= rx.h) {
			_emit_digits(rx, r, rx.k, out);
			return;
		}
		const std::uint64_t c = r;
		std::uint64_t q = rx.div0.ok ? _div_magic(rx.div0, c) : c / rx.bh;
		_emit_digits(rx, c - q * rx.bh, rx.h, out + rx.k - rx.h);
		unsigned rem = rx.k - rx.h;
		if (rem > rx.h) {
			const std::uint64_t q2 = rx.div1.ok ? _div_magic(rx.div1, q) : q / rx.bh;
			_emit_digits(rx, q - q2 * rx.bh, rx.h, out + rem - rx.h);
			rem -= rx.h;
			q = q2;
		}
		_emit_digits(rx, q, rem, out);
	}

	// The same for one shape (K digits in pieces of H, K > H, K <= 2H + 1) fixed at compile
	// time: the digit counts are constants, so each piece unrolls into a straight run of
	// multiplies and table loads with no loop or count test left to resolve at run time.
	template <unsigned M>
	static void _emit_digits_n(const radix& rx, std::uint64_t v, unsigned char* out) {
		if (M == 1) {
			out[0] = static_cast<unsigned char>(v);  // v < base
			return;
		}
		std::uint64_t f = v * rx.cinv[M];
		const std::uint64_t b = static_cast<std::uint64_t>(rx.base);
		if (const unsigned char* pair = rx.pair) {
			const std::uint64_t bb = b * b;
			if (M & 1) {
				*out++ = static_cast<unsigned char>(_umulh(f, b));
				f *= b;
			}
			for (unsigned i = 0; i < M / 2; ++i) {
				std::memcpy(out + 2 * i, pair + 2 * _umulh(f, bb), 2);
				f *= bb;
			}
		} else {
			for (unsigned i = 0; i < M; ++i) {
				out[i] = static_cast<unsigned char>(_umulh(f, b));
				f *= b;
			}
		}
	}

	template <unsigned K, unsigned H>
	static void _split_chunk_kh(const radix& rx, digit r, unsigned char* out) {
		static_assert(K > H && K <= 2 * H + 1, "a chunk of two or three pieces");
		const std::uint64_t c = r;
		std::uint64_t q = rx.div0.ok ? _div_magic(rx.div0, c) : c / rx.bh;
		_emit_digits_n<H>(rx, c - q * rx.bh, out + (K - H));
		if (K - H > H) {
			const std::uint64_t q2 = rx.div1.ok ? _div_magic(rx.div1, q) : q / rx.bh;
			_emit_digits_n<H>(rx, q - q2 * rx.bh, out + (K - 2 * H));
			q = q2;
		}
		_emit_digits_n<(K - H > H ? K - 2 * H : K - H)>(rx, q, out);
	}

#ifndef UINTEGER_T_SPLIT_SHAPES
#define UINTEGER_T_SPLIT_SHAPES 1
#endif
	// The splitter for chunks of k digits in pieces of h: one of the shapes that the bases 3 to 256
	// (not powers of two) take at 64-bit limbs, else the general one.
	static void (*_pick_split(unsigned k, unsigned h))(const radix&, digit, unsigned char*) {
#if UINTEGER_T_SPLIT_SHAPES
		switch (k * 256 + h) {
		case 8 * 256 + 4: return &_split_chunk_kh<8, 4>;
		case 9 * 256 + 4: return &_split_chunk_kh<9, 4>;
		case 10 * 256 + 5: return &_split_chunk_kh<10, 5>;
		case 11 * 256 + 5: return &_split_chunk_kh<11, 5>;
		case 12 * 256 + 6: return &_split_chunk_kh<12, 6>;
		case 13 * 256 + 6: return &_split_chunk_kh<13, 6>;
		case 14 * 256 + 7: return &_split_chunk_kh<14, 7>;
		case 15 * 256 + 7: return &_split_chunk_kh<15, 7>;
		case 16 * 256 + 8: return &_split_chunk_kh<16, 8>;
		case 17 * 256 + 8: return &_split_chunk_kh<17, 8>;
		case 18 * 256 + 9: return &_split_chunk_kh<18, 9>;
		case 19 * 256 + 9: return &_split_chunk_kh<19, 9>;
		case 20 * 256 + 10: return &_split_chunk_kh<20, 10>;
		case 22 * 256 + 11: return &_split_chunk_kh<22, 11>;
		case 24 * 256 + 12: return &_split_chunk_kh<24, 12>;
		case 27 * 256 + 13: return &_split_chunk_kh<27, 13>;
		case 40 * 256 + 20: return &_split_chunk_kh<40, 20>;
		default: break;
		}
#endif
		(void)k; (void)h;
		return &_split_chunk_general;
	}

	static void _split_chunk(const radix& rx, digit r, unsigned char* out) {
		rx.split(rx, r, out);
	}

	static std::size_t _strip_zeros(unsigned char* out, std::size_t total) {
		std::size_t z = 0;
		while (z + 1 < total && !out[z]) {
			++z;
		}
		if (z) {
			std::memmove(out, out + z, total - z);
		}
		return total - z;
	}

#if defined __clang__
#  define UINTEGER_T_UNROLL _Pragma("clang loop unroll(full)")
#elif defined __GNUC__ && __GNUC__ >= 8
#  define UINTEGER_T_UNROLL _Pragma("GCC unroll 16")
#else
#  define UINTEGER_T_UNROLL
#endif

	// N limbs, K chunks, G guard limbs and R of L + X limbs, all fixed: the fraction lives in
	// registers. The short product keeps only the partial products that reach limb N - 1.
	template <int N, int K, int G, int X>
	static std::size_t _enc_fixed(const radix& rx, const radix_table& t, const digit* x, unsigned char* out) {
		constexpr int L = K + G;
		constexpr int RL = L + X;
		digit W[N + RL];
		const digit* R = t.R;
		const digit* R2 = t.R2;
		UINTEGER_T_UNROLL
		for (int l = 0; l < N + RL; ++l) {
			W[l] = (l >= N - 1 && l < RL) ? R2[l] : 0;
		}
		UINTEGER_T_UNROLL
		for (int i = 0; i < N; ++i) {
			digit carry = 0;
			const digit xi = x[i];
			UINTEGER_T_UNROLL
			for (int l = (N - 1 - i > 0 ? N - 1 - i : 0); l < RL; ++l) {
				carry = _multadd(xi, R[l], W[i + l], carry, &W[i + l]);
			}
			W[i + RL] += carry;
		}
		digit* Y = W + N;
		const digit D = rx.D;
		digit ch[K];
		// Two passes per sweep: pass j + 1 takes pass j's output limb as it is produced.
		UINTEGER_T_UNROLL
		for (int j = 0; j + 1 < K; j += 2) {
			digit lo;
			digit c0 = _multadd(Y[j], D, 0, 0, &lo);
			digit c1 = 0;
			UINTEGER_T_UNROLL
			for (int i = j + 1; i < L; ++i) {
				c0 = _multadd(Y[i], D, c0, 0, &lo);
				c1 = _multadd(lo, D, c1, 0, &Y[i]);
			}
			ch[j] = c0;
			ch[j + 1] = c1;
		}
		if (K & 1) {
			digit carry = 0;
			UINTEGER_T_UNROLL
			for (int i = K - 1; i < L; ++i) {
				carry = _multadd(Y[i], D, carry, 0, &Y[i]);
			}
			ch[K - 1] = carry;
		}
		UINTEGER_T_UNROLL
		for (int j = 0; j < K; ++j) {
			_split_chunk(rx, ch[j], out + static_cast<std::size_t>(j) * rx.k);
		}
		return _strip_zeros(out, static_cast<std::size_t>(K) * rx.k);
	}

#undef UINTEGER_T_UNROLL

	template <int N, int E>
	static std::size_t _enc_fixed_k(const radix& rx, const radix_table& t, const digit* x, unsigned char* out) {
		switch (t.guard * 2 + (t.rlen - t.fraction)) {
		case 0: return _enc_fixed<N, N + E, 0, 0>(rx, t, x, out);
		case 1: return _enc_fixed<N, N + E, 0, 1>(rx, t, x, out);
		case 2: return _enc_fixed<N, N + E, 1, 0>(rx, t, x, out);
		default: return _enc_fixed<N, N + E, 1, 1>(rx, t, x, out);
		}
	}

	template <int N>
	static std::size_t _enc_fixed_n(const radix& rx, const radix_table& t, const digit* x, unsigned char* out) {
		switch (t.chunks - N) {
		case 0: return _enc_fixed_k<N, 0>(rx, t, x, out);
		case 1: return _enc_fixed_k<N, 1>(rx, t, x, out);
		default: return _enc_fixed_k<N, 2>(rx, t, x, out);
		}
	}

	// Any size up to radix_max_limbs: a four-row short product (row r lagged by r limbs so the
	// four rows add into the same limb) and four fused passes per sweep.
	static std::size_t _enc_general(const radix& rx, const radix_table& t, const digit* x, std::size_t n, unsigned char* out) {
		const std::size_t L = t.fraction, RL = t.rlen, K = t.chunks;
		digit W[radix_work];
		const digit* R = t.R;
		std::fill(W, W + n + RL + 8, static_cast<digit>(0));
		const std::size_t l0 = n - 1 < RL ? n - 1 : RL;
		std::copy(t.R2 + l0, t.R2 + RL, W + l0);
		std::size_t i0 = 0;
		for (; i0 + 4 <= n; i0 += 4) {
			const digit x0 = x[i0], x1 = x[i0 + 1], x2 = x[i0 + 2], x3 = x[i0 + 3];
			digit c0 = 0, c1 = 0, c2 = 0, c3 = 0, lo;
			const std::size_t pstart = n - 1 > i0 ? n - 1 : i0;
			if (pstart > i0 + 3 + RL) {
				continue;
			}
			for (std::size_t p = pstart, pend = i0 + RL + 4; p <= pend; ++p) {
				const digit* Rl = R + (p - i0);
				c0 = _multadd(x0, Rl[0], W[p], c0, &lo);
				c1 = _multadd(x1, Rl[-1], lo, c1, &lo);
				c2 = _multadd(x2, Rl[-2], lo, c2, &lo);
				c3 = _multadd(x3, Rl[-3], lo, c3, &lo);
				W[p] = lo;
			}
		}
		for (; i0 < n; ++i0) {
			const std::size_t s0 = i0 + 1 >= n ? 0 : n - 1 - i0;
			if (s0 >= RL) {
				continue;
			}
			digit carry = 0;
			for (std::size_t l = s0; l < RL; ++l) {
				carry = _multadd(x[i0], R[l], W[i0 + l], carry, &W[i0 + l]);
			}
			for (std::size_t p = i0 + RL; carry; ++p) {
				W[p] += carry;
				carry = W[p] < carry;
			}
		}
		digit* Y = W + n;
		const digit D = rx.D;
		unsigned char* o = out;
		std::size_t j = 0;
		for (; j + 4 <= K; j += 4) {
			digit lo, c0, c1, c2, c3;
			c0 = _multadd(Y[j], D, 0, 0, &lo);
			c0 = _multadd(Y[j + 1], D, c0, 0, &lo);
			c1 = _multadd(lo, D, 0, 0, &lo);
			c0 = _multadd(Y[j + 2], D, c0, 0, &lo);
			c1 = _multadd(lo, D, c1, 0, &lo);
			c2 = _multadd(lo, D, 0, 0, &lo);
			c3 = 0;
			for (std::size_t i = j + 3; i < L; ++i) {
				c0 = _multadd(Y[i], D, c0, 0, &lo);
				c1 = _multadd(lo, D, c1, 0, &lo);
				c2 = _multadd(lo, D, c2, 0, &lo);
				c3 = _multadd(lo, D, c3, 0, &Y[i]);
			}
			_split_chunk(rx, c0, o);
			_split_chunk(rx, c1, o + rx.k);
			_split_chunk(rx, c2, o + 2 * static_cast<std::size_t>(rx.k));
			_split_chunk(rx, c3, o + 3 * static_cast<std::size_t>(rx.k));
			o += 4 * static_cast<std::size_t>(rx.k);
		}
		for (; j < K; ++j) {
			digit carry = 0;
			for (std::size_t i = j; i < L; ++i) {
				carry = _multadd(Y[i], D, carry, 0, &Y[i]);
			}
			_split_chunk(rx, carry, o);
			o += rx.k;
		}
		return _strip_zeros(out, K * static_cast<std::size_t>(rx.k));
	}

	// The digits of the n >= 1 limbs of x (top limb nonzero), written to out[0..K * k) and
	// stripped of leading zeros; returns the count.
	static std::size_t _encode_radix(const radix& rx, const radix_table& t, const digit* x, std::size_t n, unsigned char* out) {
		if (n <= radix_unrolled && t.chunks - n <= 2) {
			switch (n) {
			case 1: return radix_unrolled >= 1 ? _enc_fixed_n<1>(rx, t, x, out) : 0;
			case 2: return radix_unrolled >= 2 ? _enc_fixed_n<2>(rx, t, x, out) : 0;
			case 3: return radix_unrolled >= 3 ? _enc_fixed_n<3>(rx, t, x, out) : 0;
			case 4: return radix_unrolled >= 4 ? _enc_fixed_n<4>(rx, t, x, out) : 0;
			case 5: return radix_unrolled >= 5 ? _enc_fixed_n<5>(rx, t, x, out) : 0;
			case 6: return radix_unrolled >= 6 ? _enc_fixed_n<6>(rx, t, x, out) : 0;
			case 7: return radix_unrolled >= 7 ? _enc_fixed_n<7>(rx, t, x, out) : 0;
			default: return radix_unrolled >= 8 ? _enc_fixed_n<8>(rx, t, x, out) : 0;
			}
		}
		return _enc_general(rx, t, x, n, out);
	}

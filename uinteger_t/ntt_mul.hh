// The transform multiplication: size plan, load, Chinese remainder, Karatsuba and the multiplication dispatch. Included inside class uinteger_t by uinteger_t.hh.

	// Words (64 / digit_bits limbs each) that hold `n` limbs.
	static constexpr std::size_t ntt_limbs_per_word = 64 / digit_bits;

	static std::size_t ntt_words(std::size_t n) {
		return n / ntt_limbs_per_word + (n % ntt_limbs_per_word != 0);
	}

	// The transform length for `cw` coefficients: the least of 2^k and 3 2^k (k >= 2) not below cw, and 0
	// when it would pass 2^53.
	static unsigned long long ntt_length(unsigned long long cw) {
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

	// Words of the twiddle table for a transform of length n: n / 2 + 1 entries, or 2n / 3 + 1 for 3 2^k.
	static unsigned long long ntt_table_words(unsigned long long n) {
		return 2 * (n % 3 ? n / 2 : 2 * (n / 3)) + 2;
	}

	// Plans the transform for an `an` x `bn` limb product (a square when `sqr`): its length `len` in words
	// (see ntt_length) and the `scratch` it needs in limbs (3 or 4 arrays of the length, the second
	// operand's transform unless squaring, the twiddle table and a word to align by). False when it
	// cannot be used: the length is beyond the roots of unity, or the scratch is more than a quarter of
	// what Size can address in bytes, which also keeps every sum of scratch sizes in the callers from
	// wrapping; the caller then takes Toom-4. Production code uses std::size_t; tests pass narrower sizes.
	template <typename Size>
	static bool _ntt_plan(Size an, Size bn, bool sqr, Size* len, Size* scratch) {
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

	static std::size_t k_ntt_scratch_size(std::size_t an, std::size_t bn, bool sqr) {
		std::size_t len = 0, scratch = 0;
		const bool ok = _ntt_plan<std::size_t>(an, bn, sqr, &len, &scratch);
		assert(ok); (void)(ok);
		return scratch;
	}

	// The scratch as words: aligned up to 8 bytes (the plan reserves the word).
	static ntt_word* ntt_align(digit* s) {
		const auto addr = reinterpret_cast<std::uintptr_t>(s);
		digit* const aligned = s + ((8 - (addr & 7)) & 7) / sizeof(digit);
		assert((reinterpret_cast<std::uintptr_t>(aligned) & 7) == 0);
		return reinterpret_cast<ntt_word*>(aligned);
	}

	// The words of a (the limbs packed little-endian), each reduced to [0, 2p), then zeros up to n.
	static void ntt_load(ntt_word* x, std::size_t n, const digit* a, std::size_t an, const ntt_mod& m) {
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

	// x = x y / n mod p, pointwise, in [0, p): x is scaled by sc = 2^64 / n mod p (ntt_unscale) with a Shoup
	// product (the Montgomery factor and the inverse length at once), then Montgomery-reduced against y.
	static void ntt_pointwise(ntt_word* x, const ntt_word* y, std::size_t n, const ntt_mod& m, ntt_word sc) {
		const ntt_word sq = ntt_quot(m, sc);
		for (std::size_t k = 0; k < n; ++k) {
			const ntt_word u = ntt_mulq(x[k], sc, sq, m.p);
			ntt_word lo;
			const ntt_word hi = _mul64(u, y[k], &lo);
			const ntt_word mh = _mulhi64(static_cast<ntt_word>(lo * m.pinv), m.p);
			x[k] = hi < mh ? hi - mh + m.p : hi - mh;
		}
	}

	// Stores word `w` as the limbs at word index k of r[0..rn); limbs past rn must be zero.
	static void ntt_put(digit* r, std::size_t rn, std::size_t k, ntt_word w) {
		for (std::size_t q = 0; q < ntt_limbs_per_word; ++q) {
			const std::size_t i = k * ntt_limbs_per_word + q;
			if (i < rn) {
				r[i] = static_cast<digit>(w >> (q * digit_bits));
			} else {
				assert((w >> (q * digit_bits)) == 0);
			}
		}
	}

	// Constants of the Chinese remainder step: p0^-1 mod p1, p0 mod p2, (p0 p1)^-1 mod p2, and p0 p1 as two words.
	static constexpr ntt_word ntt_crt_inv01 = 0x34fcccccccccccbeull;
	static constexpr ntt_word ntt_crt_p0_mod_p2 = 0x0540000000000000ull;
	static constexpr ntt_word ntt_crt_inv012 = 0x1790924924924ac7ull;
	static constexpr ntt_word ntt_crt_p01_hi = 0x0e670c0000000000ull;
	static constexpr ntt_word ntt_crt_p01_lo = 0x7980000000000001ull;

	// Garner's form of the Chinese remainder theorem for the three residue arrays (each in [0, 2p)), the
	// coefficients x = x0 + p0 t1 + p0 p1 t2 below p0 p1 p2 (192 bits), summed into r[0..rn) at 64-bit word
	// positions with the carries kept in two words.
	static void ntt_crt(digit* r, std::size_t rn, const ntt_word* const res[3], std::size_t cw, const ntt_mod mod[3]) {
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

	// r[0..an+bn) = a * b, or a^2 when `sqr` (b is then ignored), by the transform; r must not overlap
	// the operands. `s` is scratch of k_ntt_scratch_size limbs; the kernel does not allocate.
	static void k_ntt_mul(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, bool sqr, digit* s) {
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

	// r[0..an+bn) = a * b by Karatsuba, for an >= bn >= 2 and an < 2 bn: split at h = ceil(an / 2).
	static void k_mul_karatsuba(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s) {
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

	// r[0..an+bn) = a * b for any sizes (an, bn >= 1); r must not overlap a or b.
	// `s` is scratch of k_mul_scratch_size(an, bn) limbs.
	static void k_mul(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s) {
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

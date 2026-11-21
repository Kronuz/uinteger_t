// Raw limb kernels over digit arrays: add, subtract, multiply by a limb, shifts, basecase products, exact division. Class body of uinteger_t, included inside it by uinteger_t.hh; not a header.

	// Raw limb kernels over `digit*` arrays of explicit length (little-endian).

	// The carry passes from limb to limb as a value. One limb per pass the compiler spends two dependent
	// operations on it (adds, then a conditional increment); eight limbs per pass it chains them through
	// the carry flag (adds, then adcs back to back), which is half the latency per limb.
	static digit k_add_n(digit* r, const digit* a, const digit* b, std::size_t n) {
		digit c = 0;
		std::size_t i = 0;
		for (; n - i >= 8; i += 8) {
			c = _addcarry(a[i], b[i], c, &r[i]);
			c = _addcarry(a[i + 1], b[i + 1], c, &r[i + 1]);
			c = _addcarry(a[i + 2], b[i + 2], c, &r[i + 2]);
			c = _addcarry(a[i + 3], b[i + 3], c, &r[i + 3]);
			c = _addcarry(a[i + 4], b[i + 4], c, &r[i + 4]);
			c = _addcarry(a[i + 5], b[i + 5], c, &r[i + 5]);
			c = _addcarry(a[i + 6], b[i + 6], c, &r[i + 6]);
			c = _addcarry(a[i + 7], b[i + 7], c, &r[i + 7]);
		}
		for (; i < n; ++i) {
			c = _addcarry(a[i], b[i], c, &r[i]);
		}
		return c;
	}

	// r = a + c for n limbs, returns the carry out.
	static digit k_add_1(digit* r, const digit* a, std::size_t n, digit c) {
		std::size_t i = 0;
		for (; c && i < n; ++i) {
			c = _addcarry(a[i], 0, c, &r[i]);
		}
		if (r != a) {
			for (; i < n; ++i) {
				r[i] = a[i];
			}
		}
		return c;
	}

	static digit k_sub_n(digit* r, const digit* a, const digit* b, std::size_t n) {
		digit c = 0;
		std::size_t i = 0;
		for (; n - i >= 8; i += 8) {
			c = _subborrow(a[i], b[i], c, &r[i]);
			c = _subborrow(a[i + 1], b[i + 1], c, &r[i + 1]);
			c = _subborrow(a[i + 2], b[i + 2], c, &r[i + 2]);
			c = _subborrow(a[i + 3], b[i + 3], c, &r[i + 3]);
			c = _subborrow(a[i + 4], b[i + 4], c, &r[i + 4]);
			c = _subborrow(a[i + 5], b[i + 5], c, &r[i + 5]);
			c = _subborrow(a[i + 6], b[i + 6], c, &r[i + 6]);
			c = _subborrow(a[i + 7], b[i + 7], c, &r[i + 7]);
		}
		for (; i < n; ++i) {
			c = _subborrow(a[i], b[i], c, &r[i]);
		}
		return c;
	}

	// r = a - c for n limbs, returns the borrow out.
	static digit k_sub_1(digit* r, const digit* a, std::size_t n, digit c) {
		std::size_t i = 0;
		for (; c && i < n; ++i) {
			c = _subborrow(a[i], 0, c, &r[i]);
		}
		if (r != a) {
			for (; i < n; ++i) {
				r[i] = a[i];
			}
		}
		return c;
	}

	static digit k_mul_1(digit* r, const digit* a, std::size_t n, digit m) {
		digit c = 0;
		for (std::size_t i = 0; i < n; ++i) {
			c = _multadd(a[i], m, 0, c, &r[i]);
		}
		return c;
	}

	static digit k_addmul_1(digit* r, const digit* a, std::size_t n, digit m) {
		digit c = 0;
		for (std::size_t i = 0; i < n; ++i) {
			c = _multadd(a[i], m, r[i], c, &r[i]);
		}
		return c;
	}

	// r -= a * m over n limbs; returns the borrow limb. One word carries between limbs and takes
	// the last operation on the chain: r[i] - lo and the high product limb are off it, so each
	// limb adds one dependent subtraction, as _multadd does for addition.
	static digit k_submul_1(digit* r, const digit* a, std::size_t n, digit m) {
		digit c = 0;
		for (std::size_t i = 0; i < n; ++i) {
			digit lo;
			digit hi = _mult(a[i], m, &lo);
			digit s;
			const digit b1 = _subborrow(r[i], lo, 0, &s);
			const digit b2 = _subborrow(s, c, 0, &r[i]);
			c = static_cast<digit>(hi + b1 + b2);
		}
		return c;
	}

	// r = a << s (0 < s < digit_bits) for n limbs, returns the bits shifted out.
	static digit k_lshift(digit* r, const digit* a, std::size_t n, unsigned s) {
		if (!n) {
			return 0;
		}
		const unsigned t = digit_bits - s;
		digit out = static_cast<digit>(a[n - 1] >> t);
		for (std::size_t i = n - 1; i > 0; --i) {
			r[i] = static_cast<digit>((a[i] << s) | (a[i - 1] >> t));
		}
		r[0] = static_cast<digit>(a[0] << s);
		return out;
	}

	// r = a >> s (0 < s < digit_bits) for n limbs, returns the bits shifted out.
	static digit k_rshift(digit* r, const digit* a, std::size_t n, unsigned s) {
		if (!n) {
			return 0;
		}
		const unsigned t = digit_bits - s;
		digit out = static_cast<digit>(a[0] << t);
		for (std::size_t i = 0; i + 1 < n; ++i) {
			r[i] = static_cast<digit>((a[i] >> s) | (a[i + 1] << t));
		}
		r[n - 1] = static_cast<digit>(a[n - 1] >> s);
		return out;
	}

	// r = a + (b << s) for 0 < s < digit_bits and n limbs, returns the bits shifted out of b plus the carry (below 2^s + 1).
	static digit k_addlsh_n(digit* r, const digit* a, const digit* b, std::size_t n, unsigned s) {
		const unsigned t = digit_bits - s;
		digit c = 0;
		digit prev = 0;
		for (std::size_t i = 0; i < n; ++i) {
			digit v = static_cast<digit>((b[i] << s) | prev);
			prev = static_cast<digit>(b[i] >> t);
			c = _addcarry(a[i], v, c, &r[i]);
		}
		return static_cast<digit>(prev + c);
	}

	// r[0..an+bn) = a * b, schoolbook, two rows of b per pass over a so the products and
	// carries of both rows stay in registers. Requires an >= bn >= 1; r must not overlap a or b.
	static void k_mul_basecase(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn) {
		if (bn == 1) {
			r[an] = k_mul_1(r, a, an, b[0]);
			return;
		}
		std::fill(r, r + an, digit(0));
		std::size_t j = 0;
		for (; j + 2 <= bn; j += 2) {
			digit* w = r + j;
			const digit b0 = b[j];
			const digit b1 = b[j + 1];
			digit c0 = 0;
			digit c1 = 0;
			digit prev = 0;
			for (std::size_t i = 0; i < an; ++i) {
				digit x;
				c0 = _multadd(a[i], b0, w[i], c0, &x);
				c1 = _multadd(prev, b1, x, c1, &w[i]);
				prev = a[i];
			}
			digit lo;
			digit hi = _multadd(prev, b1, c0, c1, &lo);
			w[an] = lo;
			w[an + 1] = hi;
		}
		if (j < bn) {
			r[an + j] = k_addmul_1(r + j, a, an, b[j]);
		}
	}

	// r[off..rn) += p[0..pn), propagating the carry through the rest of r.
	static void k_add_at(digit* r, std::size_t rn, std::size_t off, const digit* p, std::size_t pn) {
		assert(off + pn <= rn);
		digit c = k_add_n(r + off, r + off, p, pn);
		if (c) {
			c = k_add_1(r + off + pn, r + off + pn, rn - off - pn, c);
		}
		assert(!c); (void)(c);
	}

	// r[0..2n) = a^2, schoolbook. The products a[i] * a[j] with i < j are formed once, two rows of
	// the triangle per pass, then doubled; the squares a[i]^2 are added last. Requires n >= 1; r must
	// not overlap a.
	static void k_sqr_basecase(digit* r, const digit* a, std::size_t n) {
		if (n == 1) {
			r[1] = _mult(a[0], a[0], &r[0]);
			return;
		}
		std::fill(r, r + 2 * n, digit(0));
		std::size_t i = 0;
		for (; i + 3 <= n; i += 2) {
			// Rows i and i + 1 over a[i + 1 ..): a[i] * a[j] lands at i + j, a[i + 1] * a[j] (j > i + 1) at
			// i + j + 1. The first step skips row i + 1 so that a[i + 1]^2 is left to the diagonal.
			digit* w = r + 2 * i + 1;
			const digit* m = a + i + 1;
			const std::size_t mn = n - i - 1;
			const digit b0 = a[i];
			const digit b1 = m[0];
			digit c0 = _multadd(m[0], b0, w[0], 0, &w[0]);
			digit c1 = 0;
			digit prev = 0;
			for (std::size_t k = 1; k < mn; ++k) {
				digit x;
				c0 = _multadd(m[k], b0, w[k], c0, &x);
				c1 = _multadd(prev, b1, x, c1, &w[k]);
				prev = m[k];
			}
			digit lo;
			digit hi = _multadd(prev, b1, c0, c1, &lo);
			w[mn] = lo;
			w[mn + 1] = hi;
		}
		if (i + 2 <= n) {
			r[n + i] = k_addmul_1(r + 2 * i + 1, a + i + 1, n - i - 1, a[i]);
		}
		r[2 * n - 1] = k_lshift(r + 1, r + 1, 2 * n - 2, 1);
		digit c = 0;
		for (std::size_t j = 0; j < n; ++j) {
			digit lo;
			digit hi = _mult(a[j], a[j], &lo);
			c = _addcarry(r[2 * j], lo, c, &r[2 * j]);
			c = _addcarry(r[2 * j + 1], hi, c, &r[2 * j + 1]);
		}
	}

	// r[0..n) = |a - b| for a of n limbs and b of bn <= n limbs; returns 1 when a < b. r may be a.
	static digit k_absdiff(digit* r, const digit* a, std::size_t n, const digit* b, std::size_t bn) {
		std::size_t i = n;
		while (i > bn && !a[i - 1]) {
			--i;
		}
		bool lt = false;
		if (i <= bn) {
			while (i && a[i - 1] == b[i - 1]) {
				--i;
			}
			lt = i && a[i - 1] < b[i - 1];
		}
		if (lt) {
			// The limbs of a above bn are zero.
			k_sub_n(r, b, a, bn);
			std::fill(r + bn, r + n, digit(0));
		} else {
			digit c = k_sub_n(r, a, b, bn);
			if (n > bn) {
				c = k_sub_1(r + bn, a + bn, n - bn, c);
			}
			assert(!c); (void)(c);
		}
		return lt;
	}

	// r[0..n) = -a mod B^n (two's complement); r may be a.
	static void k_neg_n(digit* r, const digit* a, std::size_t n) {
		digit c = 0;
		for (std::size_t i = 0; i < n; ++i) {
			c = _subborrow(0, a[i], c, &r[i]);
		}
	}

	// r[0..n) -= p[0..pn) mod B^n, for pn <= n: the borrow out is dropped.
	static void k_sub_wrap(digit* r, std::size_t n, const digit* p, std::size_t pn) {
		digit c = k_sub_n(r, r, p, pn);
		if (c && n > pn) {
			k_sub_1(r + pn, r + pn, n - pn, c);
		}
	}

	// r[0..n) = a / 3 mod B^n when 3 divides a (as the two's complement integer a stands for).
	// Each quotient limb is the limb, less the borrow from below, times the inverse of 3 mod B;
	// the borrow into the next limb is the high part of 3 times that quotient limb.
	static void k_divexact_3(digit* r, const digit* a, std::size_t n) {
		constexpr digit third = static_cast<digit>(~digit(0)) / 3;
		constexpr digit inv3 = static_cast<digit>(2 * third + 1);
		digit borrow = 0;
		for (std::size_t i = 0; i < n; ++i) {
			digit x = a[i];
			digit wrapped = x < borrow;
			digit q = static_cast<digit>(static_cast<std::uint64_t>(static_cast<digit>(x - borrow)) * inv3);
			r[i] = q;
			borrow = static_cast<digit>(wrapped + (q > third) + (q > 2 * third));
		}
	}

	// r[0..n) = a / d mod B^n for a small odd d (below 16) that divides a, as k_divexact_3 does for 3:
	// each quotient limb is the limb, less the borrow from below, times d^-1 mod B, and the borrow
	// into the next limb is the high part of d times that quotient limb (Jebelean, "An algorithm for
	// exact division", J. Symbolic Comput. 15, 1993).
	static void k_divexact_1(digit* r, const digit* a, std::size_t n, digit d) {
		assert((d & 1) && d < 16);
		// Newton's iteration for d^-1 mod 2^64: d * d = 1 mod 8, and each step doubles the correct low bits.
		std::uint64_t inv = d;
		for (int i = 0; i < 5; ++i) {
			inv *= 2 - static_cast<std::uint64_t>(d) * inv;
		}
		const digit dinv = static_cast<digit>(inv);
		digit borrow = 0;
		for (std::size_t i = 0; i < n; ++i) {
			digit x = a[i];
			digit wrapped = x < borrow;
			digit q = static_cast<digit>(static_cast<std::uint64_t>(static_cast<digit>(x - borrow)) * dinv);
			r[i] = q;
			digit lo;
			borrow = static_cast<digit>(_mult(q, d, &lo) + wrapped);
		}
	}

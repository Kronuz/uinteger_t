// Recursive (Burnikel-Ziegler) and Newton division kernels. Class body of uinteger_t, included inside it by uinteger_t.hh; not a header.

	// Recursive division after Burnikel and Ziegler, "Fast Recursive Division" (MPI-I-98-1-022,
	// 1998), on the contract of k_div_sb, with k_mul for the products. Two routines call each other:
	// k_div_dc_bal divides 2n limbs by n; k_div_dc_step produces c quotient limbs (1 <= c < n) from an
	// n-limb divisor by dividing the top 2c limbs by the top c limbs of the divisor (a balanced
	// division) and correcting the estimate with one c-by-(n-c) product. A balanced division
	// below div_dc_threshold limbs, and a step with fewer than div_dc_step_threshold quotient
	// limbs, fall back to k_div_sb.
#ifndef DIV_DC_THRESHOLD
#define DIV_DC_THRESHOLD (2048 / (sizeof(DIGIT_T) * 8))
#endif
#ifndef DIV_DC_STEP_THRESHOLD
#define DIV_DC_STEP_THRESHOLD (DIV_DC_THRESHOLD / 4 < 2 ? 2 : DIV_DC_THRESHOLD / 4)
#endif
	static constexpr std::size_t div_dc_threshold = DIV_DC_THRESHOLD;
	static constexpr std::size_t div_dc_step_threshold = DIV_DC_STEP_THRESHOLD;
	static_assert(div_dc_threshold >= 2 && div_dc_step_threshold >= 2, "the recursive division needs thresholds of at least 2 limbs");

	static std::size_t k_div_dc_bal_scratch_size(std::size_t n) {
		return n < div_dc_threshold ? 0 : k_div_dc_step_scratch_size(n, (n + 1) / 2);
	}

	// The product buffer is free again by the time the recursion returns, so it shares the scratch.
	static std::size_t k_div_dc_step_scratch_size(std::size_t dn, std::size_t c) {
		if (c < div_dc_step_threshold) {
			return 0;
		}
		const std::size_t l = dn - c;
		return std::max(k_div_dc_bal_scratch_size(c), dn + k_mul_scratch_size(std::max(c, l), std::min(c, l)));
	}

	// Scratch limbs k_div_dc needs for an nn-limb dividend and a dn-limb divisor.
	static std::size_t k_div_dc_scratch_size(std::size_t nn, std::size_t dn) {
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

	// Divides a[0..dn+c) by the normalized b[0..dn), 1 <= c < dn, where a < b * B^c. Writes c
	// quotient limbs to q and leaves the remainder in a[0..dn); a[dn..dn+c) is clobbered.
	static void k_div_dc_step(digit* q, digit* a, std::size_t dn, std::size_t c, const digit* b, digit* s) {
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

	// Divides a[0..2n) by the normalized b[0..n), where the top n limbs of a are below b. Writes
	// n quotient limbs to q and leaves the remainder in a[0..n); a[n..2n) is clobbered.
	static void k_div_dc_bal(digit* q, digit* a, std::size_t n, const digit* b, digit* s) {
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

	// Divides np[0..nn) by the normalized dp[0..dn) like k_div_sb, but subquadratically: the
	// quotient is cut into dn-limb blocks from the top (the partial block first), each one a
	// balanced or short division. `s` is scratch of k_div_dc_scratch_size(nn, dn) limbs.
	static void k_div_dc(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn, digit* s) {
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

	// Division by an approximate reciprocal of the divisor (Barrett, "Implementing the Rivest Shamir
	// and Adleman public key encryption algorithm on a standard digital signal processor", 1986;
	// Brent and Zimmermann, Modern Computer Arithmetic, 3.3.1 and 3.4.1), on the contract of k_div_dc.
	// The reciprocal is built by Newton iteration with precision doubling, so every product goes
	// through k_mul and takes the transform when it is long. A division is cut into blocks of c
	// quotient limbs that share one reciprocal of the top c limbs of the divisor; each block costs a
	// c-limb product for its quotient estimate and a c-by-dn-limb product for its remainder, then a
	// bounded number of corrections. Below div_newton_threshold divisor limbs, with fewer than
	// div_newton_quotient_threshold quotient limbs or under half the divisor's, k_div_dc takes the
	// recursive division instead: a block's two products cost the same however short the quotient, so a
	// short quotient over a tall divisor is cheaper by recursion.
#ifdef UINTEGER_T_DIV_NEWTON_THRESHOLD
	static constexpr std::size_t div_newton_threshold = UINTEGER_T_DIV_NEWTON_THRESHOLD;
#else
	static constexpr std::size_t div_newton_threshold = 1400;
#endif
#ifdef UINTEGER_T_DIV_NEWTON_QUOTIENT_THRESHOLD
	static constexpr std::size_t div_newton_quotient_threshold = UINTEGER_T_DIV_NEWTON_QUOTIENT_THRESHOLD;
#else
	static constexpr std::size_t div_newton_quotient_threshold = 5000;
#endif
	// Reciprocals of fewer limbs than this are exact, by schoolbook division.
#ifdef UINTEGER_T_DIV_NEWTON_INV_BASE
	static constexpr std::size_t div_newton_inv_base = UINTEGER_T_DIV_NEWTON_INV_BASE;
#else
	static constexpr std::size_t div_newton_inv_base = 48;
#endif
	static_assert(div_newton_inv_base >= 3 && div_newton_quotient_threshold >= 2 && div_newton_threshold >= 2,
		"the Newton division needs a reciprocal base of 3 limbs (the iteration halves n - 1) and thresholds of 2");

	// The quotient estimate of a block is at most this many above and below the quotient: the estimate
	// is the exact quotient of the top 2c limbs by the top c limbs of the divisor, less the error of the
	// reciprocal and of two truncations (see k_div_newton_step).
	static constexpr unsigned div_newton_down = 2;
	static constexpr unsigned div_newton_up = 8;
	struct k_div_corrections {
		unsigned down;  // times the divisor was added back (the quotient estimate was above)
		unsigned up;    // times it was subtracted again (the estimate was below)
	};

	// Limbs per block for an m-limb quotient and a dn-limb divisor: at least two blocks (a reciprocal of
	// half the precision costs less than the product it saves), each of at most dn limbs.
	static std::size_t k_div_newton_block(std::size_t m, std::size_t dn) {
		const std::size_t blocks = std::max<std::size_t>(2, (m - 1) / dn + 1);
		return (m - 1) / blocks + 1;
	}

	// Scratch limbs of k_div_newton_inv for an n-limb divisor.
	static std::size_t k_div_newton_inv_scratch_size(std::size_t n) {
		if (n < div_newton_inv_base) {
			return 2 * n;
		}
		const std::size_t l = (n - 1) / 2;
		const std::size_t h = n - l;
		return std::max(k_div_newton_inv_scratch_size(h), std::max((n + h + 1) + k_mul_scratch_size(n, h + 1),
			(2 * n + 4) + k_mul_scratch_size(h + 1, l + 2)));
	}

	// Plans the Newton division of an nn-limb dividend by a dn-limb divisor: the `scratch` limbs it needs
	// (the reciprocal, then the larger of its own scratch and a block's: the estimate's c-by-c product and
	// the remainder's c-by-dn product with their multiplication scratch). False when the sizes are too
	// large to add safely: the scratch is more than a quarter of what Size can address in bytes, or the
	// dividend more than a sixty-fourth of it in limbs, which also keeps every sum of sizes in the
	// callers from wrapping; k_div_dc then takes the recursive division. Production code uses
	// std::size_t; tests pass narrower sizes.
	template <typename Size>
	static bool _div_newton_plan(Size nn, Size dn, Size* scratch) {
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

	static bool k_div_uses_newton(std::size_t nn, std::size_t dn) {
		std::size_t scratch = 0;
		const std::size_t m = nn - dn;
		return dn >= div_newton_threshold && m >= div_newton_quotient_threshold && 2 * m >= dn &&
			_div_newton_plan<std::size_t>(nn, dn, &scratch);
	}

	static std::size_t k_div_newton_scratch_size(std::size_t nn, std::size_t dn) {
		std::size_t scratch = 0;
		const bool ok = _div_newton_plan<std::size_t>(nn, dn, &scratch);
		assert(ok); (void)(ok);
		return scratch;
	}

	// The reciprocal of the normalized t[0..n): w[0..n] = B^n + X with 0 <= B^(2n)/t - w < 2, X in w[0..n)
	// and w[n] = 1. `s` is scratch of k_div_newton_inv_scratch_size(n) limbs. Writing rho = B^(2n)/t:
	//  - Below div_newton_inv_base the value is exact, floor((B^(2n) - 1) / t) = B^n + floor(((B^n - 1 - t)
	//    B^n + B^n - 1) / t), a division whose top n limbs are below t.
	//  - Above it, with l = (n - 1) / 2 and h = n - l, the reciprocal W_h of the top h limbs t_h gives
	//    rho_h = B^(2h)/t_h with rho_h - rho' < 4, rho' = B^(n+h)/t (t - t_h B^l < B^l, t_h and t at least
	//    half their bases), so W_h' = W_h - 4 is at most rho' and E' = B^(n+h) - t W_h' is not negative,
	//    and below 6 t. The Newton step W = W_h' B^l + floor(W_h' E' / B^(2h)) has rho - W below
	//    (rho' - W_h')^2 B^(2l) / rho + 1 + 2/B, which is below 1 + 60/B. E' is only needed from limb
	//    h - 1 up (the dropped part is below 2/B in the quotient), and as it is below B^(n+1) it is the
	//    negation of the low n + 1 limbs of the product. As rho > B^n + 1 for t < B^n, W is at least B^n.
	static void k_div_newton_inv(digit* w, const digit* t, std::size_t n, digit* s) {
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

	// One block of k_div_newton: divides a[0..dn+c) by the normalized b[0..dn), 1 <= c <= dn, where
	// a < b B^c. Writes c quotient limbs to q and leaves the remainder in a[0..dn); a[dn..dn+c) is
	// clobbered. `w[0..c]` is the reciprocal (see k_div_newton_inv) of the top c limbs t of b, or the
	// top c + 1 limbs of the reciprocal of more limbs. `s` is scratch of 2c + k_mul_scratch_size(c, c) or
	// dn + c + k_mul_scratch_size(dn, c) limbs, the larger. Returns the corrections it made.
	//  Let A be the top c limbs of a (at most t, as a < b B^c), A_top the top 2c limbs, q_x = floor(A_top / t),
	//  W = B^c + X the reciprocal and q_n = floor(A W / B^c) = A + floor(A X / B^c) the estimate. With
	//  rho = B^(2c) / t, A_top / t - A W / B^c = A_lo / t + A (rho - W) / B^c, where A_lo < B^c is the
	//  low half of A_top: below 2 + 2, and the floor adds one, so q_x - 4 <= q_n <= q_x. As q <= q_x <= q + 2
	//  (the argument of k_div_dc_step), q_n is at most 2 above q and at most 4 below. The top limbs of a
	//  longer reciprocal are below the reciprocal of t by less than 5 (4 from truncating the divisor, one
	//  from the shift), which is 4 more below. A is at most t and W is below rho (rho is an integer only for
	//  t = B^c / 2, where W < 2 B^c), so A W < B^(2c) and q_n < B^c, even for A = t, where the quotient
	//  of the top limbs is B^c or more.
	//  The remainder a - q_n b is formed modulo B^(dn+1) (its size is below B^(dn+1) / 2), and corrected
	//  by adding b while it is negative and subtracting it while it is not below b.
	static k_div_corrections k_div_newton_step(digit* q, digit* a, std::size_t dn, std::size_t c, const digit* b, const digit* w, digit* s) {
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

	// Divides np[0..nn) by the normalized dp[0..dn) like k_div_dc, with one reciprocal of the top c
	// limbs of the divisor (c = k_div_newton_block) and the quotient cut into blocks of c limbs from
	// the top, the first of m % c limbs using the top limbs of the reciprocal. `s` is scratch of
	// k_div_newton_scratch_size(nn, dn) limbs.
	static void k_div_newton(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn, digit* s) {
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

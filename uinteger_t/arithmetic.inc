// Add, subtract, multiply and divide on values, and the small-divisor path. Class body of uinteger_t, included inside it by uinteger_t.hh; not a header.

	static uinteger_t& long_add(uinteger_t& lhs, const uinteger_t& rhs) {
		return long_add(lhs, lhs, rhs);
	}

	// result = lhs + rhs; `result` may be `lhs` or `rhs`.
	static uinteger_t& long_add(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		auto an = lhs.size();
		auto bn = rhs.size();
		auto rn = std::max(an, bn);
		// A carry out of the top limb needs one more limb. The room comes before the first store,
		// so a failed allocation leaves the operands as they were: always past the buffer (one
		// allocation for the whole result), and within it only when the top limbs can carry (their
		// sum wraps, or is all ones and a carry can arrive from below).
		if (rn && rn >= result._value.capacity()) {
			const digit t1 = an >= bn ? lhs.back() : rhs.back();
			const digit t2 = an == bn ? rhs.back() : digit(0);
			const digit t = static_cast<digit>(t1 + t2);
			if (rn > container::inline_capacity || t < t1 || t == static_cast<digit>(~digit(0))) {
				result.reserve(rn + 1);
			}
		}
		result.resize_uninit(rn);

		// the operand pointers are read after the resize, which may move `result`
		const digit* a = lhs.data();
		const digit* b = rhs.data();
		if (an < bn) {
			std::swap(a, b);
			std::swap(an, bn);
		}
		digit* r = result.data();
		digit carry = k_add_n(r, a, b, bn);
		if (an > bn) {
			carry = k_add_1(r + bn, a + bn, an - bn, carry);
		}
		if (carry) {
			result._value.push_back(1);
		}

		// Finish up
		result.trim();
		return result;
	}

	static uinteger_t& add(uinteger_t& lhs, const uinteger_t& rhs) {
		// First try saving some calculations:
		if (!rhs) {
			return lhs;
		}

		return long_add(lhs, rhs);
	}

	static uinteger_t& add(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		return long_add(result, lhs, rhs);
	}

	static uinteger_t add(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		add(result, lhs, rhs);
		return result;
	}

	static uinteger_t& long_sub(uinteger_t& lhs, const uinteger_t& rhs) {
		return long_sub(lhs, lhs, rhs);
	}

	// result = lhs - rhs, wrapping modulo B^max(size) like fixed-width unsigned arithmetic;
	// `result` may be `lhs` or `rhs`.
	static uinteger_t& long_sub(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		auto an = lhs.size();
		auto bn = rhs.size();
		result.resize_uninit(std::max(an, bn));

		// the operand pointers are read after the resize, which may move `result`
		const digit* a = lhs.data();
		const digit* b = rhs.data();
		digit* r = result.data();
		if (an >= bn) {
			digit borrow = k_sub_n(r, a, b, bn);
			if (an > bn) {
				k_sub_1(r + bn, a + bn, an - bn, borrow);
			}
		} else {
			digit borrow = k_sub_n(r, a, b, an);
			for (auto i = an; i < bn; ++i) {
				borrow = _subborrow(0, b[i], borrow, &r[i]);
			}
		}

		// Finish up
		result.trim();
		return result;
	}

	static uinteger_t& sub(uinteger_t& lhs, const uinteger_t& rhs) {
		// First try saving some calculations:
		if (!rhs) {
			return lhs;
		}

		return long_sub(lhs, rhs);
	}

	static uinteger_t& sub(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		// First try saving some calculations:
		if (!rhs) {
			result = lhs;
			return result;
		}

		return long_sub(result, lhs, rhs);
	}

	static uinteger_t sub(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		sub(result, lhs, rhs);
		return result;
	}

	// Scratch for the multiplication kernels: a small one lives on the stack, a larger one on the
	// heap, neither zeroed (the kernels write every limb before they read it).
	class k_scratch {
		digit _local[256];
		std::unique_ptr<digit[]> _heap;
	public:
		explicit k_scratch(std::size_t n) {
			if (n > sizeof(_local) / sizeof(digit)) {
				_heap.reset(new digit[n]);
			}
		}
		digit* data() {
			return _heap ? _heap.get() : _local;
		}
	};

	// Multiplication on raw limbs (schoolbook, Karatsuba, Toom-3, Toom-4, chunked for lopsided operands);
	// squaring when both operands are the same value.
	static uinteger_t& long_mult(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		auto an = lhs.size();
		auto bn = rhs.size();
		const digit* a = lhs.data();
		const digit* b = rhs.data();
		if (an < bn) {
			std::swap(a, b);
			std::swap(an, bn);
		}

		// The product is built in the result, or in a temporary when the result is an operand. The
		// scratch is allocated first, so a failed allocation leaves the result as it was.
		const bool square = a == b && an == bn;
		k_scratch scratch(square ? k_sqr_scratch_size(an) : k_mul_scratch_size(an, bn));
		uinteger_t tmp;
		const bool alias = &result == &lhs || &result == &rhs;
		uinteger_t& out = alias ? tmp : result;
		out.resize_uninit(an + bn);
		if (square) {
			k_sqr(out.data(), a, an, scratch.data());
		} else {
			k_mul(out.data(), a, an, b, bn, scratch.data());
		}

		if (alias) {
			result = std::move(tmp);
		}
		result.trim();
		return result;
	}

	static uinteger_t& mult(uinteger_t& lhs, const uinteger_t& rhs) {
		// Hard to see how this could have a further optimized implementation.
		return mult(lhs, lhs, rhs);
	}

	static uinteger_t& mult(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		// First try saving some calculations:
		if (!lhs || !rhs) {
			result.clear();
			return result;
		}
		if (lhs.size() == 1 && lhs.front() == 1) {
			result = rhs;
			return result;
		}
		if (rhs.size() == 1 && rhs.front() == 1) {
			result = lhs;
			return result;
		}

		return long_mult(result, lhs, rhs);
	}

	static uinteger_t mult(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		mult(result, lhs, rhs);
		return result;
	}

	// Sets this value to a single limb (zero is the empty value).
	void set_digit(digit v) {
		if (v) {
			_value.assign(std::size_t(1), v);
		} else {
			_value.clear();
		}
	}

	// Single word division. Either output may be null when it is not wanted; an output may be `lhs`.
	static void single_divmod(uinteger_t* quotient, uinteger_t* remainder, const uinteger_t& lhs, const uinteger_t& rhs) {
		assert(rhs.size() == 1);
		const digit d = rhs.front();
		if (quotient) {
			if (quotient != &lhs) {
				*quotient = lhs;
			}
			const digit r = quotient->divmod_small(d);
			if (remainder) {
				remainder->set_digit(r);
			}
		} else {
			remainder->set_digit(k_mod_1(lhs.data(), lhs.size(), small_divisor(d)));
		}
	}


	// A quotient-only division takes k_div_sb_trunc when its quotient is short: with m = nn - dn
	// limbs of dividend beyond the divisor's, m < div_trunc_threshold (T) for m <= dn, m < 2 T for
	// m <= dn / 2 and m < T / 2 for m <= 2 dn. The kernel costs about qn^2/2 limb products whatever
	// the divisor's length and nothing in it reaches the divisor's low limbs, while the recursive
	// division pays a product of the divisor's size for a short quotient. It beats the recursive
	// division on random operands up to several times T, but an exact multiple (a remainder of 0)
	// always flags, is repaired, and costs about one schoolbook division, which the recursion
	// undercuts above 32 limbs; T is set where that stays within about 15% of the recursive division.
	// A longer quotient truncates only its last dn digits, which does not repay a repair. The Newton
	// division starts at a quotient of 5000 limbs, far above every quotient this takes.
#ifdef UINTEGER_T_DIV_TRUNC_THRESHOLD
	static constexpr std::size_t div_trunc_threshold = UINTEGER_T_DIV_TRUNC_THRESHOLD;
#else
	static constexpr std::size_t div_trunc_threshold = 64;
#endif

	static bool k_div_uses_trunc(std::size_t m, std::size_t dn) {
		if (m <= dn / 2) {
			return m < 2 * div_trunc_threshold;
		}
		return m <= dn ? m < div_trunc_threshold : (m - dn <= dn && m < div_trunc_threshold / 2);
	}

	// Knuth's Algorithm D on raw limbs, with a reciprocal-based quotient digit estimate. Either
	// output may be null when it is not wanted; an output may be `lhs` or `rhs`, which are copied
	// before any output is written.
	static void knuth_divmod(uinteger_t* quotient, uinteger_t* remainder, const uinteger_t& lhs, const uinteger_t& rhs) {
		const std::size_t nn = lhs.size();
		const std::size_t dn = rhs.size();
		assert(nn >= dn && dn >= 2);

		// The normalized divisor and dividend, the quotient and the recursion's scratch share one
		// buffer, on the stack when it is small.
		const std::size_t qn = nn + 1 - dn;
		const bool trunc = !remainder && k_div_uses_trunc(nn - dn, dn);
		const std::size_t sn = trunc ? 0 : k_div_dc_scratch_size(nn + 1, dn);
		const std::size_t total = dn + (nn + 1) + qn + sn;
		k_scratch buffer(total);
		digit* d = buffer.data();
		digit* n = d + dn;
		digit* q = n + nn + 1;
		digit* s = q + qn;

		// Normalize: shift the divisor so its top bit is set, and the dividend alike.
		const unsigned sh = static_cast<unsigned>(digit_bits - _bits(rhs.back()));
		const auto load = [&] {
			if (sh) {
				n[nn] = k_lshift(n, lhs.data(), nn, sh);
			} else {
				std::copy(lhs.data(), lhs.data() + nn, n);
				n[nn] = 0;
			}
		};
		if (sh) {
			k_lshift(d, rhs.data(), dn, sh);
		} else {
			std::copy(rhs.data(), rhs.data() + dn, d);
		}
		load();

		if (trunc) {
			// The kernel repairs a flagged digit itself; if its own checks fail the division goes to
			// the exact kernels on a fresh dividend (`lhs` is intact until the outputs are written),
			// with the scratch for them taken then.
			if (k_div_sb_trunc(q, n, nn + 1, d, dn) == 3) {
				load();
				k_scratch exact(k_div_dc_scratch_size(nn + 1, dn));
				k_div_dc(q, n, nn + 1, d, dn, exact.data());
			}
		} else {
			k_div_dc(q, n, nn + 1, d, dn, s);
		}

		if (quotient) {
			quotient->_value.assign(q, q + qn);
			quotient->trim();
		}
		if (remainder) {
			if (sh) {
				k_rshift(n, n, dn, sh);
			}
			remainder->_value.assign(n, n + dn);
			remainder->trim();
		}
	}

	// Quotient and remainder of lhs / rhs; either output may be null when it is not wanted, so
	// `a / b` and `a % b` build one result. An output may be `lhs` or `rhs`.
	static void divide(uinteger_t* quotient, uinteger_t* remainder, const uinteger_t& lhs, const uinteger_t& rhs) {
		// First try saving some calculations:
		if (!rhs) {
			throw std::domain_error("Error: division or modulus by 0");
		}
		const auto lhs_sz = lhs.size();
		const auto rhs_sz = rhs.size();
		if (lhs_sz == 1 && rhs_sz == 1) {
			// Fast division and modulo for single value
			const digit a = lhs.front();
			const digit b = rhs.front();
			if (quotient) {
				quotient->set_digit(a / b);
			}
			if (remainder) {
				remainder->set_digit(a % b);
			}
			return;
		}
		if (rhs_sz == 1 && rhs.front() == 1) {
			if (quotient) {
				*quotient = lhs;
			}
			if (remainder) {
				remainder->clear();
			}
			return;
		}
		auto compared = compare(lhs, rhs);
		if (compared == 0) {
			if (quotient) {
				quotient->set_digit(1);
			}
			if (remainder) {
				remainder->clear();
			}
			return;
		}
		if (!lhs || compared < 0) {
			if (remainder) {
				*remainder = lhs;
			}
			if (quotient) {
				quotient->clear();
			}
			return;
		}
		if (rhs_sz == 1) {
			single_divmod(quotient, remainder, lhs, rhs);
			return;
		}

		knuth_divmod(quotient, remainder, lhs, rhs);
	}

	static std::pair<uinteger_t, uinteger_t> divmod(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t quotient;
		uinteger_t remainder;
		divmod(quotient, remainder, lhs, rhs);
		return std::make_pair(std::move(quotient), std::move(remainder));
	}

UINTEGER_T_PRIVATE:
	// In-place single-limb primitives (for owned values)

	// Divisor with a precomputed reciprocal, reusable across divmod_small calls.
	struct small_divisor {
		digit d;
		digit dn;
		digit inv;
		unsigned shift;

		explicit small_divisor(digit divisor) :
			d(divisor),
			dn(divisor),
			inv(0),
			shift(0)
		{
			if (!divisor) {
				throw std::domain_error("Error: Division by zero");
			}
			while (!(dn >> (digit_bits - 1))) {
				dn <<= 1;
				++shift;
			}
		#if defined HAVE____INT128_T
			if (digit_bits == 64) {
				inv = static_cast<digit>(~static_cast<__uint128_t>(0) / dn);
			}
		#endif
		}
	};

	// Divides this value by the divisor in place and returns the remainder.
	digit divmod_small(const small_divisor& dv) {
		auto n = size();
		if (!n) {
			return 0;
		}
		auto p = data();
		digit r = 0;
	#if defined HAVE____INT128_T
		if (digit_bits == 64) {
			// Moller-Granlund 2-by-1 division with a precomputed reciprocal.
			const unsigned s = dv.shift;
			const digit dn = dv.dn;
			const digit v = dv.inv;
			if (s) {
				r = p[n - 1] >> (digit_bits - s);
			}
			for (auto i = n; i--;) {
				digit u = p[i];
				if (s) {
					u = (u << s) | (i ? p[i - 1] >> (digit_bits - s) : 0);
				}
				auto t = static_cast<__uint128_t>(v) * r + ((static_cast<__uint128_t>(r) << digit_bits) | u);
				digit q = static_cast<digit>(t >> digit_bits) + 1;
				digit q0 = static_cast<digit>(t);
				r = u - q * dn;
				if (r > q0) {
					--q;
					r += dn;
				}
				if (r >= dn) {
					++q;
					r -= dn;
				}
				p[i] = q;
			}
			r >>= s;
			trim();
			return r;
		}
	#endif
		for (auto i = n; i--;) {
			r = _divmod(r, p[i], dv.d, &p[i]);
		}
		trim();
		return r;
	}

	digit divmod_small(digit divisor) {
		return divmod_small(small_divisor(divisor));
	}

	// The remainder of a[0..n) divided by the divisor, without storing the quotient.
	static digit k_mod_1(const digit* a, std::size_t n, const small_divisor& dv) {
		digit r = 0;
	#if defined HAVE____INT128_T
		if (digit_bits == 64) {
			// The 2-by-1 step of divmod_small, keeping only the remainder.
			const unsigned s = dv.shift;
			const digit dn = dv.dn;
			const digit v = dv.inv;
			if (s) {
				r = a[n - 1] >> (digit_bits - s);
			}
			for (auto i = n; i--;) {
				digit u = a[i];
				if (s) {
					u = (u << s) | (i ? a[i - 1] >> (digit_bits - s) : 0);
				}
				auto t = static_cast<__uint128_t>(v) * r + ((static_cast<__uint128_t>(r) << digit_bits) | u);
				digit q = static_cast<digit>(t >> digit_bits) + 1;
				digit q0 = static_cast<digit>(t);
				r = u - q * dn;
				if (r > q0) {
					r += dn;
				}
				if (r >= dn) {
					r -= dn;
				}
			}
			return r >> s;
		}
	#endif
		for (auto i = n; i--;) {
			digit q;
			r = _divmod(r, a[i], dv.d, &q);
		}
		return r;
	}

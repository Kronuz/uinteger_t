// Add, subtract, multiply and divide on values, and the small-divisor paths. Included by uinteger_t.hh.

#pragma once

#include "../uinteger_t.hh"

inline uinteger_t& uinteger_t::long_add(uinteger_t& lhs, const uinteger_t& rhs) {
	return long_add(lhs, lhs, rhs);
}

inline uinteger_t& uinteger_t::long_add(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
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

inline uinteger_t& uinteger_t::add(uinteger_t& lhs, const uinteger_t& rhs) {
	// First try saving some calculations:
	if (!rhs) {
		return lhs;
	}

	return long_add(lhs, rhs);
}

inline uinteger_t& uinteger_t::add(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
	return long_add(result, lhs, rhs);
}

inline uinteger_t uinteger_t::add(const uinteger_t& lhs, const uinteger_t& rhs) {
	uinteger_t result;
	add(result, lhs, rhs);
	return result;
}

inline uinteger_t& uinteger_t::long_sub(uinteger_t& lhs, const uinteger_t& rhs) {
	return long_sub(lhs, lhs, rhs);
}

inline uinteger_t& uinteger_t::long_sub(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
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

inline uinteger_t& uinteger_t::sub(uinteger_t& lhs, const uinteger_t& rhs) {
	// First try saving some calculations:
	if (!rhs) {
		return lhs;
	}

	return long_sub(lhs, rhs);
}

inline uinteger_t& uinteger_t::sub(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
	// First try saving some calculations:
	if (!rhs) {
		result = lhs;
		return result;
	}

	return long_sub(result, lhs, rhs);
}

inline uinteger_t uinteger_t::sub(const uinteger_t& lhs, const uinteger_t& rhs) {
	uinteger_t result;
	sub(result, lhs, rhs);
	return result;
}

inline uinteger_t& uinteger_t::long_mult(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
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

inline uinteger_t& uinteger_t::mult(uinteger_t& lhs, const uinteger_t& rhs) {
	// Hard to see how this could have a further optimized implementation.
	return mult(lhs, lhs, rhs);
}

inline uinteger_t& uinteger_t::mult(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
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

inline uinteger_t uinteger_t::mult(const uinteger_t& lhs, const uinteger_t& rhs) {
	uinteger_t result;
	mult(result, lhs, rhs);
	return result;
}

inline void uinteger_t::set_digit(digit v) {
	if (v) {
		_value.assign(std::size_t(1), v);
	} else {
		_value.clear();
	}
}

inline void uinteger_t::single_divmod(uinteger_t* quotient, uinteger_t* remainder, const uinteger_t& lhs, const uinteger_t& rhs) {
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

inline bool uinteger_t::k_div_uses_trunc(std::size_t m, std::size_t dn) {
	if (m <= dn / 2) {
		return m < 2 * div_trunc_threshold;
	}
	return m <= dn ? m < div_trunc_threshold : (m - dn <= dn && m < div_trunc_threshold / 2);
}

inline void uinteger_t::knuth_divmod(uinteger_t* quotient, uinteger_t* remainder, const uinteger_t& lhs, const uinteger_t& rhs) {
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

inline void uinteger_t::divide(uinteger_t* quotient, uinteger_t* remainder, const uinteger_t& lhs, const uinteger_t& rhs) {
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

inline std::pair<uinteger_t, uinteger_t> uinteger_t::divmod(const uinteger_t& lhs, const uinteger_t& rhs) {
	uinteger_t quotient;
	uinteger_t remainder;
	divmod(quotient, remainder, lhs, rhs);
	return std::make_pair(std::move(quotient), std::move(remainder));
}

inline uinteger_t::digit uinteger_t::divmod_small(const small_divisor& dv) {
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

inline uinteger_t::digit uinteger_t::divmod_small(digit divisor) {
	return divmod_small(small_divisor(divisor));
}

inline uinteger_t::digit uinteger_t::k_mod_1(const digit* a, std::size_t n, const small_divisor& dv) {
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

inline uinteger_t& uinteger_t::mul_add_small(digit mult, digit add) {
	auto n = size();
	auto p = data();
	digit carry = add;
	for (std::size_t i = 0; i < n; ++i) {
		carry = _multadd(p[i], mult, carry, 0, &p[i]);
	}
	if (carry) {
		append(carry);
	} else if (!mult) {
		trim();
	}
	return *this;
}

// Word primitives: bit length, widening multiply, two-by-one divide, add with carry and subtract with borrow. Included by uinteger_t.hh.

#pragma once

#include "../uinteger_t.hh"

inline uinteger_t::digit uinteger_t::_bits(digit x) {
#if defined HAVE____BUILTIN_CLZLL
	if (digit_octets == sizeof(unsigned long long)) {
		return x ? digit_bits - __builtin_clzll(x) : 1;
	}
#endif
#if defined HAVE____BUILTIN_CLZL
	if (digit_octets == sizeof(unsigned long)) {
		return x ? digit_bits - __builtin_clzl(x) : 1;
	}
#endif
#if defined HAVE____BUILTIN_CLZ
	if (digit_octets == sizeof(unsigned)) {
		return x ? digit_bits - __builtin_clz(x) : 1;
	}
#endif
	{
		digit c = x ? 0 : 1;
		while (x) {
			x >>= 1;
			++c;
		}
		return c;
	}
}

inline uinteger_t::digit uinteger_t::_mult(digit x, digit y, digit* lo) {
#if defined HAVE____INT128_T
	if (digit_bits == 64) {
		auto r = static_cast<__uint128_t>(x) * static_cast<__uint128_t>(y);
		*lo = static_cast<digit>(r);
		return static_cast<digit>(r >> digit_bits);
	}
#endif
#if defined HAVE___UMUL128
	if (digit_bits == 64) {
		unsigned __int64 h;
		*lo = static_cast<digit>(_umul128(static_cast<unsigned __int64>(x), static_cast<unsigned __int64>(y), &h));
		return static_cast<digit>(h);
	}
#endif
#if defined HAVE___UMULH
	if (digit_bits == 64) {
		*lo = static_cast<digit>(x * y);
		return static_cast<digit>(__umulh(static_cast<unsigned __int64>(x), static_cast<unsigned __int64>(y)));
	}
#endif
	if (digit_bits == 64) {
		// Four half-limb products; `mid` sums three values below 2^32, so it cannot wrap.
		const unsigned half = digit_bits / 2;
		const digit mask = (static_cast<digit>(1) << half) - 1;
		digit x0 = x & mask, x1 = x >> half;
		digit y0 = y & mask, y1 = y >> half;
		digit p00 = x0 * y0, p01 = x0 * y1, p10 = x1 * y0, p11 = x1 * y1;
		digit mid = (p00 >> half) + (p01 & mask) + (p10 & mask);
		*lo = (mid << half) | (p00 & mask);
		return p11 + (p01 >> half) + (p10 >> half) + (mid >> half);
	}
	if (digit_bits == 32) {
		auto r = static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(y);
		*lo = static_cast<digit>(r);
		return static_cast<digit>(r >> 32);
	}
	if (digit_bits == 16) {
		auto r = static_cast<std::uint32_t>(x) * static_cast<std::uint32_t>(y);
		*lo = static_cast<digit>(r);
		return static_cast<digit>(r >> 16);
	}
	auto r = static_cast<std::uint16_t>(x) * static_cast<std::uint16_t>(y);
	*lo = static_cast<digit>(r);
	return static_cast<digit>(r >> 8);
}

inline uinteger_t::digit uinteger_t::_mullo(digit x, digit y) {
	using wide = typename std::conditional<(sizeof(digit) < sizeof(unsigned)), unsigned, digit>::type;
	return static_cast<digit>(static_cast<wide>(x) * static_cast<wide>(y));
}

inline uinteger_t::digit uinteger_t::_multadd(digit x, digit y, digit a, digit c, digit* lo) {
#if defined HAVE____INT128_T
	if (digit_bits == 64) {
		// Add a before c, each with its own carry into the high limb: the carry chain through c is
		// one add and one adc. As one 128-bit sum the compiler adds a + c first, which puts three
		// dependent operations on the chain of every limb.
		auto p = static_cast<__uint128_t>(x) * static_cast<__uint128_t>(y);
		auto l = static_cast<digit>(p);
		auto h = static_cast<digit>(p >> digit_bits);
		digit s = static_cast<digit>(l + a);
		h += s < l;
		digit t = static_cast<digit>(s + c);
		h += t < s;
		*lo = t;
		return h;
	}
#endif
	if (digit_bits == 64) {
		digit l;
		digit h = _mult(x, y, &l);
		l += a;
		h += l < a;
		l += c;
		h += l < c;
		*lo = l;
		return h;
	}
	if (digit_bits == 32) {
		auto r = static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(y) + static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(c);
		*lo = static_cast<digit>(r);
		return static_cast<digit>(r >> 32);
	}
	if (digit_bits == 16) {
		auto r = static_cast<std::uint32_t>(x) * static_cast<std::uint32_t>(y) + static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(c);
		*lo = static_cast<digit>(r);
		return static_cast<digit>(r >> 16);
	}
	auto r = static_cast<std::uint32_t>(x) * static_cast<std::uint32_t>(y) + static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(c);
	*lo = static_cast<digit>(r);
	return static_cast<digit>(r >> 8);
}

inline uinteger_t::digit uinteger_t::_divmod(digit x_hi, digit x_lo, digit y, digit* result) {
#if defined HAVE____INT128_T
	if (digit_bits == 64) {
		auto x = static_cast<__uint128_t>(x_hi) << digit_bits | static_cast<__uint128_t>(x_lo);
		digit q = x / y;
		digit r = x % y;

		*result = q;
		return r;
	}
#endif
#if defined HAVE___UDIV128
	if (digit_bits == 64) {
		unsigned __int64 r;
		*result = static_cast<digit>(_udiv128(static_cast<unsigned __int64>(x_hi), static_cast<unsigned __int64>(x_lo), static_cast<unsigned __int64>(y), &r));
		return static_cast<digit>(r);
	}
#endif
	if (digit_bits == 64) {
		// Two-digit long division on half limbs (Knuth, TAOCP 4.3.1, Algorithm D with b = 2^32);
		// requires x_hi < y, so the quotient fits one limb.
		const unsigned half = digit_bits / 2;
		const digit b = static_cast<digit>(1) << half;
		const digit mask = b - 1;
		const unsigned s = static_cast<unsigned>(digit_bits - _bits(y));
		const digit v = y << s;
		const digit v1 = v >> half;
		const digit v0 = v & mask;
		const digit u32 = s ? (x_hi << s) | (x_lo >> (digit_bits - s)) : x_hi;
		const digit u10 = x_lo << s;
		const digit u1 = u10 >> half;
		const digit u0 = u10 & mask;

		digit q1 = u32 / v1;
		digit rhat = u32 - q1 * v1;
		while (q1 >= b || q1 * v0 > ((rhat << half) | u1)) {
			--q1;
			rhat += v1;
			if (rhat >= b) {
				break;
			}
		}
		const digit u21 = (u32 << half) + u1 - q1 * v;

		digit q0 = u21 / v1;
		rhat = u21 - q0 * v1;
		while (q0 >= b || q0 * v0 > ((rhat << half) | u0)) {
			--q0;
			rhat += v1;
			if (rhat >= b) {
				break;
			}
		}

		*result = (q1 << half) | q0;
		return ((u21 << half) + u0 - q0 * v) >> s;
	}
	if (digit_bits == 32) {
		auto x = static_cast<std::uint64_t>(x_hi) << 32 | static_cast<std::uint64_t>(x_lo);
		digit q = x / y;
		digit r = x % y;

		*result = q;
		return r;
	}
	if (digit_bits == 16) {
		auto x = static_cast<std::uint32_t>(x_hi) << 16 | static_cast<std::uint32_t>(x_lo);
		digit q = x / y;
		digit r = x % y;

		*result = q;
		return r;
	}
	if (digit_bits == 8) {
		auto x = static_cast<std::uint16_t>(x_hi) << 8 | static_cast<std::uint16_t>(x_lo);
		digit q = x / y;
		digit r = x % y;

		*result = q;
		return r;
	}
}

inline uinteger_t::digit uinteger_t::_addcarry(digit x, digit y, digit c, digit* result) {
#if defined HAVE___ADDCARRY_U64
	if (digit_bits == 64) {
		// _addcarry_u64(carry in (one bit), x, y, *sum) -> carry out
		unsigned __int64 r;
		auto carry = _addcarry_u64(static_cast<unsigned char>(c), static_cast<unsigned __int64>(x), static_cast<unsigned __int64>(y), &r);
		*result = static_cast<digit>(r);
		return carry;
	}
#endif
#if defined HAVE____BUILTIN_ADDCLL
	if (digit_octets == sizeof(unsigned long long)) {
		unsigned long long carryout;
		*result = __builtin_addcll(x, y, c, &carryout);  // __builtin_addcll(x, y, carryin, *carryout) -> sum
		return carryout;
	}
#endif
#if defined HAVE____BUILTIN_ADDCL
	if (digit_octets == sizeof(unsigned long)) {
		unsigned long carryout;
		*result = __builtin_addcl(x, y, c, &carryout);  // __builtin_addcl(x, y, carryin, *carryout) -> sum
		return carryout;
	}
#endif
#if defined HAVE____BUILTIN_ADDC
	if (digit_octets == sizeof(unsigned)) {
		unsigned carryout;
		*result = __builtin_addc(x, y, c, &carryout);  // __builtin_addc(x, y, carryin, *carryout) -> sum
		return carryout;
	}
#endif
#if defined HAVE____INT128_T
	if (digit_bits == 64) {
		auto r = static_cast<__uint128_t>(x) + static_cast<__uint128_t>(y) + static_cast<__uint128_t>(c);
		*result = r;
		return static_cast<bool>(r >> digit_bits);
	}
#endif
	if (digit_bits == 64) {
		const unsigned half = digit_bits / 2;
		const digit mask = (static_cast<digit>(1) << half) - 1;
		digit x0 = x & mask;
		digit x1 = x >> half;
		digit y0 = y & mask;
		digit y1 = y >> half;

		auto u = x0 + y0 + c;
		auto v = x1 + y1 + static_cast<bool>(u >> half);
		*result = (v << half) + (u & mask);
		return static_cast<bool>(v >> half);
	}
	if (digit_bits == 32) {
		auto r = static_cast<std::uint64_t>(x) + static_cast<std::uint64_t>(y) + static_cast<std::uint64_t>(c);
		*result = r;
		return static_cast<bool>(r >> 32);
	}
	if (digit_bits == 16) {
		auto r = static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(y) + static_cast<std::uint32_t>(c);
		*result = r;
		return static_cast<bool>(r >> 16);
	}
	if (digit_bits == 8) {
		auto r = static_cast<std::uint16_t>(x) + static_cast<std::uint16_t>(y) + static_cast<std::uint16_t>(c);
		*result = r;
		return static_cast<bool>(r >> 8);
	}
}

inline uinteger_t::digit uinteger_t::_subborrow(digit x, digit y, digit c, digit* result) {
#if defined HAVE___SUBBORROW_U64
	if (digit_bits == 64) {
		// _subborrow_u64(borrow in (one bit), x, y, *difference) -> borrow out
		unsigned __int64 r;
		auto borrow = _subborrow_u64(static_cast<unsigned char>(c), static_cast<unsigned __int64>(x), static_cast<unsigned __int64>(y), &r);
		*result = static_cast<digit>(r);
		return borrow;
	}
#endif
#if defined HAVE____BUILTIN_SUBCLL
	if (digit_octets == sizeof(unsigned long long)) {
		unsigned long long carryout;
		*result = __builtin_subcll(x, y, c, &carryout);  // __builtin_subcll(x, y, carryin, *carryout) -> sum
		return carryout;
	}
#endif
#if defined HAVE____BUILTIN_SUBCL
	if (digit_octets == sizeof(unsigned long)) {
		unsigned long carryout;
		*result = __builtin_subcl(x, y, c, &carryout);  // __builtin_subcl(x, y, carryin, *carryout) -> sum
		return carryout;
	}
#endif
#if defined HAVE____BUILTIN_SUBC
	if (digit_octets == sizeof(unsigned)) {
		unsigned carryout;
		*result = __builtin_subc(x, y, c, &carryout);  // __builtin_subc(x, y, carryin, *carryout) -> sum
		return carryout;
	}
#endif
#if defined HAVE____INT128_T
	if (digit_bits == 64) {
		auto r = static_cast<__uint128_t>(x) - static_cast<__uint128_t>(y) - static_cast<__uint128_t>(c);
		*result = r;
		return static_cast<bool>(r >> 64);
	}
#endif
	if (digit_bits == 64) {
		const unsigned half = digit_bits / 2;
		const digit mask = (static_cast<digit>(1) << half) - 1;
		digit x0 = x & mask;
		digit x1 = x >> half;
		digit y0 = y & mask;
		digit y1 = y >> half;

		auto u = x0 - y0 - c;
		auto v = x1 - y1 - static_cast<bool>(u >> half);
		*result = (v << half) + (u & mask);
		return static_cast<bool>(v >> half);
	}
	if (digit_bits == 32) {
		auto r = static_cast<std::uint64_t>(x) - static_cast<std::uint64_t>(y) - static_cast<std::uint64_t>(c);
		*result = r;
		return static_cast<bool>(r >> 32);
	}
	if (digit_bits == 16) {
		auto r = static_cast<std::uint32_t>(x) - static_cast<std::uint32_t>(y) - static_cast<std::uint32_t>(c);
		*result = r;
		return static_cast<bool>(r >> 16);
	}
	if (digit_bits == 8) {
		auto r = static_cast<std::uint16_t>(x) - static_cast<std::uint16_t>(y) - static_cast<std::uint16_t>(c);
		*result = r;
		return static_cast<bool>(r >> 8);
	}
}

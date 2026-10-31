/*
uinteger_t.hh
An arbitrary precision unsigned integer type for C++

Copyright (c) 2017,2019 German Mendez Bravo (Kronuz) @ german dot mb at gmail.com
Copyright (c) 2013 - 2017 Jason Lee @ calccrypto at gmail.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

With much help from Auston Sterling

Thanks to Stefan Deigmüller for finding
a bug in operator*.

Thanks to François Dessenne for convincing me
to do a general rewrite of this class.

Germán Mández Bravo (Kronuz) converted Jason Lee's uint128_t
to header-only and extended to arbitrary bit length.
*/

#ifndef __uint_t__
#define __uint_t__

#include <cassert>
#include <limits>
#include <vector>
#include <string>
#include <utility>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <functional>
#include <type_traits>

// Compatibility inlines
#ifndef __has_builtin         // Optional of course
#define __has_builtin(x) 0    // Compatibility with non-clang compilers
#endif

#if defined _MSC_VER
#  include <intrin.h>
#  if defined _M_X64
#    define HAVE___ADDCARRY_U64
#    define HAVE___SUBBORROW_U64
#    define HAVE___UMUL128
#    if _MSC_VER >= 1920
#      define HAVE___UDIV128
#    endif
#  elif defined _M_ARM64
#    define HAVE___UMULH
#  endif
#endif

#if (defined(__clang__) && __has_builtin(__builtin_clzll)) || (defined(__GNUC__ ) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)))
#  define HAVE____BUILTIN_CLZLL
#endif
#if (defined(__clang__) && __has_builtin(__builtin_clzl)) || (defined(__GNUC__ ) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)))
#  define HAVE____BUILTIN_CLZL
#endif
#if (defined(__clang__) && __has_builtin(__builtin_clz)) || (defined(__GNUC__ ) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)))
#  define HAVE____BUILTIN_CLZ
#endif
#if (defined(__clang__) && __has_builtin(__builtin_addcll))
#  define HAVE____BUILTIN_ADDCLL
#endif
#if (defined(__clang__) && __has_builtin(__builtin_addcl))
#  define HAVE____BUILTIN_ADDCL
#endif
#if (defined(__clang__) && __has_builtin(__builtin_addc))
#  define HAVE____BUILTIN_ADDC
#endif
#if (defined(__clang__) && __has_builtin(__builtin_subcll))
#  define HAVE____BUILTIN_SUBCLL
#endif
#if (defined(__clang__) && __has_builtin(__builtin_subcl))
#  define HAVE____BUILTIN_SUBCL
#endif
#if (defined(__clang__) && __has_builtin(__builtin_subc))
#  define HAVE____BUILTIN_SUBC
#endif

// Define UINTEGER_T_NO_INT128 to test the portable 64-bit limb paths.
#if defined __SIZEOF_INT128__ && !defined UINTEGER_T_NO_INT128
#define HAVE____INT128_T
#endif


#ifndef DIGIT_T
#define DIGIT_T        std::uint64_t
#endif

#ifndef HALF_DIGIT_T
#define HALF_DIGIT_T   std::uint32_t
#endif

class uinteger_t;

// uinteger_t is meant to drop into generic numeric code, so it deliberately
// specializes the standard numeric traits to advertise itself as an unsigned
// integer (so a `template <class T, enable_if_t<is_integral_v<T>>>` accepts it).
// The standard reserves these templates, and a recent libc++ marks them
// [[no_specializations]], making the specialization ill-formed (surfaced as
// -Winvalid-specialization). This is a knowing exception, so suppress that one
// diagnostic -- but ONLY where the compiler defines it: guarding on __has_warning
// avoids a -Wunknown-warning-option warning on toolchains (e.g. Apple clang 17)
// that accept the specialization and never had that flag.
#if defined(__clang__)
#  if __has_warning("-Winvalid-specialization")
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Winvalid-specialization"
#    define UINTEGER_T_RESTORE_DIAG
#  endif
#endif
namespace std {
	template <> struct is_arithmetic <uinteger_t> : std::true_type {};
	template <> struct is_integral   <uinteger_t> : std::true_type {};
	template <> struct is_unsigned   <uinteger_t> : std::true_type {};
}
#ifdef UINTEGER_T_RESTORE_DIAG
#  pragma clang diagnostic pop
#  undef UINTEGER_T_RESTORE_DIAG
#endif

class uinteger_t {
public:
	using digit = DIGIT_T;
	using half_digit = HALF_DIGIT_T;

	static constexpr std::size_t digit_octets = sizeof(digit);             // number of octets per digit
	static constexpr std::size_t digit_bits = digit_octets * 8;            // number of bits per digit
	static constexpr std::size_t half_digit_octets = sizeof(half_digit);   // number of octets per half_digit
	static constexpr std::size_t half_digit_bits = half_digit_octets * 8;  // number of bits per half_digit

	using container = std::vector<digit>;

	template <typename T>
	struct is_result {
		static const bool value = false;
	};

	template <typename T, typename Alloc>
	struct is_result<std::vector<T, Alloc>> {
		static const bool value = true;
	};

	template <typename charT, typename traits, typename Alloc>
	struct is_result<std::basic_string<charT, traits, Alloc>> {
		static const bool value = true;
	};

private:
	static_assert(digit_octets == half_digit_octets * 2, "half_digit must be exactly half the size of digit");

	static constexpr double growth_factor = 1.5;

	std::size_t _begin;
	std::size_t _end;
	container _value_instance;
	container& _value;
	bool _carry;

public:
	// Window to vector (uses _begin and _end)

	void reserve(std::size_t sz) {
		_value.reserve(sz + _begin);
	}

	std::size_t grow(std::size_t n) {
		// expands the vector using a growth factor
		// and returns the new capacity.
		auto cc = _value.capacity();
		if (n >= cc) {
			cc = n * growth_factor;
			_value.reserve(cc);
		}
		return cc;
	}

	void resize(std::size_t sz) {
		grow(sz + _begin);
		_value.resize(sz + _begin);
	}

	void resize(std::size_t sz, const digit& c) {
		grow(sz + _begin);
		_value.resize(sz + _begin, c);
	}

	void clear() {
		_value.clear();
		_begin = 0;
		_end = 0;
		_carry = false;
	}

	digit* data() noexcept {
		return _value.data() + _begin;
	}

	const digit* data() const noexcept {
		return _value.data() + _begin;
	}

	std::size_t size() const noexcept {
		return _end ? _end - _begin : _value.size() - _begin;
	}

	void prepend(std::size_t sz, const digit& c) {
		// Efficiently prepend by growing backwards by growth factor
		auto min = std::min(_begin, sz);
		if (min) {
			// If there is some space before `_begin`, we try using it first:
			_begin -= min;
			std::fill_n(_value.begin() + _begin, min, c);
			sz -= min;
		}
		if (sz) {
			assert(_begin == 0); // _begin should be 0 in here
			// If there's still more room needed, we grow the vector:
			// Ex.: grow using prepend(3, y)
			//    sz = 3
			//    _begin = 0  (B)
			//    _end = 1  (E)
			// initially (capacity == 12):
			//              |xxxxxxxxxx- |
			//              B           E
			// after reclaiming space after `_end` (same capacity == 12):
			//              |xxxxxxxxxx  |
			//              B
			//    _end = 0
			//    csz = 10
			// grow returns the new capacity (22)
			//    isz = 12  (22 - 10)
			//    _begin = 9  (12 - 3)
			// after (capacity == (12 + 3) * 1.5 == 22):
			//    |---------yyyxxxxxxxxxx|
			//              B
			if (_end) {
				// reclaim space after `_end`
				_value.resize(_end);
				_end = 0;
			}
			auto csz = _value.size();
			auto isz = grow(csz + sz) - csz;
			_value.insert(_value.begin(), isz, c);
			_begin = isz - sz;
		}
	}

	void prepend(const digit& c) {
		prepend(1, c);
	}

	void prepend(const uinteger_t& num) {
		prepend(num.size(), 0);
		std::copy(num.begin(), num.end(), begin());
	}

	void append(std::size_t sz, const digit& c) {
		// Efficiently append by growing by growth factor
		if (_end) {
			// reclaim space after `_end`
			_value.resize(_end);
			_end = 0;
		}
		auto nsz = _value.size() + sz;
		grow(nsz);
		_value.resize(nsz, c);
	}

	void append(const digit& c) {
		append(1, c);
	}

	void append(const uinteger_t& num) {
		auto sz = num.size();
		append(sz, 0);
		std::copy(num.begin(), num.end(), end() - sz);
	}

	container::iterator begin() noexcept {
		return _value.begin() + _begin;
	}

	container::const_iterator begin() const noexcept {
		return _value.cbegin() + _begin;
	}

	container::iterator end() noexcept {
		return _end ? _value.begin() + _end : _value.end();
	}

	container::const_iterator end() const noexcept {
		return _end ? _value.cbegin() + _end : _value.cend();
	}

	container::reverse_iterator rbegin() noexcept {
		return _end ? container::reverse_iterator(_value.begin() + _end) : _value.rbegin();
	}

	container::const_reverse_iterator rbegin() const noexcept {
		return _end ? container::const_reverse_iterator(_value.cbegin() + _end) : _value.crbegin();
	}

	container::reverse_iterator rend() noexcept {
		return container::reverse_iterator(_value.begin() + _begin);
	}

	container::const_reverse_iterator rend() const noexcept {
		return container::const_reverse_iterator(_value.cbegin() + _begin);
	}

	container::reference front() {
		return *begin();
	}

	container::const_reference front() const {
		return *begin();
	}

	container::reference back() {
		return *rbegin();
	}

	container::const_reference back() const {
		return *rbegin();
	}

private:
	// Optimized primitives for operations

	static digit _bits(digit x) {
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

	// (hi, lo) = x * y
	static digit _mult(digit x, digit y, digit* lo) {
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

	// (hi, lo) = x * y + a + c, which cannot overflow two limbs.
	static digit _multadd(digit x, digit y, digit a, digit c, digit* lo) {
	#if defined HAVE____INT128_T
		if (digit_bits == 64) {
			auto r = static_cast<__uint128_t>(x) * static_cast<__uint128_t>(y) + static_cast<__uint128_t>(a) + static_cast<__uint128_t>(c);
			*lo = static_cast<digit>(r);
			return static_cast<digit>(r >> digit_bits);
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

	static digit _divmod(digit x_hi, digit x_lo, digit y, digit* result) {
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
			// quotient
			digit q = x_lo << 1;

			// remainder
			digit r = x_hi;

			digit carry = x_lo >> 63;
			int i;

			for (i = 0; i < 64; i++) {
				auto tmp = r >> 63;
				r <<= 1;
				r |= carry;
				carry = tmp;

				if (carry == 0) {
					if (r >= y) {
						carry = 1;
					} else {
						tmp = q >> 63;
						q <<= 1;
						q |= carry;
						carry = tmp;
						continue;
					}
				}

				r -= y;
				r -= (1 - carry);
				carry = 1;
				tmp = q >> 63;
				q <<= 1;
				q |= carry;
				carry = tmp;
			}

			*result = q;
			return r;
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

	static digit _addcarry(digit x, digit y, digit c, digit* result) {
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
			digit x0 = x & 0xffffffffUL;
			digit x1 = x >> 32;
			digit y0 = y & 0xffffffffUL;
			digit y1 = y >> 32;

			auto u = x0 + y0 + c;
			auto v = x1 + y1 + static_cast<bool>(u >> 32);
			*result = (v << 32) + (u & 0xffffffffUL);
			return static_cast<bool>(v >> 32);
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

	static digit _subborrow(digit x, digit y, digit c, digit* result) {
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
			digit x0 = x & 0xffffffffUL;
			digit x1 = x >> 32;
			digit y0 = y & 0xffffffffUL;
			digit y1 = y >> 32;

			auto u = x0 - y0 - c;
			auto v = x1 - y1 - static_cast<bool>(u >> 32);
			*result = (v << 32) + (u & 0xffffffffUL);
			return static_cast<bool>(v >> 32);
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

	// Raw limb kernels over `digit*` arrays of explicit length (little-endian).

	static digit k_add_n(digit* r, const digit* a, const digit* b, std::size_t n) {
		digit c = 0;
		for (std::size_t i = 0; i < n; ++i) {
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
		for (std::size_t i = 0; i < n; ++i) {
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

	static digit k_submul_1(digit* r, const digit* a, std::size_t n, digit m) {
		digit hi = 0;
		digit b = 0;
		for (std::size_t i = 0; i < n; ++i) {
			digit lo;
			hi = _multadd(a[i], m, 0, hi, &lo);
			b = _subborrow(r[i], lo, b, &r[i]);
		}
		return static_cast<digit>(hi + b);
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

	// r[0..n) -= p[0..pn) with pn <= n, propagating the borrow.
	static void k_sub_at(digit* r, std::size_t n, const digit* p, std::size_t pn) {
		assert(pn <= n);
		digit c = k_sub_n(r, r, p, pn);
		if (c) {
			c = k_sub_1(r + pn, r + pn, n - pn, c);
		}
		assert(!c); (void)(c);
	}

	static constexpr std::size_t mul_karatsuba_threshold = 2048 / digit_bits;

	static std::size_t k_mul_scratch_size(std::size_t an, std::size_t bn) {
		if (bn < mul_karatsuba_threshold) {
			return 0;
		}
		std::size_t bits = 0;
		for (auto x = an; x; x >>= 1) {
			++bits;
		}
		return 6 * an + 16 * bits + 32;
	}

	// r[0..an+bn) = a * b for any sizes (an, bn >= 1); r must not overlap a or b.
	// `s` is scratch of at least k_mul_scratch_size(max, min) limbs when the smaller operand
	// is at or above the Karatsuba threshold.
	static void k_mul(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s) {
		if (an < bn) {
			std::swap(a, b);
			std::swap(an, bn);
		}
		if (bn < mul_karatsuba_threshold) {
			k_mul_basecase(r, a, an, b, bn);
			return;
		}

		if (an >= 2 * bn) {
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
			return;
		}

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

		const std::size_t a1n = an - h;
		const std::size_t b1n = bn - h;

		// z0 = a0 * b0 and z2 = a1 * b1 land directly in r.
		k_mul(r, a, h, b, h, s);
		k_mul(r + 2 * h, a + h, a1n, b + h, b1n, s);

		digit* sa = s;
		digit* sb = sa + h + 1;
		digit* p = sb + h + 1;
		s = p + 2 * h + 2;

		sa[h] = k_add_n(sa, a, a + h, a1n);
		if (a1n < h) {
			sa[h] = k_add_1(sa + a1n, a + a1n, h - a1n, sa[h]);
		}
		sb[h] = k_add_n(sb, b, b + h, b1n);
		if (b1n < h) {
			sb[h] = k_add_1(sb + b1n, b + b1n, h - b1n, sb[h]);
		}

		k_mul(p, sa, h + 1, sb, h + 1, s);

		// p = (a0 + a1)(b0 + b1) - z0 - z2 = a0 b1 + a1 b0
		k_sub_at(p, 2 * h + 2, r, 2 * h);
		k_sub_at(p, 2 * h + 2, r + 2 * h, a1n + b1n);

		std::size_t pn = 2 * h + 2;
		while (pn && !p[pn - 1]) {
			--pn;
		}
		k_add_at(r, an + bn, h, p, std::min(pn, an + bn - h));
	}

	// Division by invariant integers follows Moller and Granlund, "Improved division by
	// invariant integers" (IEEE Trans. Computers, 2011): reciprocal of a normalized limb,
	// of a normalized two-limb divisor, and the 3-by-2 division step.

	// Reciprocal of a normalized limb: floor((B^2 - 1) / d) - B.
	static digit k_inv_1(digit d) {
		digit q;
		_divmod(static_cast<digit>(~d), static_cast<digit>(~static_cast<digit>(0)), d, &q);
		return q;
	}

	// Reciprocal of a normalized two-limb divisor: floor((B^3 - 1) / (d1 B + d0)) - B.
	static digit k_inv_3by2(digit d1, digit d0) {
		digit v = k_inv_1(d1);
		// Refine so that v is the reciprocal for (d1, d0): add d0 into the product d1 * v,
		// correcting the estimate on each wrap-around.
		digit p = static_cast<digit>(d1 * v);
		digit sum = static_cast<digit>(p + d0);
		if (sum < p) {
			--v;
			if (sum >= d1) {
				--v;
				sum = static_cast<digit>(sum - d1);
			}
			sum = static_cast<digit>(sum - d1);
		}
		digit lo;
		digit hi = _mult(d0, v, &lo);
		digit top = static_cast<digit>(sum + hi);
		if (top < hi) {
			--v;
			if (top > d1 || (top == d1 && lo >= d0)) {
				--v;
			}
		}
		return v;
	}

	// Divides (n2,n1,n0) by the normalized (d1,d0) given v = k_inv_3by2(d1, d0), where
	// (n2,n1) < (d1,d0). Returns the quotient limb and the two-limb remainder.
	static digit k_div_3by2(digit n2, digit n1, digit n0, digit d1, digit d0, digit v, digit* rem1, digit* rem0) {
		digit q0;
		digit q1 = _mult(n2, v, &q0);
		q1 = static_cast<digit>(q1 + n2 + _addcarry(q0, n1, 0, &q0));

		// (r1, r0) = (n1 - d1 q1, n0) - (d1, d0) - d0 q1, all modulo B^2
		digit r1 = static_cast<digit>(n1 - static_cast<digit>(d1 * q1));
		digit r0;
		r1 = static_cast<digit>(r1 - d1 - _subborrow(n0, d0, 0, &r0));
		digit t0;
		digit t1 = _mult(d0, q1, &t0);
		r1 = static_cast<digit>(r1 - t1 - _subborrow(r0, t0, 0, &r0));

		q1 = static_cast<digit>(q1 + 1);
		if (r1 >= q0) {
			q1 = static_cast<digit>(q1 - 1);
			r1 = static_cast<digit>(r1 + d1 + _addcarry(r0, d0, 0, &r0));
		}
		if (r1 > d1 || (r1 == d1 && r0 >= d0)) {
			q1 = static_cast<digit>(q1 + 1);
			r1 = static_cast<digit>(r1 - d1 - _subborrow(r0, d0, 0, &r0));
		}
		*rem1 = r1;
		*rem0 = r0;
		return q1;
	}

	// Schoolbook division of np[0..nn) by the normalized dp[0..dn) (dn >= 2, top bit set).
	// Requires the top dn limbs of np to be below dp; writes nn-dn quotient limbs to q
	// and leaves the remainder in np[0..dn).
	static void k_div_sb(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn) {
		const digit d1 = dp[dn - 1];
		const digit d0 = dp[dn - 2];
		const digit dinv = k_inv_3by2(d1, d0);
		for (std::size_t i = nn - dn; i--;) {
			digit* w = np + i;
			digit qd;
			if (w[dn] == d1 && w[dn - 1] == d0) {
				qd = static_cast<digit>(~static_cast<digit>(0));
			} else {
				digit r1, r0;
				qd = k_div_3by2(w[dn], w[dn - 1], w[dn - 2], d1, d0, dinv, &r1, &r0);
			}
			digit cy = k_submul_1(w, dp, dn, qd);
			digit bor = _subborrow(w[dn], cy, 0, &w[dn]);
			if (bor) {
				--qd;
				digit c = k_add_n(w, w, dp, dn);
				w[dn] = static_cast<digit>(w[dn] + c);
			}
			q[i] = qd;
		}
	}

	// Helper functions

	void trim(digit mask = 0) {
		auto rit = rbegin();
		auto rit_e = rend();

		// Masks the last value of internal vector
		mask &= (digit_bits - 1);
		if (mask && rit != rit_e) {
			*rit &= (static_cast<digit>(1) << mask) - 1;
		}

		// Removes all unused zeros from the internal vector
		auto rit_f = std::find_if(rit, rit_e, [](const digit& c) { return c; });
		resize(rit_e - rit_f); // shrink
	}

	static constexpr char chr(int ord) {
		constexpr const char _[256] = {
			'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b',
			'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
			'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
		};
		return _[ord];
	}

	static constexpr int ord(int chr) {
		constexpr const int _[256] = {
			-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
			-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
			-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
			 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,

			-1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
			25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1,
			-1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
			25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1,

			-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
			-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
			-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
			-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

			-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
			-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
			-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
			-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		};
		return _[chr];
	}

public:
	static constexpr unsigned base_bits(int base) {
		constexpr const unsigned _[256] = {
			0, 1, 0, 2, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 4,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6,

			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7,

			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8,
		};
		return _[base - 1];
	}

	static constexpr unsigned base_size(int base) {
		constexpr const unsigned _[256] = {
			0, 64, 41, 32, 28, 25, 23, 22, 21, 20, 19, 18, 18, 17, 17, 16,
			16, 16, 16, 15, 15, 15, 15, 14, 14, 14, 14, 14, 14, 14, 13, 13,
			13, 13, 13, 13, 13, 13, 13, 13, 12, 12, 12, 12, 12, 12, 12, 12,
			12, 12, 12, 12, 12, 12, 12, 12, 11, 11, 11, 11, 11, 11, 11, 11,

			11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
			11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
			10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
			10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,

			10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  9,  9,  9,  9,  9,  9,
			9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
			9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
			9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,

			9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
			9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
			9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
			9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  8,
		};
		return _[base - 1];
	}

	static const uinteger_t& uint_0() {
		static uinteger_t uint_0(0);
		return uint_0;
	}

	static const uinteger_t& uint_1() {
		static uinteger_t uint_1(1);
		return uint_1;
	}

private:
	// Public Implementation
#ifdef UINT_T_PUBLIC_IMPLEMENTATION
public:
#endif
	static uinteger_t& bitwise_and(uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		if (lhs_sz > rhs_sz) {
			lhs.resize(rhs_sz); // shrink
		}

		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs.end();

		auto rhs_it = rhs.begin();

		for (; lhs_it != lhs_it_e; ++lhs_it, ++rhs_it) {
			*lhs_it &= *rhs_it;
		}

		// Finish up
		lhs.trim();
		return lhs;
	}

	static uinteger_t& bitwise_and(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		auto result_sz = std::max(lhs_sz, rhs_sz);
		result.resize(result_sz);

		// not using `end()` because resize of `result.resize()` could have
		// resized `lhs` or `rhs` if `result` is also either `rhs` or `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		auto rhs_it = rhs.begin();
		auto rhs_it_e = rhs_it + rhs_sz;

		auto it = result.begin();

		if (lhs_sz < rhs_sz) {
			for (; lhs_it != lhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				*it = *lhs_it & *rhs_it;
			}
			for (; rhs_it != rhs_it_e; ++rhs_it, ++it) {
				*it = 0;
			}
		} else {
			for (; rhs_it != rhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				*it = *lhs_it & *rhs_it;
			}
			for (; lhs_it != lhs_it_e; ++lhs_it, ++it) {
				*it = 0;
			}
		}

		// Finish up
		result.trim();
		return result;
	}

	static uinteger_t bitwise_and(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		bitwise_and(result, lhs, rhs);
		return result;
	}

	static uinteger_t& bitwise_or(uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		if (lhs_sz < rhs_sz) {
			lhs.resize(rhs_sz, 0); // grow
		}

		auto lhs_it = lhs.begin();

		auto rhs_it = rhs.begin();
		auto rhs_it_e = rhs.end();

		for (; rhs_it != rhs_it_e; ++lhs_it, ++rhs_it) {
			*lhs_it |= *rhs_it;
		}

		// Finish up
		lhs.trim();
		return lhs;
	}

	static uinteger_t& bitwise_or(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		auto result_sz = std::max(lhs_sz, rhs_sz);
		result.resize(result_sz);

		// not using `end()` because resize of `result.resize()` could have
		// resized `lhs` or `rhs` if `result` is also either `rhs` or `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		auto rhs_it = rhs.begin();
		auto rhs_it_e = rhs_it + rhs_sz;

		auto it = result.begin();

		if (lhs_sz < rhs_sz) {
			for (; lhs_it != lhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				*it = *lhs_it | *rhs_it;
			}
			for (; rhs_it != rhs_it_e; ++rhs_it, ++it) {
				*it = *rhs_it;
			}
		} else {
			for (; rhs_it != rhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				*it = *lhs_it | *rhs_it;
			}
			for (; lhs_it != lhs_it_e; ++lhs_it, ++it) {
				*it = *lhs_it;
			}
		}

		// Finish up
		result.trim();
		return result;
	}
	static uinteger_t bitwise_or(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		bitwise_or(result, lhs, rhs);
		return result;
	}

	static uinteger_t& bitwise_xor(uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		if (lhs_sz < rhs_sz) {
			lhs.resize(rhs_sz, 0); // grow
		}

		auto lhs_it = lhs.begin();

		auto rhs_it = rhs.begin();
		auto rhs_it_e = rhs.end();

		for (; rhs_it != rhs_it_e; ++lhs_it, ++rhs_it) {
			*lhs_it ^= *rhs_it;
		}

		// Finish up
		lhs.trim();
		return lhs;
	}

	static uinteger_t& bitwise_xor(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		auto result_sz = std::max(lhs_sz, rhs_sz);
		result.resize(result_sz);

		// not using `end()` because resize of `result.resize()` could have
		// resized `lhs` or `rhs` if `result` is also either `rhs` or `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		auto rhs_it = rhs.begin();
		auto rhs_it_e = rhs_it + rhs_sz;

		auto it = result.begin();

		if (lhs_sz < rhs_sz) {
			for (; lhs_it != lhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				*it = *lhs_it ^ *rhs_it;
			}
			for (; rhs_it != rhs_it_e; ++rhs_it, ++it) {
				*it = *rhs_it;
			}
		} else {
			for (; rhs_it != rhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				*it = *lhs_it ^ *rhs_it;
			}
			for (; lhs_it != lhs_it_e; ++lhs_it, ++it) {
				*it = *lhs_it;
			}
		}

		// Finish up
		result.trim();
		return result;
	}

	static uinteger_t bitwise_xor(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		bitwise_xor(result, lhs, rhs);
		return result;
	}

	static uinteger_t& bitwise_inv(uinteger_t& lhs) {
		auto lhs_sz = lhs.size();

		auto b = lhs.bits();

		if (!lhs_sz) {
			lhs.append(0);
		}

		// not using `end()` because resize of `result.resize()` could have
		// resized `lhs` if `result` is also `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		for (; lhs_it != lhs_it_e; ++lhs_it) {
			*lhs_it = ~*lhs_it;
		}

		// Finish up
		lhs.trim(b ? b : 1);
		return lhs;
	}

	static uinteger_t& bitwise_inv(uinteger_t& result, const uinteger_t& lhs) {
		auto lhs_sz = lhs.size();

		auto b = lhs.bits();

		auto result_sz = lhs_sz ? lhs_sz : 1;
		result.resize(result_sz);

		// not using `end()` because resize of `result.resize()` could have
		// resized `lhs` if `result` is also `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		auto it = result.begin();
		auto it_e = it + result_sz;

		for (; lhs_it != lhs_it_e; ++lhs_it, ++it) {
			*it = ~*lhs_it;
		}
		for (; it != it_e; ++it) {
			*it = ~static_cast<digit>(0);
		}

		// Finish up
		result.trim(b ? b : 1);
		return result;
	}

	static uinteger_t bitwise_inv(const uinteger_t& lhs) {
		uinteger_t result;
		bitwise_inv(result, lhs);
		return result;
	}

	static uinteger_t& bitwise_lshift(uinteger_t& lhs, const uinteger_t& rhs) {
		if (!rhs) {
			return lhs;
		}

		uinteger_t shifts_q;
		uinteger_t shifts_r;
		auto _digit_bits = digit_bits;
		auto uint_digit_bits = uinteger_t(_digit_bits);
		divmod(shifts_q, shifts_r, rhs, uint_digit_bits);
		std::size_t shifts = static_cast<std::size_t>(shifts_q);
		std::size_t shift = static_cast<std::size_t>(shifts_r);

		if (shifts) {
			lhs.prepend(shifts, 0);
		}
		if (shift) {
			digit shifted = 0;
			auto lhs_it = lhs.begin() + shifts;
			auto lhs_it_e = lhs.end();
			for (; lhs_it != lhs_it_e; ++lhs_it) {
				auto v = (*lhs_it << shift) | shifted;
				shifted = *lhs_it >> (_digit_bits - shift);
				*lhs_it = v;
			}
			if (shifted) {
				lhs.append(shifted);
			}
		}

		// Finish up
		lhs.trim();
		return lhs;
	}

	static uinteger_t& bitwise_lshift(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		if (&result._value == &lhs._value) {
			bitwise_lshift(result, rhs);
			return result;
		}
		if (!rhs) {
			result = lhs;
			return result;
		}

		auto lhs_sz = lhs.size();

		uinteger_t shifts_q;
		uinteger_t shifts_r;
		auto _digit_bits = digit_bits;
		auto uint_digit_bits = uinteger_t(_digit_bits);
		divmod(shifts_q, shifts_r, rhs, uint_digit_bits);
		std::size_t shifts = static_cast<std::size_t>(shifts_q);
		std::size_t shift = static_cast<std::size_t>(shifts_r);

		auto result_sz = lhs_sz + shifts;
		result.grow(result_sz + 1);
		result.resize(shifts, 0);
		result.resize(result_sz);

		// not using `end()` because resize of `result.resize()` could have
		// resized `lhs` if `result` is also `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		auto it = result.begin() + shifts;

		if (shift) {
			digit shifted = 0;
			for (; lhs_it != lhs_it_e; ++lhs_it, ++it) {
				auto v = (*lhs_it << shift) | shifted;
				shifted = *lhs_it >> (_digit_bits - shift);
				*it = v;
			}
			if (shifted) {
				result.append(shifted);
			}
		} else {
			for (; lhs_it != lhs_it_e; ++lhs_it, ++it) {
				*it = *lhs_it;
			}
		}

		// Finish up
		result.trim();
		return result;
	}

	static uinteger_t bitwise_lshift(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		bitwise_lshift(result, lhs, rhs);
		return result;
	}

	static uinteger_t& bitwise_rshift(uinteger_t& lhs, const uinteger_t& rhs) {
		if (!rhs) {
			return lhs;
		}

		auto lhs_sz = lhs.size();

		auto _digit_bits = digit_bits;
		if (compare(rhs, uinteger_t(lhs_sz * _digit_bits)) >= 0) {
			lhs = uint_0();
			return lhs;
		}

		uinteger_t shifts_q;
		uinteger_t shifts_r;
		auto uint_digit_bits = uinteger_t(_digit_bits);
		divmod(shifts_q, shifts_r, rhs, uint_digit_bits);
		std::size_t shifts = static_cast<std::size_t>(shifts_q);
		std::size_t shift = static_cast<std::size_t>(shifts_r);

		if (shifts) {
			lhs._begin += shifts;
		}
		if (shift) {
			digit shifted = 0;
			auto lhs_rit = lhs.rbegin();
			auto lhs_rit_e = lhs.rend();
			for (; lhs_rit != lhs_rit_e; ++lhs_rit) {
				auto v = (*lhs_rit >> shift) | shifted;
				shifted = *lhs_rit << (_digit_bits - shift);
				*lhs_rit = v;
			}
			lhs.trim();
		}

		return lhs;
	}

	static uinteger_t& bitwise_rshift(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		if (&result._value == &lhs._value) {
			bitwise_lshift(result, rhs);
			return result;
		}
		if (!rhs) {
			result = lhs;
			return result;
		}

		auto lhs_sz = lhs.size();

		auto _digit_bits = digit_bits;
		if (compare(rhs, uinteger_t(lhs_sz * _digit_bits)) >= 0) {
			result = uint_0();
			return result;
		}

		uinteger_t shifts_q;
		uinteger_t shifts_r;
		auto uint_digit_bits = uinteger_t(_digit_bits);
		divmod(shifts_q, shifts_r, rhs, uint_digit_bits);
		std::size_t shifts = static_cast<std::size_t>(shifts_q);
		std::size_t shift = static_cast<std::size_t>(shifts_r);

		auto result_sz = lhs_sz - shifts;
		result.resize(result_sz);

		// not using `end()` because resize of `result.resize()` could have
		// resized `lhs` if `result` is also `lhs`.
		auto lhs_rit = lhs.rbegin();
		auto lhs_rit_e = lhs_rit + lhs_sz - shifts;

		auto rit = result.rbegin();
		auto rit_e = rit + result_sz;

		if (shift) {
			digit shifted = 0;
			for (; lhs_rit != lhs_rit_e; ++lhs_rit, ++rit) {
				assert(rit != rit_e); (void)(rit_e);
				auto v = (*lhs_rit >> shift) | shifted;
				shifted = *lhs_rit << (_digit_bits - shift);
				*rit = v;
			}
		} else {
			for (; lhs_rit != lhs_rit_e; ++lhs_rit, ++rit) {
				assert(rit != rit_e); (void)(rit_e);
				*rit = *lhs_rit;
			}
		}

		// Finish up
		result.trim();
		return result;
	}

	static uinteger_t bitwise_rshift(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		bitwise_rshift(result, lhs, rhs);
		return result;
	}

	static int compare(const uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		if (lhs_sz > rhs_sz) return 1;
		if (lhs_sz < rhs_sz) return -1;

		auto lhs_rit = lhs.rbegin();
		auto lhs_rit_e = lhs.rend();

		auto rhs_rit = rhs.rbegin();

		for (; lhs_rit != lhs_rit_e && *lhs_rit == *rhs_rit; ++lhs_rit, ++rhs_rit);

		if (lhs_rit != lhs_rit_e) {
			if (*lhs_rit > *rhs_rit) return 1;
			if (*lhs_rit < *rhs_rit) return -1;
		}

		return 0;
	}

	static uinteger_t& long_add(uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		if (lhs_sz < rhs_sz) {
			lhs.reserve(rhs_sz + 1);
			lhs.resize(rhs_sz, 0); // grow
		}

		// not using `end()` because resize of `lhs.resize()` could have
		// resized `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		auto rhs_it = rhs.begin();
		auto rhs_it_e = rhs_it + rhs_sz;

		digit carry = 0;
		if (lhs_sz < rhs_sz) {
			for (; lhs_it != lhs_it_e; ++rhs_it, ++lhs_it) {
				carry = _addcarry(*lhs_it, *rhs_it, carry, &*lhs_it);
			}
			for (; carry && rhs_it != rhs_it_e; ++rhs_it, ++lhs_it) {
				carry = _addcarry(0, *rhs_it, carry, &*lhs_it);
			}
			for (; rhs_it != rhs_it_e; ++rhs_it, ++lhs_it) {
				*lhs_it = *rhs_it;
			}
		} else {
			for (; rhs_it != rhs_it_e; ++rhs_it, ++lhs_it) {
				carry = _addcarry(*lhs_it, *rhs_it, carry, &*lhs_it);
			}
			for (; carry && lhs_it != lhs_it_e; ++lhs_it) {
				carry = _addcarry(*lhs_it, 0, carry, &*lhs_it);
			}
		}

		if (carry) {
			lhs.append(1);
		}

		lhs._carry = false;

		// Finish up
		lhs.trim();
		return lhs;
	}

	static uinteger_t& long_add(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		auto result_sz = std::max(lhs_sz, rhs_sz);
		result.reserve(result_sz + 1);
		result.resize(result_sz, 0);

		// not using `end()` because resize of `result.resize()` could have
		// resized `lhs` or `rhs` if `result` is also either `rhs` or `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		auto rhs_it = rhs.begin();
		auto rhs_it_e = rhs_it + rhs_sz;

		auto it = result.begin();

		digit carry = 0;
		if (lhs_sz < rhs_sz) {
			for (; lhs_it != lhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				carry = _addcarry(*lhs_it, *rhs_it, carry, &*it);
			}
			for (; carry && rhs_it != rhs_it_e; ++rhs_it, ++it) {
				carry = _addcarry(0, *rhs_it, carry, &*it);
			}
			for (; rhs_it != rhs_it_e; ++rhs_it, ++it) {
				*it = *rhs_it;
			}
		} else {
			for (; rhs_it != rhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				carry = _addcarry(*lhs_it, *rhs_it, carry, &*it);
			}
			for (; carry && lhs_it != lhs_it_e; ++lhs_it, ++it) {
				carry = _addcarry(*lhs_it, 0, carry, &*it);
			}
			for (; lhs_it != lhs_it_e; ++lhs_it, ++it) {
				*it = *lhs_it;
			}
		}

		if (carry) {
			result.append(1);
		}
		result._carry = false;

		// Finish up
		result.trim();
		return result;
	}

	static uinteger_t& add(uinteger_t& lhs, const uinteger_t& rhs) {
		// First try saving some calculations:
		if (!rhs) {
			return lhs;
		}
		if (!lhs) {
			lhs = rhs;
			return lhs;
		}

		return long_add(lhs, rhs);
	}

	static uinteger_t& add(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		// First try saving some calculations:
		if (!rhs) {
			result = lhs;
			return result;
		}
		if (!lhs) {
			result = rhs;
			return result;
		}

		return long_add(result, lhs, rhs);
	}

	static uinteger_t add(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		add(result, lhs, rhs);
		return result;
	}

	static uinteger_t& long_sub(uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		if (lhs_sz < rhs_sz) {
			lhs.resize(rhs_sz, 0); // grow
		}

		// not using `end()` because resize of `lhs.resize()` could have
		// resized `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		auto rhs_it = rhs.begin();
		auto rhs_it_e = rhs_it + rhs_sz;

		digit borrow = 0;
		if (lhs_sz < rhs_sz) {
			for (; lhs_it != lhs_it_e; ++lhs_it, ++rhs_it) {
				borrow = _subborrow(*lhs_it, *rhs_it, borrow, &*lhs_it);
			}
			for (; rhs_it != rhs_it_e; ++lhs_it, ++rhs_it) {
				borrow = _subborrow(0, *rhs_it, borrow, &*lhs_it);
			}
		} else {
			for (; rhs_it != rhs_it_e; ++lhs_it, ++rhs_it) {
				borrow = _subborrow(*lhs_it, *rhs_it, borrow, &*lhs_it);
			}
			for (; borrow && lhs_it != lhs_it_e; ++lhs_it) {
				borrow = _subborrow(*lhs_it, 0, borrow, &*lhs_it);
			}
		}

		lhs._carry = borrow;

		// Finish up
		lhs.trim();
		return lhs;
	}

	static uinteger_t& long_sub(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		auto result_sz = std::max(lhs_sz, rhs_sz);
		result.resize(result_sz, 0);

		// not using `end()` because resize of `result.resize()` could have
		// resized `lhs` or `rhs` if `result` is also either `rhs` or `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		auto rhs_it = rhs.begin();
		auto rhs_it_e = rhs_it + rhs_sz;

		auto it = result.begin();

		digit borrow = 0;
		if (lhs_sz < rhs_sz) {
			for (; lhs_it != lhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				borrow = _subborrow(*lhs_it, *rhs_it, borrow, &*it);
			}
			for (; rhs_it != rhs_it_e; ++rhs_it, ++it) {
				borrow = _subborrow(0, *rhs_it, borrow, &*it);
			}
		} else {
			for (; rhs_it != rhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				borrow = _subborrow(*lhs_it, *rhs_it, borrow, &*it);
			}
			for (; borrow && lhs_it != lhs_it_e; ++lhs_it, ++it) {
				borrow = _subborrow(*lhs_it, 0, borrow, &*it);
			}
			for (; lhs_it != lhs_it_e; ++lhs_it, ++it) {
				*it = *lhs_it;
			}
		}

		result._carry = borrow;

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

	// Multiplication on raw limbs (schoolbook, Karatsuba and chunked for lopsided operands)
	static uinteger_t& long_mult(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		auto an = lhs.size();
		auto bn = rhs.size();
		const digit* a = lhs.data();
		const digit* b = rhs.data();
		if (an < bn) {
			std::swap(a, b);
			std::swap(an, bn);
		}

		uinteger_t tmp;
		tmp.resize(an + bn, 0);
		auto scratch_sz = k_mul_scratch_size(an, bn);
		if (scratch_sz) {
			std::vector<digit> scratch(scratch_sz);
			k_mul(tmp.data(), a, an, b, bn, scratch.data());
		} else {
			k_mul_basecase(tmp.data(), a, an, b, bn);
		}

		result = std::move(tmp);
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
			result = uint_0();
			return result;
		}
		if (compare(lhs, uint_1()) == 0) {
			result = rhs;
			return result;
		}
		if (compare(rhs, uint_1()) == 0) {
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

	// Single word division
	static std::pair<std::reference_wrapper<uinteger_t>, std::reference_wrapper<uinteger_t>> single_divmod(uinteger_t& quotient, uinteger_t& remainder, const uinteger_t& lhs, const uinteger_t& rhs) {
		assert(rhs.size() == 1);
		uinteger_t q(lhs);
		digit r = q.divmod_small(rhs.front());
		quotient = std::move(q);
		remainder = r;
		return std::make_pair(std::ref(quotient), std::ref(remainder));
	}

	// Knuth's Algorithm D on raw limbs, with a reciprocal-based quotient digit estimate
	static std::pair<std::reference_wrapper<uinteger_t>, std::reference_wrapper<uinteger_t>> knuth_divmod(uinteger_t& quotient, uinteger_t& remainder, const uinteger_t& lhs, const uinteger_t& rhs) {
		auto nn = lhs.size();
		auto dn = rhs.size();
		assert(nn >= dn && dn >= 2);

		// Normalize: shift the divisor so its top bit is set, and the dividend alike.
		const unsigned s = static_cast<unsigned>(digit_bits - _bits(rhs.back()));
		uinteger_t d;
		d.resize(dn, 0);
		uinteger_t n;
		n.resize(nn + 1, 0);
		if (s) {
			k_lshift(d.data(), rhs.data(), dn, s);
			n.data()[nn] = k_lshift(n.data(), lhs.data(), nn, s);
		} else {
			std::copy(rhs.data(), rhs.data() + dn, d.data());
			std::copy(lhs.data(), lhs.data() + nn, n.data());
		}

		uinteger_t q;
		q.resize(nn + 1 - dn, 0);
		k_div_sb(q.data(), n.data(), nn + 1, d.data(), dn);

		if (s) {
			k_rshift(n.data(), n.data(), dn, s);
		}
		n.resize(dn);
		q.trim();
		n.trim();

		quotient = std::move(q);
		remainder = std::move(n);
		return std::make_pair(std::ref(quotient), std::ref(remainder));
	}

	static std::pair<std::reference_wrapper<uinteger_t>, std::reference_wrapper<uinteger_t>> divmod(uinteger_t& quotient, uinteger_t& remainder, const uinteger_t& lhs, const uinteger_t& rhs) {
		// First try saving some calculations:
		if (!rhs) {
			throw std::domain_error("Error: division or modulus by 0");
		}
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();
		if (lhs_sz == 1 && rhs_sz == 1) {
			// Fast division and modulo for single value
			auto a = *lhs.begin();
			auto b = *rhs.begin();
			quotient = a / b;
			remainder = a % b;
			return std::make_pair(std::ref(quotient), std::ref(remainder));
		}
		if (compare(rhs, uint_1()) == 0) {
			quotient = lhs;
			remainder = uint_0();
			return std::make_pair(std::ref(quotient), std::ref(remainder));
		}
		auto compared = compare(lhs, rhs);
		if (compared == 0) {
			quotient = uint_1();
			remainder = uint_0();
			return std::make_pair(std::ref(quotient), std::ref(remainder));
		}
		if (!lhs || compared < 0) {
			quotient = uint_0();
			remainder = lhs;
			return std::make_pair(std::ref(quotient), std::ref(remainder));
		}
		if (rhs_sz == 1) {
			return single_divmod(quotient, remainder, lhs, rhs);
		}

		return knuth_divmod(quotient, remainder, lhs, rhs);
	}

	static std::pair<uinteger_t, uinteger_t> divmod(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t quotient;
		uinteger_t remainder;
		divmod(quotient, remainder, lhs, rhs);
		return std::make_pair(std::move(quotient), std::move(remainder));
	}

private:
	// In-place single-limb primitives (for owned values)
#ifdef UINT_T_PUBLIC_IMPLEMENTATION
public:
#endif

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

	// Sets this value to `this * mult + add` in place.
	uinteger_t& mul_add_small(digit mult, digit add) {
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

	// Largest power of `base` that fits in a digit, and its exponent.
	static digit base_chunk(int base, unsigned& count) {
		const digit max = std::numeric_limits<digit>::max();
		digit chunk = 1;
		count = 0;
		while (chunk <= max / static_cast<digit>(base)) {
			chunk *= static_cast<digit>(base);
			++count;
		}
		return chunk;
	}

	// Size arithmetic for the bit-sliced conversions. A count that does not fit in `Size`
	// throws std::length_error instead of wrapping (a 32-bit size_t reaches it at 512 MiB
	// of base-256 digits). Production code uses std::size_t; tests pass narrower sizes.

	// Limbs that hold `n` digits of `bits` bits each.
	template <typename Size>
	static Size _limbs_for_digits(Size n, unsigned bits) {
		const auto max = static_cast<unsigned long long>(std::numeric_limits<Size>::max());
		if (static_cast<unsigned long long>(n) > (max - (digit_bits - 1)) / bits) {
			throw std::length_error("Error: Too many digits");
		}
		return static_cast<Size>((static_cast<unsigned long long>(n) * bits + digit_bits - 1) / digit_bits);
	}

	// Digits of `bits` bits each in a value of `n` >= 1 limbs whose top limb has `top`
	// significant bits.
	template <typename Size>
	static Size _digits_for_limbs(Size n, unsigned top, unsigned bits) {
		const auto max = static_cast<unsigned long long>(std::numeric_limits<Size>::max());
		if (max < top + bits - 1 || static_cast<unsigned long long>(n) - 1 > (max - top - (bits - 1)) / digit_bits) {
			throw std::length_error("Error: Value too large to convert");
		}
		const auto nbits = (static_cast<unsigned long long>(n) - 1) * digit_bits + top;
		return static_cast<Size>((nbits + bits - 1) / bits);
	}

	// Builds a value from `n` digits in `base` (2..256), most significant first;
	// `get(i)` returns digit i and throws when it is not valid.
	template <typename Get>
	static uinteger_t _from_digits(std::size_t n, int base, const Get& get) {
		if (base < 2 || base > 256) {
			throw std::invalid_argument("Error: Cannot convert from base " + std::to_string(base));
		}
		uinteger_t result;
		if (!n) {
			return result;
		}
		if (const auto bits_per_digit = base_bits(base)) {
			// Pack each digit straight into its place in the limbs.
			const std::size_t limbs = _limbs_for_digits<std::size_t>(n, bits_per_digit);
			result.resize(limbs, 0);
			digit* p = result.data();
			for (std::size_t i = 0; i < n; ++i) {
				const digit d = get(i);
				const std::size_t offset = (n - 1 - i) * bits_per_digit;
				const std::size_t shift = offset % digit_bits;
				const std::size_t limb = offset / digit_bits;
				p[limb] |= static_cast<digit>(d << shift);
				if (shift + bits_per_digit > digit_bits) {
					p[limb + 1] |= static_cast<digit>(d >> (digit_bits - shift));
				}
			}
			result.trim();
		} else {
			// Accumulate a limb's worth of digits and fold it in with one pass.
			unsigned chunk_digits;
			const digit chunk_max = base_chunk(base, chunk_digits);
			result.reserve(n / chunk_digits + 1);
			digit chunk = 1;
			digit acc = 0;
			unsigned count = 0;
			for (std::size_t i = 0; i < n; ++i) {
				acc = acc * static_cast<digit>(base) + get(i);
				chunk *= static_cast<digit>(base);
				if (++count == chunk_digits) {
					result.mul_add_small(chunk_max, acc);
					chunk = 1;
					acc = 0;
					count = 0;
				}
			}
			if (count) {
				result.mul_add_small(chunk, acc);
			}
		}
		return result;
	}

private:
	// Constructors

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	void _uint_t(const T& value) {
		append(static_cast<digit>(value));
	}

	template <typename T, typename... Args, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	void _uint_t(const T& value, Args... args) {
		_uint_t(args...);
		append(static_cast<digit>(value));
	}

	// This constructor creates a window view of the _value
	uinteger_t(const uinteger_t& o, std::size_t begin, std::size_t end) :
		_begin(begin),
		_end(end),
		_value(o._value),
		_carry(o._carry) { }

public:
	uinteger_t() :
		_begin(0),
		_end(0),
		_value(_value_instance),
		_carry(false) { }

	uinteger_t(const uinteger_t& o) :
		_begin(0),
		_end(0),
		_value_instance(o.begin(), o.end()),
		_value(_value_instance),
		_carry(o._carry) { }

	uinteger_t(uinteger_t&& o) :
		_begin(std::move(o._begin)),
		_end(std::move(o._end)),
		_value_instance(std::move(o._value_instance)),
		_value(_value_instance),
		_carry(std::move(o._carry)) { }

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t(const T& value) :
		_begin(0),
		_end(0),
		_value(_value_instance),
		_carry(false) {
		if (value) {
			append(static_cast<digit>(value));
		}
	}

	template <typename T, typename... Args, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t(const T& value, Args... args) :
		_begin(0),
		_end(0),
		_value(_value_instance),
		_carry(false) {
		_uint_t(args...);
		append(static_cast<digit>(value));
		trim();
	}

	template <typename T, typename... Args, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t(std::initializer_list<T> list) :
		_begin(0),
		_end(0),
		_value(_value_instance),
		_carry(false) {
		reserve(list.size());
		for (const auto& value : list) {
			append(static_cast<digit>(value));
		}
		trim();
	}

	template <typename T, std::size_t N>
	explicit uinteger_t(T (&&s)[N], int base=10) :
		uinteger_t(s, N - 1, base) { }

	explicit uinteger_t(const unsigned char* bytes, std::size_t sz, int base) :
		uinteger_t(strtouint(bytes, sz, base)) { }

	explicit uinteger_t(const char* bytes, std::size_t sz, int base) :
		uinteger_t(strtouint(bytes, sz, base)) { }

	template <typename T>
	explicit uinteger_t(const std::vector<T>& bytes, int base=10) :
		uinteger_t(bytes.data(), bytes.size(), base) { }

	explicit uinteger_t(const std::string& bytes, int base=10) :
		uinteger_t(bytes.data(), bytes.size(), base) { }

	// Assignment Operator
	uinteger_t& operator=(const uinteger_t& o) {
		_begin = 0;
		_end = 0;
		_value = container(o.begin(), o.end());
		_carry = o._carry;
		return *this;
	}
	uinteger_t& operator=(uinteger_t&& o) {
		_begin = std::move(o._begin);
		_end = std::move(o._end);
		_value_instance = std::move(o._value_instance);
		_carry = std::move(o._carry);
		return *this;
	}

	// Typecast Operators
	explicit operator bool() const {
		return static_cast<bool>(size());
	}
	explicit operator unsigned char() const {
		return static_cast<unsigned char>(size() ? front() : 0);
	}
	explicit operator unsigned short() const {
		return static_cast<unsigned short>(size() ? front() : 0);
	}
	explicit operator unsigned int() const {
		return static_cast<unsigned int>(size() ? front() : 0);
	}
	explicit operator unsigned long() const {
		return static_cast<unsigned long>(size() ? front() : 0);
	}
	explicit operator unsigned long long() const {
		return static_cast<unsigned long long>(size() ? front() : 0);
	}
	explicit operator char() const {
		return static_cast<char>(size() ? front() : 0);
	}
	explicit operator short() const {
		return static_cast<short>(size() ? front() : 0);
	}
	explicit operator int() const {
		return static_cast<int>(size() ? front() : 0);
	}
	explicit operator long() const {
		return static_cast<long>(size() ? front() : 0);
	}
	explicit operator long long() const {
		return static_cast<long long>(size() ? front() : 0);
	}

	// Bitwise Operators
	uinteger_t operator&(const uinteger_t& rhs) const {
		return bitwise_and(*this, rhs);
	}

	uinteger_t& operator&=(const uinteger_t& rhs) {
		return bitwise_and(*this, rhs);
	}

	uinteger_t operator|(const uinteger_t& rhs) const {
		return bitwise_or(*this, rhs);
	}

	uinteger_t& operator|=(const uinteger_t& rhs) {
		return bitwise_or(*this, rhs);
	}

	uinteger_t operator^(const uinteger_t& rhs) const {
		return bitwise_xor(*this, rhs);
	}

	uinteger_t& operator^=(const uinteger_t& rhs) {
		return bitwise_xor(*this, rhs);
	}

	uinteger_t operator~() const {
		return bitwise_inv(*this);
	}

	uinteger_t inv() {
		return bitwise_inv(*this);
	}

	// Bit Shift Operators
	uinteger_t operator<<(const uinteger_t& rhs) const {
		return bitwise_lshift(*this, rhs);
	}

	uinteger_t& operator<<=(const uinteger_t& rhs) {
		return bitwise_lshift(*this, rhs);
	}

	uinteger_t operator>>(const uinteger_t& rhs) const {
		return bitwise_rshift(*this, rhs);
	}

	uinteger_t& operator>>=(const uinteger_t& rhs) {
		return bitwise_rshift(*this, rhs);
	}

	// Logical Operators
	bool operator!() const {
		return !static_cast<bool>(*this);
	}

	bool operator&&(const uinteger_t& rhs) const {
		return static_cast<bool>(*this) && rhs;
	}

	bool operator||(const uinteger_t& rhs) const {
		return static_cast<bool>(*this) || rhs;
	}

	// Comparison Operators
	bool operator==(const uinteger_t& rhs) const {
		return compare(*this, rhs) == 0;
	}

	bool operator!=(const uinteger_t& rhs) const {
		return compare(*this, rhs) != 0;
	}

	bool operator>(const uinteger_t& rhs) const {
		return compare(*this, rhs) > 0;
	}

	bool operator<(const uinteger_t& rhs) const {
		return compare(*this, rhs) < 0;
	}

	bool operator>=(const uinteger_t& rhs) const {
		return compare(*this, rhs) >= 0;
	}

	bool operator<=(const uinteger_t& rhs) const {
		return compare(*this, rhs) <= 0;
	}

	// Arithmetic Operators
	uinteger_t operator+(const uinteger_t& rhs) const {
		return add(*this, rhs);
	}

	uinteger_t& operator+=(const uinteger_t& rhs) {
		return add(*this, rhs);
	}

	uinteger_t operator-(const uinteger_t& rhs) const {
		return sub(*this, rhs);
	}

	uinteger_t& operator-=(const uinteger_t& rhs) {
		return sub(*this, rhs);
	}

	uinteger_t operator*(const uinteger_t& rhs) const {
		return mult(*this, rhs);
	}

	uinteger_t& operator*=(const uinteger_t& rhs) {
		return mult(*this, rhs);
	}

	std::pair<uinteger_t, uinteger_t> divmod(const uinteger_t& rhs) const {
		return divmod(*this, rhs);
	}

	uinteger_t operator/(const uinteger_t& rhs) const {
		return divmod(*this, rhs).first;
	}

	uinteger_t& operator/=(const uinteger_t& rhs) {
		uinteger_t quotient;
		uinteger_t remainder;
		divmod(quotient, remainder, *this, rhs);
		*this = std::move(quotient);
		return *this;
	}

	uinteger_t operator%(const uinteger_t& rhs) const {
		return divmod(*this, rhs).second;
	}

	uinteger_t& operator%=(const uinteger_t& rhs) {
		uinteger_t quotient;
		uinteger_t remainder;
		divmod(quotient, remainder, *this, rhs);
		*this = std::move(remainder);
		return *this;
	}

	// Increment Operator
	uinteger_t& operator++() {
		return *this += uint_1();
	}
	uinteger_t operator++(int) {
		uinteger_t temp(*this);
		++*this;
		return temp;
	}

	// Decrement Operator
	uinteger_t& operator--() {
		return *this -= uint_1();
	}
	uinteger_t operator--(int) {
		uinteger_t temp(*this);
		--*this;
		return temp;
	}

	// Nothing done since promotion doesn't work here
	uinteger_t operator+() const {
		return *this;
	}

	// two's complement
	uinteger_t operator-() const {
		return uint_0() - *this;
	}

	// Get private value at index
	const digit& value(std::size_t idx) const {
		static const digit zero = 0;
		return idx < size() ? *(begin() + idx) : zero;
	}

	// Get value of bit N
	bool operator[](std::size_t n) const {
		auto nd = n / digit_bits;
		auto nm = n % digit_bits;
		return nd < size() ? (*(begin() + nd) >> nm) & 1 : 0;
	}

	// Get bitsize of value
	std::size_t bits() const {
		auto sz = size();
		if (sz) {
			return _bits(back()) + (sz - 1) * digit_bits;
		}
		return 0;
	}

	// Appends the digits of the value in `base` (2..256) to `out` as byte values
	// 0..base-1, most significant first. Zero gives a single 0 digit.
	template <typename Result, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	void digits(Result& out, int base) const {
		using value_type = typename Result::value_type;
		if (base < 2 || base > 256) {
			throw std::invalid_argument("Base must be in the range [2, 256]");
		}
		const digit* p = data();
		auto n = size();
		while (n && !p[n - 1]) {
			--n;
		}
		if (!n) {
			out.push_back(static_cast<value_type>(0));
			return;
		}
		const auto start = out.size();
		if (const auto bits_per_digit = base_bits(base)) {
			// Slice the digits straight out of the limbs.
			const digit mask = (static_cast<digit>(1) << bits_per_digit) - 1;
			const std::size_t count = _digits_for_limbs<std::size_t>(n, static_cast<unsigned>(_bits(p[n - 1])), bits_per_digit);
			if (count > out.max_size() - start) {
				throw std::length_error("Error: Value too large to convert");
			}
			out.resize(start + count);
			std::size_t limb = 0;
			unsigned shift = 0;
			for (std::size_t i = count; i--;) {
				digit v = p[limb] >> shift;
				shift += bits_per_digit;
				if (shift >= digit_bits) {
					// The digit ends this limb; a straddling one continues in the next.
					shift -= digit_bits;
					++limb;
					if (shift && limb < n) {
						v |= static_cast<digit>(p[limb] << (bits_per_digit - shift));
					}
				}
				out[start + i] = static_cast<value_type>(v & mask);
			}
		} else {
			// Peel a limb's worth of digits per pass, least significant first. The
			// reserve is only an estimate, so it is skipped when it would not fit.
			const std::size_t per_limb = static_cast<std::size_t>(base_size(base)) * digit_bits;
			if (n <= (std::numeric_limits<std::size_t>::max() - 63 - start) / per_limb) {
				const std::size_t wanted = start + (n * per_limb + 63) / 64;
				if (out.capacity() < wanted) {
					out.reserve(wanted);
				}
			}
			unsigned chunk_digits;
			const small_divisor chunk(base_chunk(base, chunk_digits));
			uinteger_t quotient = *this;
			do {
				auto r = quotient.divmod_small(chunk);
				if (quotient) {
					for (unsigned i = 0; i < chunk_digits; ++i) {
						out.push_back(static_cast<value_type>(r % static_cast<digit>(base)));
						r /= static_cast<digit>(base);
					}
				} else {
					do {
						out.push_back(static_cast<value_type>(r % static_cast<digit>(base)));
						r /= static_cast<digit>(base);
					} while (r);
				}
			} while (quotient);
			std::reverse(out.begin() + start, out.end());
		}
	}

	// The inverse of digits(): `n` digits in `base` (2..256), most significant first,
	// one byte each. Throws std::invalid_argument on a digit >= base.
	template <typename T, typename = std::enable_if_t<std::is_integral<T>::value && sizeof(T) == 1>>
	static uinteger_t from_digits(const T* src, std::size_t n, int base) {
		return _from_digits(n, base, [src, n, base](std::size_t i) {
			const auto d = static_cast<digit>(static_cast<unsigned char>(src[i]));
			if (d >= static_cast<digit>(base)) {
				throw std::invalid_argument("Error: Not a digit in base " + std::to_string(base) + ": " + std::to_string(d) + " at " + std::to_string(n - i));
			}
			return d;
		});
	}

	// Get string representation of value
	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result str(int alphabet_base = 10) const {
		if (alphabet_base < 2 || (alphabet_base > 36 && alphabet_base != 256)) {
			throw std::invalid_argument("Base must be in the range [2, 36]");
		}
		Result result;
		digits(result, alphabet_base);
		if (alphabet_base != 256) {
			for (auto& c : result) {
				c = chr(static_cast<int>(c));
			}
		}
		return result;
	}

	static uinteger_t strtouint(const void* encoded, std::size_t encoded_size, int alphabet_base) {
		const char* data = (const char *)encoded;
		uinteger_t result;

		if (alphabet_base >= 2 && alphabet_base <= 36) {
			result = _from_digits(encoded_size, alphabet_base, [data, encoded_size, alphabet_base](std::size_t i) {
				const auto d = ord(static_cast<unsigned char>(data[i]));
				if (d < 0 || d >= alphabet_base) {
					throw std::invalid_argument("Error: Not a digit in base " + std::to_string(alphabet_base) + ": '" + std::string(1, data[i]) + "' at " + std::to_string(encoded_size - i));
				}
				return static_cast<digit>(d);
			});
		} else if (encoded_size && alphabet_base == 256) {
			auto value_size = encoded_size / digit_octets;
			auto value_padding = encoded_size % digit_octets;
			if (value_padding) {
				value_padding = digit_octets - value_padding;
				++value_size;
			}
			result.resize(value_size); // grow (no initialization)
			*result.begin() = 0; // initialize value
			auto ptr = reinterpret_cast<char*>(result.data());
			std::copy(data, data + encoded_size, ptr + value_padding);
			std::reverse(ptr, ptr + value_size * digit_octets);
		} else {
			throw std::invalid_argument("Error: Cannot convert from base " + std::to_string(alphabet_base));
		}

		return result;
	}

	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result bin() const {
		return str<Result>(2);
	}

	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result oct() const {
		return str<Result>(8);
	}

	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result hex() const {
		return str<Result>(16);
	}

	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result raw() const {
		return str<Result>(256);
	}
};

namespace std {  // This is probably not a good idea
	// Make it work with std::string()
	inline std::string to_string(uinteger_t& num) {
		return num.str();
	}
	inline const std::string to_string(const uinteger_t& num) {
		return num.str();
	}
}

// lhs type T as first arguemnt
// If the output is not a bool, casts to type T

// Bitwise Operators
template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator&(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) & rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator&=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(rhs & lhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator|(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) | rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator|=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(rhs | lhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator^(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) ^ rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator^=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(rhs ^ lhs);
}

// Bitshift operators
template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
inline uinteger_t operator<<(T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) << rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator<<=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(lhs << rhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
inline uinteger_t operator>>(T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) >> rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator>>=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(lhs >> rhs);
}

// Comparison Operators
template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
bool operator==(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) == rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
bool operator!=(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) != rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
bool operator>(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) > rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
bool operator<(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) < rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
bool operator>=(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) >= rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
bool operator<=(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) <= rhs;
}

// Arithmetic Operators
template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator+(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) + rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator+=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(rhs + lhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator-(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) - rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator-=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(lhs - rhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator*(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) * rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator*=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(rhs * lhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator/(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) / rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator/=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(lhs / rhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator%(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) % rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator%=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(lhs % rhs);
}

// IO Operator
inline std::ostream& operator<<(std::ostream& stream, const uinteger_t& rhs) {
	if (stream.flags() & stream.oct) {
		stream << rhs.str(8);
	} else if (stream.flags() & stream.dec) {
		stream << rhs.str(10);
	} else if (stream.flags() & stream.hex) {
		stream << rhs.str(16);
	}
	return stream;
}

#endif

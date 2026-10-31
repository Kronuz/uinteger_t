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
#include <atomic>
#include <limits>
#include <memory>
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

	// The limbs, least significant first, with no leading zero limb (zero is empty).
	container _value;

public:
	// Limb storage

	void reserve(std::size_t sz) {
		_value.reserve(sz);
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
		_value.resize(sz);
	}

	void resize(std::size_t sz, const digit& c) {
		_value.resize(sz, c);
	}

	void clear() {
		_value.clear();
	}

	digit* data() noexcept {
		return _value.data();
	}

	const digit* data() const noexcept {
		return _value.data();
	}

	std::size_t size() const noexcept {
		return _value.size();
	}

	void prepend(std::size_t sz, const digit& c) {
		_value.insert(_value.begin(), sz, c);
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
		return _value.begin();
	}

	container::const_iterator begin() const noexcept {
		return _value.cbegin();
	}

	container::iterator end() noexcept {
		return _value.end();
	}

	container::const_iterator end() const noexcept {
		return _value.cend();
	}

	container::reverse_iterator rbegin() noexcept {
		return _value.rbegin();
	}

	container::const_reverse_iterator rbegin() const noexcept {
		return _value.crbegin();
	}

	container::reverse_iterator rend() noexcept {
		return _value.rend();
	}

	container::const_reverse_iterator rend() const noexcept {
		return _value.crend();
	}

	container::reference front() {
		return _value.front();
	}

	container::const_reference front() const {
		return _value.front();
	}

	container::reference back() {
		return _value.back();
	}

	container::const_reference back() const {
		return _value.back();
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

	// The low limb of x * y. Limbs narrower than int would promote to signed int and overflow
	// (undefined behavior), so the product is taken in an unsigned type at least as wide as int.
	static digit _mullo(digit x, digit y) {
		using wide = typename std::conditional<(sizeof(digit) < sizeof(unsigned)), unsigned, digit>::type;
		return static_cast<digit>(static_cast<wide>(x) * static_cast<wide>(y));
	}

	// (hi, lo) = x * y + a + c, which cannot overflow two limbs.
	static digit _multadd(digit x, digit y, digit a, digit c, digit* lo) {
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

			const unsigned top = digit_bits - 1;
			digit carry = x_lo >> top;
			int i;

			for (i = 0; i < 64; i++) {
				auto tmp = r >> top;
				r <<= 1;
				r |= carry;
				carry = tmp;

				if (carry == 0) {
					if (r >= y) {
						carry = 1;
					} else {
						tmp = q >> top;
						q <<= 1;
						q |= carry;
						carry = tmp;
						continue;
					}
				}

				r -= y;
				r -= (1 - carry);
				carry = 1;
				tmp = q >> top;
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

#ifdef UINT_T_PUBLIC_IMPLEMENTATION
public:
#endif
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

	// r[0..n) += p[0..pn) mod B^n, for pn <= n: the carry out is dropped.
	static void k_add_wrap(digit* r, std::size_t n, const digit* p, std::size_t pn) {
		digit c = k_add_n(r, r, p, pn);
		if (c && n > pn) {
			k_add_1(r + pn, r + pn, n - pn, c);
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

	// Algorithm cutoffs in limbs: the multiplication ones compare against the smaller operand. They
	// are limb counts, not bit counts: the crossovers are set by the cost of a limb product against
	// a limb add, which is the same for 64- and 32-bit limbs (swept on Apple M4 with bench/tune.cc).
	// Each can be set by macro; the tests force every path at small sizes with tiny values.
#ifdef UINTEGER_T_MUL_KARATSUBA_THRESHOLD
	static constexpr std::size_t mul_karatsuba_threshold = UINTEGER_T_MUL_KARATSUBA_THRESHOLD;
#else
	static constexpr std::size_t mul_karatsuba_threshold = 24;
#endif
#ifdef UINTEGER_T_MUL_TOOM3_THRESHOLD
	static constexpr std::size_t mul_toom3_threshold = UINTEGER_T_MUL_TOOM3_THRESHOLD;
#else
	static constexpr std::size_t mul_toom3_threshold = 140;
#endif
#ifdef UINTEGER_T_SQR_KARATSUBA_THRESHOLD
	static constexpr std::size_t sqr_karatsuba_threshold = UINTEGER_T_SQR_KARATSUBA_THRESHOLD;
#else
	static constexpr std::size_t sqr_karatsuba_threshold = 40;
#endif
#ifdef UINTEGER_T_SQR_TOOM3_THRESHOLD
	static constexpr std::size_t sqr_toom3_threshold = UINTEGER_T_SQR_TOOM3_THRESHOLD;
#else
	static constexpr std::size_t sqr_toom3_threshold = 300;
#endif
	static_assert(mul_karatsuba_threshold >= 2 && sqr_karatsuba_threshold >= 2, "a Karatsuba threshold below 2 limbs would recurse forever");

	// Toom-3 splits the larger operand into thirds of k limbs, and needs a smaller operand with
	// a third piece (bn > 2k) and enough limbs to pay for it.
	static bool k_mul_uses_toom3(std::size_t an, std::size_t bn) {
		return bn >= mul_toom3_threshold && bn > 2 * ((an + 2) / 3);
	}

	static bool k_sqr_uses_toom3(std::size_t n) {
		return n >= sqr_toom3_threshold && n > 2 * ((n + 2) / 3);
	}

	// Scratch limbs k_mul needs for operands of an and bn limbs; zero below the Karatsuba threshold.
	// It follows the same choices as k_mul, level by level.
	static std::size_t k_mul_scratch_size(std::size_t an, std::size_t bn) {
		if (an < bn) {
			std::swap(an, bn);
		}
		if (bn < mul_karatsuba_threshold) {
			return 0;
		}
		if (an >= 2 * bn) {
			std::size_t need = k_mul_scratch_size(bn, bn);
			if (an % bn) {
				need = std::max(need, k_mul_scratch_size(bn, an % bn));
			}
			return 2 * bn + need;
		}
		if (k_mul_uses_toom3(an, bn)) {
			const std::size_t k = (an + 2) / 3;
			const std::size_t m = k + 1;
			std::size_t need = std::max(k_mul_scratch_size(k, k), k_mul_scratch_size(an - 2 * k, bn - 2 * k));
			return std::max(need, 13 * m + k_mul_scratch_size(m, m));
		}
		const std::size_t h = (an + 1) >> 1;
		if (bn <= h) {
			return an + bn + std::max(k_mul_scratch_size(h, bn), k_mul_scratch_size(an - h, bn));
		}
		std::size_t need = std::max(k_mul_scratch_size(h, h), k_mul_scratch_size(an - h, bn - h));
		return std::max(need, 4 * h + k_mul_scratch_size(h, h));
	}

	// Scratch limbs k_sqr needs for an n-limb operand; zero below the squaring Karatsuba threshold.
	static std::size_t k_sqr_scratch_size(std::size_t n) {
		if (n < sqr_karatsuba_threshold) {
			return 0;
		}
		if (k_sqr_uses_toom3(n)) {
			const std::size_t k = (n + 2) / 3;
			const std::size_t m = k + 1;
			std::size_t need = std::max(k_sqr_scratch_size(k), k_sqr_scratch_size(n - 2 * k));
			return std::max(need, 10 * m + k_sqr_scratch_size(m));
		}
		const std::size_t h = (n + 1) >> 1;
		std::size_t need = std::max(k_sqr_scratch_size(h), k_sqr_scratch_size(n - h));
		return std::max(need, 3 * h + k_sqr_scratch_size(h));
	}

	// With x = x0 + x1 B^k + x2 B^2k (x0 and x1 of k limbs, x2 of x2n <= k limbs, x2n >= 1), the
	// evaluations of the Toom-3 polynomial at 1, -1 and -2, as m = k + 1 limbs each: p1 = x(1),
	// pm1 = |x(-1)|, pm2 = |x(-2)|. `w` is m limbs of temporary space. Returns the signs: bit 0 is set
	// when x(-1) is negative, bit 1 when x(-2) is.
	static unsigned k_toom3_eval(digit* p1, digit* pm1, digit* pm2, digit* w, const digit* x, std::size_t k, std::size_t x2n) {
		const std::size_t m = k + 1;
		const digit* x1 = x + k;
		const digit* x2 = x + 2 * k;
		// pm1 = x0 + x2, p1 = pm1 + x1, pm1 = |pm1 - x1|
		digit c = k_add_n(pm1, x, x2, x2n);
		if (x2n < k) {
			c = k_add_1(pm1 + x2n, x + x2n, k - x2n, c);
		}
		pm1[k] = c;
		c = k_add_n(p1, pm1, x1, k);
		p1[k] = static_cast<digit>(pm1[k] + c);
		unsigned neg = static_cast<unsigned>(k_absdiff(pm1, pm1, m, x1, k));
		// pm2 = |x0 + 4 x2 - 2 x1|
		pm2[x2n] = k_lshift(pm2, x2, x2n, 2);
		std::fill(pm2 + x2n + 1, pm2 + m, digit(0));
		k_add_at(pm2, m, 0, x, k);
		w[k] = k_lshift(w, x1, k, 1);
		neg |= static_cast<unsigned>(k_absdiff(pm2, pm2, m, w, m)) << 1;
		return neg;
	}

	// Toom-3 interpolation (Bodrato and Zanoni, "Integer and Polynomial Multiplication: Towards Optimal
	// Toom-Cook Matrices", ISSAC 2007), for a product c0 + c1 B^k + c2 B^2k + c3 B^3k + c4 B^4k. On entry
	// r[0..2k) holds c0 and r[4k..4k + vn) holds c4, with r[2k..4k) zero; v1, vm1 and vm2 are the
	// N = 2k + 2 limb products at 1, -1 and -2, vm1 and vm2 meant negative when m1neg and m2neg are
	// set. They are consumed: the middle coefficients are added into r (of rn limbs) at k, 2k and 3k.
	// Every step is exact on the non-negative coefficients; the intermediates may be negative, so they
	// are taken mod B^N in two's complement and the divisions by 2 are only done on non-negative values.
	static void k_toom3_interp(digit* r, std::size_t rn, std::size_t k, std::size_t vn, digit* v1, digit* vm1, digit* vm2, bool m1neg, bool m2neg) {
		const std::size_t n = 2 * k + 2;
		const digit* v0 = r;
		const digit* vinf = r + 4 * k;
		if (m1neg) {
			k_neg_n(vm1, vm1, n);
		}
		if (m2neg) {
			k_neg_n(vm2, vm2, n);
		}
		k_sub_n(vm2, vm2, v1, n);               // vm2 - v1 = 3 (-c1 + c2 - 3 c3 + 5 c4)
		k_divexact_3(vm2, vm2, n);              // t3 = -c1 + c2 - 3 c3 + 5 c4
		k_sub_n(v1, v1, vm1, n);                // v1 - vm1 = 2 (c1 + c3)
		k_rshift(v1, v1, n, 1);                 // t1 = c1 + c3
		k_sub_wrap(vm1, n, v0, 2 * k);          // t2 = vm1 - c0 = -c1 + c2 - c3 + c4
		k_sub_n(vm2, vm1, vm2, n);              // t2 - t3 = 2 c3 - 4 c4
		digit c = k_addmul_1(vm2, vinf, vn, 4); // + 4 c4 = 2 c3
		c = _addcarry(vm2[vn], c, 0, &vm2[vn]);
		k_add_1(vm2 + vn + 1, vm2 + vn + 1, n - vn - 1, c);
		k_rshift(vm2, vm2, n, 1);               // c3
		k_add_n(vm1, vm1, v1, n);               // t2 + t1 = c2 + c4
		k_sub_wrap(vm1, n, vinf, vn);           // c2
		k_sub_n(v1, v1, vm2, n);                // c1 = t1 - c3
		auto add = [&](const digit* p, std::size_t at) {
			std::size_t pn = n;
			while (pn && !p[pn - 1]) {
				--pn;
			}
			k_add_at(r, rn, at, p, pn);
		};
		add(v1, k);
		add(vm1, 2 * k);
		add(vm2, 3 * k);
	}

	// r[0..an+bn) = a * b by Toom-3, for an >= bn > 2k with k = ceil(an / 3): both operands split
	// into three pieces, the product read off five products at 0, 1, -1, -2 and infinity.
	static void k_mul_toom3(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s) {
		const std::size_t k = (an + 2) / 3;
		const std::size_t m = k + 1;
		const std::size_t n = 2 * m;
		const std::size_t a2n = an - 2 * k;
		const std::size_t b2n = bn - 2 * k;
		assert(an >= bn && b2n >= 1);

		// c0 = a0 b0 and c4 = a2 b2 land directly in r.
		k_mul(r, a, k, b, k, s);
		k_mul(r + 4 * k, a + 2 * k, a2n, b + 2 * k, b2n, s);
		std::fill(r + 2 * k, r + 4 * k, digit(0));

		digit* v1 = s;
		digit* vm1 = v1 + n;
		digit* vm2 = vm1 + n;
		digit* a1 = vm2 + n;
		digit* am1 = a1 + m;
		digit* am2 = am1 + m;
		digit* b1 = am2 + m;
		digit* bm1 = b1 + m;
		digit* bm2 = bm1 + m;
		digit* w = bm2 + m;
		s = w + m;

		unsigned sa = k_toom3_eval(a1, am1, am2, w, a, k, a2n);
		unsigned sb = k_toom3_eval(b1, bm1, bm2, w, b, k, b2n);
		k_mul(v1, a1, m, b1, m, s);
		k_mul(vm1, am1, m, bm1, m, s);
		k_mul(vm2, am2, m, bm2, m, s);
		unsigned sign = sa ^ sb;
		k_toom3_interp(r, an + bn, k, a2n + b2n, v1, vm1, vm2, sign & 1, sign & 2);
	}

	// r[0..2n) = a^2 by Toom-3, for n > 2k with k = ceil(n / 3).
	static void k_sqr_toom3(digit* r, const digit* a, std::size_t n, digit* s) {
		const std::size_t k = (n + 2) / 3;
		const std::size_t m = k + 1;
		const std::size_t pn = 2 * m;
		const std::size_t a2n = n - 2 * k;
		assert(a2n >= 1);

		k_sqr(r, a, k, s);
		k_sqr(r + 4 * k, a + 2 * k, a2n, s);
		std::fill(r + 2 * k, r + 4 * k, digit(0));

		digit* v1 = s;
		digit* vm1 = v1 + pn;
		digit* vm2 = vm1 + pn;
		digit* a1 = vm2 + pn;
		digit* am1 = a1 + m;
		digit* am2 = am1 + m;
		digit* w = am2 + m;
		s = w + m;

		k_toom3_eval(a1, am1, am2, w, a, k, a2n);
		k_sqr(v1, a1, m, s);
		k_sqr(vm1, am1, m, s);
		k_sqr(vm2, am2, m, s);
		k_toom3_interp(r, 2 * n, k, 2 * a2n, v1, vm1, vm2, false, false);
	}

	// r[0..2n) = a^2 by Karatsuba: with a = a0 + a1 B^h, the middle term 2 a0 a1 is
	// a0^2 + a1^2 - (a0 - a1)^2, so the third square is of h limbs and has no carry limb.
	static void k_sqr_karatsuba(digit* r, const digit* a, std::size_t n, digit* s) {
		const std::size_t h = (n + 1) >> 1;
		const std::size_t a1n = n - h;

		k_sqr(r, a, h, s);
		k_sqr(r + 2 * h, a + h, a1n, s);

		digit* d = s;
		digit* p = d + h;
		s = p + 2 * h;
		k_absdiff(d, a, h, a + h, a1n);
		k_sqr(p, d, h, s);

		// p = z0 + z2 - p, in 2h limbs and a top carry limb.
		digit top = k_sub_n(p, r, p, 2 * h);
		digit c = k_add_n(p, p, r + 2 * h, 2 * a1n);
		if (2 * a1n < 2 * h) {
			c = k_add_1(p + 2 * a1n, p + 2 * a1n, 2 * (h - a1n), c);
		}
		top = static_cast<digit>(c - top);
		std::size_t pn = 2 * h;
		while (pn && !p[pn - 1]) {
			--pn;
		}
		k_add_at(r, 2 * n, h, p, pn);
		if (top) {
			digit t = k_add_1(r + 3 * h, r + 3 * h, 2 * n - 3 * h, top);
			assert(!t); (void)(t);
		}
	}

	// r[0..2n) = a^2 for n >= 1; r must not overlap a. `s` is scratch of k_sqr_scratch_size(n) limbs.
	static void k_sqr(digit* r, const digit* a, std::size_t n, digit* s) {
		if (n < sqr_karatsuba_threshold) {
			k_sqr_basecase(r, a, n);
		} else if (k_sqr_uses_toom3(n)) {
			k_sqr_toom3(r, a, n, s);
		} else {
			k_sqr_karatsuba(r, a, n, s);
		}
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
		} else if (k_mul_uses_toom3(an, bn)) {
			k_mul_toom3(r, a, an, b, bn, s);
		} else {
			k_mul_karatsuba(r, a, an, b, bn, s);
		}
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
		digit p = _mullo(d1, v);
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
		digit r1 = static_cast<digit>(n1 - _mullo(d1, q1));
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

	static int k_cmp_n(const digit* a, const digit* b, std::size_t n) {
		while (n--) {
			if (a[n] != b[n]) {
				return a[n] < b[n] ? -1 : 1;
			}
		}
		return 0;
	}

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

	// Helper functions

	void trim(digit mask = 0) {
		auto n = _value.size();
		digit* p = _value.data();

		// Masks the last value of internal vector
		mask &= (digit_bits - 1);
		if (mask && n) {
			p[n - 1] &= (static_cast<digit>(1) << mask) - 1;
		}

		// Removes all unused zeros from the internal vector
		auto m = n;
		while (m && !p[m - 1]) {
			--m;
		}
		if (m != n) {
			_value.resize(m); // shrink
		}
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

	// Shifts by `n` bits. The limb count is `n / digit_bits` and the bit count `n % digit_bits`
	// (digit_bits is a power of two), so there is no division and no temporary. `result` may be
	// `lhs`: the kernels move limbs in the direction that reads each one before it is overwritten.
	static uinteger_t& lshift(uinteger_t& result, const uinteger_t& lhs, digit n) {
		const std::size_t an = lhs.size();
		if (!an) {
			result.clear();
			return result;
		}
		if (!n) {
			if (&result != &lhs) {
				result = lhs;
			}
			return result;
		}
		const unsigned long long limbs = n / digit_bits;
		const unsigned bits = static_cast<unsigned>(n % digit_bits);
		const auto max = static_cast<unsigned long long>(result._value.max_size());
		if (limbs >= max - an) {
			throw std::length_error("Error: Shift too large");
		}
		// A bit shift needs one more limb exactly when the top limb's bits overflow it.
		const std::size_t extra = bits && _bits(lhs.back()) + bits > digit_bits;
		const std::size_t rn = an + static_cast<std::size_t>(limbs) + extra;

		result.resize(rn);
		digit* r = result.data();
		const digit* a = lhs.data();
		if (bits) {
			const digit out = k_lshift(r + limbs, a, an, bits);
			if (extra) {
				r[rn - 1] = out;
			}
		} else {
			std::copy_backward(a, a + an, r + rn);
		}
		std::fill_n(r, limbs, digit(0));

		// Finish up
		result.trim();
		return result;
	}

	// Shifts right by `n` bits, discarding the bits shifted out.
	static uinteger_t& rshift(uinteger_t& result, const uinteger_t& lhs, digit n) {
		const std::size_t an = lhs.size();
		const unsigned long long limbs = n / digit_bits;
		if (limbs >= an) {
			result.clear();
			return result;
		}
		if (!n) {
			if (&result != &lhs) {
				result = lhs;
			}
			return result;
		}
		const unsigned bits = static_cast<unsigned>(n % digit_bits);
		const std::size_t rn = an - static_cast<std::size_t>(limbs);

		if (&result != &lhs) {
			result.resize(rn);
		}
		digit* r = result.data();
		const digit* a = lhs.data() + limbs;
		if (bits) {
			k_rshift(r, a, rn, bits);
		} else {
			std::copy(a, a + rn, r);
		}
		if (&result == &lhs) {
			result.resize(rn); // shrink
		}

		// Finish up
		result.trim();
		return result;
	}

	// A shift count held in a uinteger_t is its low limb; a longer count exceeds any size a value can
	// have, so a left shift of a nonzero value by it cannot be represented and a right shift gives zero.
	static uinteger_t& bitwise_lshift(uinteger_t& lhs, const uinteger_t& rhs) {
		return bitwise_lshift(lhs, lhs, rhs);
	}

	static uinteger_t& bitwise_lshift(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		if (rhs.size() > 1) {
			if (lhs) {
				throw std::length_error("Error: Shift too large");
			}
			result.clear();
			return result;
		}
		return lshift(result, lhs, rhs.size() ? rhs.front() : 0);
	}

	static uinteger_t bitwise_lshift(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		bitwise_lshift(result, lhs, rhs);
		return result;
	}

	static uinteger_t& bitwise_rshift(uinteger_t& lhs, const uinteger_t& rhs) {
		return bitwise_rshift(lhs, lhs, rhs);
	}

	static uinteger_t& bitwise_rshift(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		if (rhs.size() > 1) {
			result.clear();
			return result;
		}
		return rshift(result, lhs, rhs.size() ? rhs.front() : 0);
	}

	static uinteger_t bitwise_rshift(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		bitwise_rshift(result, lhs, rhs);
		return result;
	}

	static int compare(const uinteger_t& lhs, const uinteger_t& rhs) {
		auto n = lhs.size();
		if (n != rhs.size()) {
			return n > rhs.size() ? 1 : -1;
		}
		return k_cmp_n(lhs.data(), rhs.data(), n);
	}

	static uinteger_t& long_add(uinteger_t& lhs, const uinteger_t& rhs) {
		return long_add(lhs, lhs, rhs);
	}

	// result = lhs + rhs; `result` may be `lhs` or `rhs`.
	static uinteger_t& long_add(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		auto an = lhs.size();
		auto bn = rhs.size();
		auto rn = std::max(an, bn);
		result.reserve(rn + 1);
		result.resize(rn);

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
		result.resize(std::max(an, bn));

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

	// Multiplication on raw limbs (schoolbook, Karatsuba, Toom-3, chunked for lopsided operands);
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

		uinteger_t tmp;
		tmp.resize(an + bn, 0);
		if (a == b && an == bn) {
			k_scratch scratch(k_sqr_scratch_size(an));
			k_sqr(tmp.data(), a, an, scratch.data());
		} else {
			k_scratch scratch(k_mul_scratch_size(an, bn));
			k_mul(tmp.data(), a, an, b, bn, scratch.data());
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
		const std::size_t sn = k_div_dc_scratch_size(nn + 1, dn);
		const std::size_t total = dn + (nn + 1) + qn + sn;
		digit local[64];
		std::vector<digit> heap;
		digit* buf = local;
		if (total > sizeof(local) / sizeof(digit)) {
			heap.resize(total);
			buf = heap.data();
		}
		digit* d = buf;
		digit* n = d + dn;
		digit* q = n + nn + 1;
		digit* s = q + qn;
		std::fill_n(s, sn, digit(0));

		// Normalize: shift the divisor so its top bit is set, and the dividend alike.
		const unsigned sh = static_cast<unsigned>(digit_bits - _bits(rhs.back()));
		if (sh) {
			k_lshift(d, rhs.data(), dn, sh);
			n[nn] = k_lshift(n, lhs.data(), nn, sh);
		} else {
			std::copy(rhs.data(), rhs.data() + dn, d);
			std::copy(lhs.data(), lhs.data() + nn, n);
			n[nn] = 0;
		}

		k_div_dc(q, n, nn + 1, d, dn, s);

		if (sh) {
			k_rshift(n, n, dn, sh);
		}
		if (quotient) {
			quotient->_value.assign(q, q + qn);
			quotient->trim();
		}
		if (remainder) {
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

	static std::pair<std::reference_wrapper<uinteger_t>, std::reference_wrapper<uinteger_t>> divmod(uinteger_t& quotient, uinteger_t& remainder, const uinteger_t& lhs, const uinteger_t& rhs) {
		divide(&quotient, &remainder, lhs, rhs);
		return std::make_pair(std::ref(quotient), std::ref(remainder));
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

	// Conversion kernels for bases that are not powers of two.
	//
	// Encoding writes the value as K chunks of k digits, one per power of D = base^k (the
	// largest power below the limb size B), and splits each chunk into digits. The chunks
	// come from a fixed-point fraction, not from divisions: x / D^K is held in L = K + g limbs
	// and each pass multiplies it by D, so the carry out of a pass is the next chunk, most
	// significant first (Bouvier and Zimmermann, "Division-Free Binary-to-Decimal Conversion",
	// IEEE Trans. Computers 63(8), 2014, Algorithm 2). Their initial division is a multiply
	// by R = floor(B^(L+n) / D^K), built once per base and size from K single-limb divisions
	// (Moller and Granlund, "Improved division by invariant integers", IEEE Trans. Computers
	// 60(2), 2011) and cached: the fraction starts at (x + 1/2) / D^K, rounded down by at most
	// n + 2 units of its last limb, and every pass drops one limb. While the accumulated error
	// stays below 1/2 each chunk is exact; it is bounded in _make_radix_table, and the guard
	// limbs g add the precision a base with D close to B needs.
	// Values up to radix_unrolled limbs run fully unrolled kernels with the fraction in
	// registers, tables chosen by bit length so K is as small as the value allows. Larger values
	// up to radix_max_bits run four fused passes and a four-row short product, one table per
	// limb count.
	//
	// Decoding builds each chunk of k digits with SWAR multiplies (eight digit bytes in three
	// multiplies) and folds the chunks in four per pass, Horner style.

	static constexpr bool radix_fast = digit_bits == 32 || digit_bits == 64;
	static constexpr std::size_t radix_max_bits = 8192;
	static constexpr std::size_t radix_max_limbs = radix_max_bits / digit_bits;
	// Scratch limbs of the general kernel: n + L + 8 with L at most 8192 / (digit_bits - 8) + 2.
	static constexpr std::size_t radix_work = radix_max_limbs + radix_max_bits / (digit_bits > 8 ? digit_bits - 8 : 1) + 24;
#ifndef UINTEGER_T_RADIX_UNROLL
#define UINTEGER_T_RADIX_UNROLL 6
#endif
	static constexpr std::size_t radix_unrolled = UINTEGER_T_RADIX_UNROLL;
	static_assert(radix_unrolled <= 8, "the unrolled digit kernels go up to 8 limbs");

	// R and floor(R / 2) for inputs below 2^bits, with the chunk and limb counts they imply.
	struct radix_table {
		unsigned chunks;           // K: D^K >= 2^bits
		unsigned guard;            // g
		unsigned fraction;         // L = K + g
		unsigned rlen;             // limbs of R: L, or L + 1 when D^K < B^n
		std::vector<digit> store;  // 4 zero limbs, R, 8 zero limbs, floor(R / 2)
		const digit* R;
		const digit* R2;
	};

	// floor(r / d) for r below 2^nd with a multiply and a shift (Granlund and Montgomery,
	// "Division by invariant integers using multiplication", PLDI 1994). `wide` is the
	// 65-bit multiplier form that covers any 64-bit r.
	struct radix_div {
		std::uint64_t m;
		unsigned s;
		bool wide;
		bool ok;
	};

	// The context of one base: chunk size, digit extraction constants and the tables.
	struct radix {
		int base;
		unsigned k;                // digits per chunk, D = base^k
		unsigned h;                // digits per piece: base^h < 2^32
		digit D;
		digit E[4];                // D^4, for the four-chunk Horner step
		std::uint64_t bh;          // base^h
		std::uint64_t cinv[digit_bits + 1];  // ceil(2^64 / base^m)
		radix_div div0, div1;      // chunk / bh, and the quotient / bh when k = 2h + 1
		const unsigned char* pair; // two digits per entry for base <= 64
		std::vector<unsigned char> pair_store;
		mutable std::atomic<const radix_table*> by_bits[radix_unrolled * digit_bits + 1];
		mutable std::atomic<const radix_table*> by_limbs[radix_max_limbs + 1];
	};

	static std::uint64_t _umulh(std::uint64_t x, std::uint64_t y) {
	#if defined HAVE____INT128_T
		return static_cast<std::uint64_t>((static_cast<__uint128_t>(x) * y) >> 64);
	#elif defined HAVE___UMUL128
		unsigned __int64 h;
		_umul128(static_cast<unsigned __int64>(x), static_cast<unsigned __int64>(y), &h);
		return static_cast<std::uint64_t>(h);
	#elif defined HAVE___UMULH
		return static_cast<std::uint64_t>(__umulh(static_cast<unsigned __int64>(x), static_cast<unsigned __int64>(y)));
	#else
		const std::uint64_t mask = 0xffffffffu;
		const std::uint64_t x0 = x & mask, x1 = x >> 32, y0 = y & mask, y1 = y >> 32;
		const std::uint64_t p00 = x0 * y0, p01 = x0 * y1, p10 = x1 * y0, p11 = x1 * y1;
		const std::uint64_t mid = (p00 >> 32) + (p01 & mask) + (p10 & mask);
		return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
	#endif
	}

	// floor(2^e / d) as (hi, lo) and the remainder, by shift and subtract (table building only).
	static std::uint64_t _pow2_div(unsigned e, std::uint64_t d, std::uint64_t& hi, std::uint64_t& lo) {
		hi = 0;
		lo = 0;
		std::uint64_t rem = 0;
		for (unsigned i = e + 1; i--;) {
			const bool top = (rem >> 63) != 0;
			rem = (rem << 1) | (i == e ? 1 : 0);
			hi = (hi << 1) | (lo >> 63);
			lo <<= 1;
			if (top || rem >= d) {
				rem -= d;
				lo |= 1;
			}
		}
		return rem;
	}

	static unsigned _bit_length(std::uint64_t v) {
		unsigned n = 0;
		for (; v; v >>= 1) {
			++n;
		}
		return n;
	}

	// The multiplier for dividends below 2^nd by d (not a power of two, d < 2^32).
	static radix_div _make_div(std::uint64_t d, unsigned nd) {
		radix_div v = {0, 0, false, false};
		const unsigned l = _bit_length(d);  // ceil(log2 d)
		std::uint64_t hi, lo;
		if (nd + l < 64 && nd < 64) {
			return v;
		}
		const unsigned e = (nd < 64 ? nd : 64) + l;
		if (_pow2_div(e, d, hi, lo)) {  // round up
			if (++lo == 0) {
				++hi;
			}
		}
		if (nd < 64) {
			if (!hi) {
				v.m = lo;
				v.s = e - 64;
				v.ok = true;
			}
		} else if (hi == 1) {
			v.m = lo;  // the multiplier is 2^64 + lo
			v.s = l - 1;
			v.wide = true;
			v.ok = true;
		}
		return v;
	}

	static std::uint64_t _div_magic(const radix_div& v, std::uint64_t r) {
		const std::uint64_t t = _umulh(v.m, r);
		return v.wide ? (t + ((r - t) >> 1)) >> v.s : t >> v.s;
	}

	static radix* _make_radix(int base) {
		std::unique_ptr<radix> rx(new radix());
		rx->base = base;
		rx->D = base_chunk(base, rx->k);
		const std::uint64_t b = static_cast<std::uint64_t>(base);
		std::uint64_t bh = 1;
		unsigned h = 0;
		while (bh * b < (static_cast<std::uint64_t>(1) << 32)) {
			bh *= b;
			++h;
		}
		rx->h = h;
		rx->bh = bh;
		std::uint64_t power = 1;
		for (unsigned m = 1; m <= digit_bits && m <= h && m <= rx->k; ++m) {
			power *= b;
			std::uint64_t hi, lo;
			if (_pow2_div(64, power, hi, lo)) {
				++lo;
			}
			rx->cinv[m] = lo;
		}
		const std::uint64_t dmax = static_cast<std::uint64_t>(rx->D) - 1;
		rx->div0 = _make_div(bh, _bit_length(dmax));
		rx->div1 = _make_div(bh, _bit_length(dmax / bh));
		if (base <= 64) {
			rx->pair_store.resize(static_cast<std::size_t>(2 * base * base));
			for (int i = 0; i < base * base; ++i) {
				rx->pair_store[2 * i] = static_cast<unsigned char>(i / base);
				rx->pair_store[2 * i + 1] = static_cast<unsigned char>(i % base);
			}
			rx->pair = rx->pair_store.data();
		} else {
			rx->pair = nullptr;
		}
		rx->E[0] = rx->D;
		rx->E[1] = rx->E[2] = rx->E[3] = 0;
		for (int r = 0; r < 3; ++r) {
			digit carry = 0;
			for (int i = 0; i < 4; ++i) {
				carry = _multadd(rx->E[i], rx->D, carry, 0, &rx->E[i]);
			}
		}
		for (auto& slot : rx->by_bits) {
			slot.store(nullptr, std::memory_order_relaxed);
		}
		for (auto& slot : rx->by_limbs) {
			slot.store(nullptr, std::memory_order_relaxed);
		}
		return rx.release();
	}

	// R for values below 2^bits (n limbs), built from K single-limb divisions: floor(floor(a / D) / D)
	// is floor(a / D^2). `guard` and `half` override the computed guard limbs and drop the 1/2
	// offset; tests use them to show the edge inputs catch an under-sized table.
	static radix_table* _make_radix_table(const radix& rx, std::size_t n, std::size_t bits, int guard = -1, bool half = true) {
		unsigned K = 0;
		{
			uinteger_t power(1);
			while (power.bits() <= bits) {
				power.mul_add_small(rx.D, 0);
				++K;
			}
		}
		// Error budget in units of x: (n + 2) r^K for the initial product and R, plus r^m for the
		// limb dropped by each of the K - 1 passes, with r = D / B; g guard limbs divide it by B^g.
		double B = 1;
		for (unsigned i = 0; i < digit_bits; ++i) {
			B *= 2;
		}
		const double r = static_cast<double>(rx.D) / B;
		double power_r = 1, bound = 0;
		for (unsigned m = 1; m < K; ++m) {
			power_r *= r;
			bound += power_r;
		}
		bound += static_cast<double>(n + 2) * power_r * r;
		const unsigned g = guard >= 0 ? static_cast<unsigned>(guard) : (bound < 0.45 ? 0 : 1);
		assert(guard >= 0 || bound / (g ? B : 1) < 0.45);
		const std::size_t L = K + g;
		uinteger_t a;
		a.resize(L + n + 1, 0);
		a.data()[L + n] = 1;
		const small_divisor dv(rx.D);
		for (unsigned i = 0; i < K; ++i) {
			a.divmod_small(dv);
		}
		const std::size_t an = a.size();
		assert(an <= L + 1);
		const std::size_t rl = an > L ? an : L;
		std::unique_ptr<radix_table> t(new radix_table());
		t->chunks = K;
		t->guard = g;
		t->fraction = static_cast<unsigned>(L);
		t->rlen = static_cast<unsigned>(rl);
		t->store.assign(4 + rl + 8 + rl, 0);
		digit* R = t->store.data() + 4;
		std::copy(a.data(), a.data() + an, R);
		digit* R2 = R + rl + 8;
		if (half) {
			for (std::size_t l = 0; l < rl; ++l) {
				R2[l] = (R[l] >> 1) | (l + 1 < rl ? static_cast<digit>(R[l + 1] << (digit_bits - 1)) : 0);
			}
		}
		t->R = R;
		t->R2 = R2;
		return t.release();
	}

	// Tables and contexts are built on first use and published with a compare-and-swap, so
	// concurrent first calls are safe; the loser deletes its copy. They live until exit.
	template <typename T>
	static const T* _publish(std::atomic<const T*>& slot, T* fresh) {
		std::unique_ptr<T> mine(fresh);
		const T* expected = nullptr;
		if (slot.compare_exchange_strong(expected, fresh, std::memory_order_acq_rel, std::memory_order_acquire)) {
			mine.release();
			return fresh;
		}
		return expected;
	}

	static const radix& _radix_for(int base) {
		static std::atomic<const radix*> contexts[257];
		std::atomic<const radix*>& slot = contexts[base];
		const radix* rx = slot.load(std::memory_order_acquire);
		if (!rx) {
			rx = _publish(slot, _make_radix(base));
		}
		return *rx;
	}

	// The table for a value of n limbs and `bits` bits.
	static const radix_table& _table_for(const radix& rx, std::size_t n, std::size_t bits) {
		const bool by_bits = n <= radix_unrolled;
		std::atomic<const radix_table*>& slot = by_bits ? rx.by_bits[bits] : rx.by_limbs[n];
		const radix_table* t = slot.load(std::memory_order_acquire);
		if (!t) {
			t = _publish(slot, _make_radix_table(rx, n, by_bits ? bits : n * digit_bits));
		}
		return *t;
	}

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
	static void _split_chunk(const radix& rx, digit r, unsigned char* out) {
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

	// Appends the digits of the value to `out`: one-byte results are written in place.
	template <typename Result, typename Fill>
	static void _digits_append(Result& out, std::size_t cap, Fill fill, std::true_type) {
		const auto start = out.size();
		out.resize(start + cap);
		const std::size_t count = fill(reinterpret_cast<unsigned char*>(&out[start]));
		out.resize(start + count);
	}

	template <typename Result, typename Fill>
	static void _digits_append(Result& out, std::size_t cap, Fill fill, std::false_type) {
		using value_type = typename Result::value_type;
		std::vector<unsigned char> buffer(cap);
		const std::size_t count = fill(buffer.data());
		for (std::size_t i = 0; i < count; ++i) {
			out.push_back(static_cast<value_type>(buffer[i]));
		}
	}

	template <typename Result>
	static void _digits_radix(Result& out, int base, const digit* p, std::size_t n) {
		using value_type = typename Result::value_type;
		using in_place = std::integral_constant<bool, std::is_integral<value_type>::value && sizeof(value_type) == 1 && !std::is_same<value_type, bool>::value>;
		const radix& rx = _radix_for(base);
		const std::size_t bits = (n - 1) * digit_bits + static_cast<std::size_t>(_bits(p[n - 1]));
		const radix_table& t = _table_for(rx, n, bits);
		const std::size_t cap = static_cast<std::size_t>(t.chunks) * rx.k;
		if (cap > out.max_size() - out.size()) {
			throw std::length_error("Error: Value too large to convert");
		}
		_digits_append(out, cap, [&](unsigned char* buffer) { return _encode_radix(rx, t, p, n, buffer); }, in_place());
	}

	// Reads eight digit bytes (first byte most significant) as one number below base^8 with
	// three multiplies; the digits are below `base`, so no lane overflows.
	static std::uint64_t _swar8(const unsigned char* p, std::uint64_t b, std::uint64_t b2, std::uint64_t b4) {
		std::uint64_t v;
		std::memcpy(&v, p, 8);
	#if defined __BYTE_ORDER__ && defined __ORDER_BIG_ENDIAN__ && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		v = __builtin_bswap64(v);
	#endif
		v = (v & 0x00ff00ff00ff00ffull) * b + ((v >> 8) & 0x00ff00ff00ff00ffull);
		v = (v & 0x0000ffff0000ffffull) * b2 + ((v >> 16) & 0x0000ffff0000ffffull);
		return (v & 0x00000000ffffffffull) * b4 + (v >> 32);
	}

	// One chunk of `count` digits as a number below base^count.
	static std::uint64_t _chunk_value(const radix& rx, const unsigned char* p, unsigned count, std::uint64_t b8) {
		const std::uint64_t b = static_cast<std::uint64_t>(rx.base);
		std::uint64_t acc = 0;
		unsigned i = 0;
		if (count >= 8) {
			const std::uint64_t b2 = b * b;
			acc = _swar8(p, b, b2, b2 * b2);
			for (i = 8; i + 8 <= count; i += 8) {
				acc = acc * b8 + _swar8(p + i, b, b2, b2 * b2);
			}
		}
		for (; i < count; ++i) {
			acc = acc * b + p[i];
		}
		return acc;
	}

	// The value of m >= 1 digits d[0..m), each below `base`, most significant first. The
	// chunks go into the result's own limbs: Horner step j reads chunk j and leaves at most j
	// limbs below it, so nothing unread is overwritten.
	static uinteger_t _from_digit_bytes(const unsigned char* d, std::size_t m, int base) {
		const radix& rx = _radix_for(base);
		const unsigned k = rx.k;
		std::size_t first = m % k;
		if (!first) {
			first = k;
		}
		const std::size_t chunks = (m - first) / k + 1;
		uinteger_t result;
		result._value.resize(chunks + 4);
		digit* x = result._value.data();
		const std::uint64_t b = static_cast<std::uint64_t>(base);
		std::uint64_t b8 = 1;
		for (int i = 0; i < 8; ++i) {
			b8 *= b;
		}
		x[0] = static_cast<digit>(_chunk_value(rx, d, static_cast<unsigned>(first), b8));
		const unsigned char* p = d + first;
		for (std::size_t j = 1; j < chunks; ++j, p += k) {
			x[j] = static_cast<digit>(_chunk_value(rx, p, k, b8));
		}
		const digit D = rx.D;
		std::size_t n = 0, j = 0;
		for (; (chunks - j) % 4; ++j) {
			digit carry = x[j];
			for (std::size_t i = 0; i < n; ++i) {
				carry = _multadd(x[i], D, carry, 0, &x[i]);
			}
			if (carry) {
				x[n++] = carry;
			}
		}
		for (; j < chunks; j += 4) {
			// a = ((c0 D + c1) D + c2) D + c3 as four limbs, then x = x D^4 + a.
			digit a0 = x[j], a1, a2, a3, carry;
			a1 = _multadd(a0, D, x[j + 1], 0, &a0);
			carry = _multadd(a0, D, x[j + 2], 0, &a0);
			a2 = _multadd(a1, D, carry, 0, &a1);
			carry = _multadd(a0, D, x[j + 3], 0, &a0);
			carry = _multadd(a1, D, carry, 0, &a1);
			a3 = _multadd(a2, D, carry, 0, &a2);
			for (std::size_t i = 0; i < n; ++i) {
				const digit xi = x[i];
				digit l0, l1, l2, l3;
				digit h = _multadd(xi, rx.E[0], a0, 0, &l0);
				h = _multadd(xi, rx.E[1], a1, h, &l1);
				h = _multadd(xi, rx.E[2], a2, h, &l2);
				h = _multadd(xi, rx.E[3], a3, h, &l3);
				x[i] = l0;
				a0 = l1;
				a1 = l2;
				a2 = l3;
				a3 = h;
			}
			x[n] = a0;
			x[n + 1] = a1;
			x[n + 2] = a2;
			x[n + 3] = a3;
			n += 4;
			while (n && !x[n - 1]) {
				--n;
			}
		}
		result._value.resize(n);
		return result;
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
		} else if (radix_fast) {
			// Gather the digit bytes (get() validates each one), then convert them at once.
			unsigned char stack_digits[256];
			std::unique_ptr<unsigned char[]> heap_digits;
			unsigned char* digits = stack_digits;
			if (n > sizeof(stack_digits)) {
				heap_digits.reset(new unsigned char[n]);
				digits = heap_digits.get();
			}
			for (std::size_t i = 0; i < n; ++i) {
				digits[i] = static_cast<unsigned char>(get(i));
			}
			return _from_digit_bytes(digits, n, base);
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

public:
	uinteger_t() = default;

	// The range form measures about 1 ns faster than the defaulted copy constructor.
	uinteger_t(const uinteger_t& o) : _value(o._value.begin(), o._value.end()) { }

	uinteger_t(uinteger_t&& o) noexcept = default;

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t(const T& value) {
		set_digit(static_cast<digit>(value));
	}

	template <typename T, typename... Args, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t(const T& value, Args... args) {
		_uint_t(args...);
		append(static_cast<digit>(value));
		trim();
	}

	template <typename T, typename... Args, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t(std::initializer_list<T> list) {
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
	uinteger_t& operator=(const uinteger_t& o) = default;
	uinteger_t& operator=(uinteger_t&& o) noexcept = default;

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

	// Shifts by a native integer read the count directly (negative counts wrap to huge ones, as
	// converting the count to a uinteger_t does).
	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t operator<<(T rhs) const {
		uinteger_t result;
		lshift(result, *this, static_cast<digit>(rhs));
		return result;
	}

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t& operator<<=(T rhs) {
		return lshift(*this, *this, static_cast<digit>(rhs));
	}

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t operator>>(T rhs) const {
		uinteger_t result;
		rshift(result, *this, static_cast<digit>(rhs));
		return result;
	}

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t& operator>>=(T rhs) {
		return rshift(*this, *this, static_cast<digit>(rhs));
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
		uinteger_t quotient;
		divide(&quotient, nullptr, *this, rhs);
		return quotient;
	}

	uinteger_t& operator/=(const uinteger_t& rhs) {
		divide(this, nullptr, *this, rhs);
		return *this;
	}

	uinteger_t operator%(const uinteger_t& rhs) const {
		uinteger_t remainder;
		divide(nullptr, &remainder, *this, rhs);
		return remainder;
	}

	uinteger_t& operator%=(const uinteger_t& rhs) {
		divide(nullptr, this, *this, rhs);
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
		} else if (radix_fast && n <= radix_max_limbs) {
			_digits_radix(out, base, p, n);
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
		if (radix_fast && n && base >= 2 && base <= 256 && !base_bits(base)) {
			// Check every digit first so the kernel can trust them.
			const unsigned char* bytes = reinterpret_cast<const unsigned char*>(src);
			unsigned char largest = 0;
			for (std::size_t i = 0; i < n; ++i) {
				largest = bytes[i] > largest ? bytes[i] : largest;
			}
			if (largest >= base) {
				std::size_t i = 0;
				while (bytes[i] < base) {
					++i;
				}
				throw std::invalid_argument("Error: Not a digit in base " + std::to_string(base) + ": " + std::to_string(bytes[i]) + " at " + std::to_string(n - i));
			}
			return _from_digit_bytes(bytes, n, base);
		}
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

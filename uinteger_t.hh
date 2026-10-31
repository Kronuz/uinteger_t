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
#include <cmath>
#include <atomic>
#include <limits>
#include <memory>
#include <vector>
#include <iterator>
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

// Keeps the allocating paths of the limb container out of line, so its fast paths inline.
#if defined __GNUC__ || defined __clang__
#define UINTEGER_T_NOINLINE __attribute__((noinline))
#elif defined _MSC_VER
#define UINTEGER_T_NOINLINE __declspec(noinline)
#else
#define UINTEGER_T_NOINLINE
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

// A value of up to this many limbs lives inside the object, with no heap block; the default holds
// the same number of bytes at every limb width.
#ifndef UINTEGER_T_INLINE_LIMBS
#define UINTEGER_T_INLINE_LIMBS   (64 / sizeof(DIGIT_T))
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

// A test defines UINTEGER_T_PUBLIC_IMPLEMENTATION (UINT_T_PUBLIC_IMPLEMENTATION is the older
// spelling) to reach the private members of the class. UINTEGER_T_PRIVATE stands for `private`
// in the class below and is undefined after it.
#if defined UINTEGER_T_PUBLIC_IMPLEMENTATION || defined UINT_T_PUBLIC_IMPLEMENTATION
#  define UINTEGER_T_PRIVATE public
#else
#  define UINTEGER_T_PRIVATE private
#endif

class uinteger_t {
public:
	using digit = DIGIT_T;
	using half_digit = HALF_DIGIT_T;

	static constexpr std::size_t digit_octets = sizeof(digit);             // number of octets per digit
	static constexpr std::size_t digit_bits = digit_octets * 8;            // number of bits per digit
	static constexpr std::size_t half_digit_octets = sizeof(half_digit);   // number of octets per half_digit
	static constexpr std::size_t half_digit_bits = half_digit_octets * 8;  // number of bits per half_digit

	// The limbs of a value: a vector of `digit` that keeps up to `inline_capacity` limbs inside the
	// object and uses a heap block beyond that. `data()` is a pointer member, to the buffer or to
	// the block (the block is in use exactly when `capacity() > inline_capacity`), so reaching the
	// limbs reads nothing the limbs' own stores could delay. Growing zero-fills the new limbs;
	// shrinking keeps the storage. Growing past the capacity moves the limbs, which invalidates
	// pointers into them. A move of an inline value copies its limbs, of a heap value takes the
	// block; the source is left empty. The paths that allocate are out of line, so the rest
	// inlines into the arithmetic.
	class container {
	public:
		using value_type = digit;
		using size_type = std::size_t;
		using difference_type = std::ptrdiff_t;
		using reference = digit&;
		using const_reference = const digit&;
		using pointer = digit*;
		using const_pointer = const digit*;
		using iterator = digit*;
		using const_iterator = const digit*;
		using reverse_iterator = std::reverse_iterator<iterator>;
		using const_reverse_iterator = std::reverse_iterator<const_iterator>;

		// Read it by value (`constexpr size_type n = container::inline_capacity;`): the header
		// gives it no out-of-class definition, so a reference to it would not link before C++17.
		static constexpr size_type inline_capacity = UINTEGER_T_INLINE_LIMBS;
		static_assert(inline_capacity >= 1, "UINTEGER_T_INLINE_LIMBS must be at least 1");

	private:
		digit* _p;       // _local, or the heap block
		size_type _size;
		size_type _cap;  // inline_capacity while the limbs are inline
		digit _local[inline_capacity];

		// Copies `n` limbs; the ranges may overlap. g++ cannot see that a value's size bounds the
		// buffer it copies from and to, so it reports out-of-bounds and uninitialized reads that
		// the invariants rule out.
		static void copy_limbs(digit* to, const digit* from, size_type n) {
#if defined __GNUC__ && !defined __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
			if (n) {
				std::memmove(to, from, n * sizeof(digit));
			}
#if defined __GNUC__ && !defined __clang__
#pragma GCC diagnostic pop
#endif
		}

		// Frees the heap block, if there is one. The caller sets `_p` and `_cap` next.
		void release() noexcept {
			if (_p != _local) {
				delete[] _p;
			}
		}

		// Moves the limbs to a new heap block of exactly `n` limbs (`n > _cap`). Throws before
		// changing anything.
		UINTEGER_T_NOINLINE void reallocate(size_type n) {
			if (n > max_size()) {
				throw std::length_error("Error: container too large");
			}
			digit* p = new digit[n];
			copy_limbs(p, _p, _size);
			release();
			_p = p;
			_cap = n;
		}

		// Room for `n` limbs (`n > _cap`): exact from the inline buffer, at least doubled from a
		// heap block, so repeated growth is linear overall.
		UINTEGER_T_NOINLINE void grow(size_type n) {
			reallocate(_cap > inline_capacity && n <= max_size() ? std::min(std::max(n, 2 * _cap), max_size()) : n);
		}

		// A heap block of exactly `n` limbs for a value about to be overwritten (`n > _cap`).
		UINTEGER_T_NOINLINE void replace(size_type n) {
			if (n > max_size()) {
				throw std::length_error("Error: container too large");
			}
			digit* p = new digit[n];
			release();
			_p = p;
			_cap = n;
		}

		// Copy construction of a value too long to be inline (`_cap` is still `inline_capacity`).
		UINTEGER_T_NOINLINE void copy_to_heap(const container& o) {
			_p = new digit[o._size];
			_cap = o._size;
			copy_limbs(_p, o._p, o._size);
		}

	public:
		container() noexcept : _p(_local), _size(0), _cap(inline_capacity) { }

		container(const container& o) : _p(_local), _size(o._size), _cap(inline_capacity) {
			if (_size > inline_capacity) {
				copy_to_heap(o);
			} else {
				copy_limbs(_local, o._p, _size);
			}
		}

		container(container&& o) noexcept : _p(_local), _size(o._size), _cap(inline_capacity) {
			if (o._cap > inline_capacity) {
				_p = o._p;
				_cap = o._cap;
				o._p = o._local;
				o._cap = inline_capacity;
			} else {
				copy_limbs(_local, o._local, _size);
			}
			o._size = 0;
		}

		~container() {
			release();
		}

		// Copy assignment reuses the storage when the limbs fit; otherwise it allocates before
		// it releases, so a failure leaves `*this` unchanged.
		container& operator=(const container& o) {
			if (this != &o) {
				assign(o._p, o._p + o._size);
			}
			return *this;
		}

		container& operator=(container&& o) noexcept {
			if (this != &o) {
				if (o._cap > inline_capacity) {
					release();
					_p = o._p;
					_cap = o._cap;
					o._p = o._local;
					o._cap = inline_capacity;
				} else {
					copy_limbs(_p, o._local, o._size);
				}
				_size = o._size;
				o._size = 0;
			}
			return *this;
		}

		// Replaces the limbs with a copy of [first, last), which must not lie inside this container.
		void assign(const digit* first, const digit* last) {
			const size_type n = static_cast<size_type>(last - first);
			if (n > _cap) {
				replace(n);
			}
			copy_limbs(_p, first, n);
			_size = n;
		}

		// Replaces the limbs with `n` copies of `v`.
		void assign(size_type n, digit v) {
			if (n > _cap) {
				replace(n);
			}
			std::fill_n(_p, n, v);
			_size = n;
		}

		size_type size() const noexcept {
			return _size;
		}

		size_type capacity() const noexcept {
			return _cap;
		}

		bool empty() const noexcept {
			return !_size;
		}

		static constexpr size_type max_size() noexcept {
			return static_cast<size_type>(std::numeric_limits<difference_type>::max()) / sizeof(digit);
		}

		digit* data() noexcept {
			return _p;
		}

		const digit* data() const noexcept {
			return _p;
		}

		iterator begin() noexcept { return data(); }
		const_iterator begin() const noexcept { return data(); }
		const_iterator cbegin() const noexcept { return data(); }
		iterator end() noexcept { return data() + _size; }
		const_iterator end() const noexcept { return data() + _size; }
		const_iterator cend() const noexcept { return data() + _size; }
		reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
		const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
		const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }
		reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
		const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
		const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

		reference operator[](size_type i) noexcept { return data()[i]; }
		const_reference operator[](size_type i) const noexcept { return data()[i]; }
		reference front() noexcept { return data()[0]; }
		const_reference front() const noexcept { return data()[0]; }
		reference back() noexcept { return data()[_size - 1]; }
		const_reference back() const noexcept { return data()[_size - 1]; }

		void reserve(size_type n) {
			if (n > _cap) {
				reallocate(n);
			}
		}

		void clear() noexcept {
			_size = 0;
		}

		void resize(size_type n) {
			resize(n, 0);
		}

		void resize(size_type n, digit c) {
			if (n > _size) {
				if (n > _cap) {
					grow(n);
				}
				std::fill(data() + _size, data() + n, c);
			}
			_size = n;
		}

		// Resizes without initializing new limbs, for a caller that writes them before it reads
		// them. UINTEGER_T_POISON_LIMBS fills them with a pattern, so a test can catch a limb left unwritten.
		void resize_uninit(size_type n) {
			if (n > _cap) {
				grow(n);
			}
#ifdef UINTEGER_T_POISON_LIMBS
			if (n > _size) {
				std::fill(data() + _size, data() + n, static_cast<digit>(0xa5a5a5a5a5a5a5a5ull));
			}
#endif
			_size = n;
		}

		void push_back(digit v) {
			if (_size == _cap) {
				grow(_size + 1);
			}
			data()[_size++] = v;
		}

		// Inserts `n` copies of `c` before `pos`; returns the position of the first.
		UINTEGER_T_NOINLINE iterator insert(const_iterator pos, size_type n, digit c) {
			const size_type at = static_cast<size_type>(pos - begin());
			if (n > max_size() - _size) {
				throw std::length_error("Error: container too large");
			}
			const size_type total = _size + n;
			if (total > _cap) {
				const size_type cap = _cap > inline_capacity ? std::min(std::max(total, 2 * _cap), max_size()) : total;
				digit* p = new digit[cap];
				copy_limbs(p, _p, at);
				std::fill_n(p + at, n, c);
				copy_limbs(p + at + n, _p + at, _size - at);
				release();
				_p = p;
				_cap = cap;
			} else if (n) {
				digit* d = _p;
				std::copy_backward(d + at, d + _size, d + total);
				std::fill_n(d + at, n, c);
			}
			_size = total;
			return begin() + at;
		}
	};

	// Whether `T` can receive digits from `str()`, `digits()` and `raw()`: a `std::basic_string` or a
	// `std::vector`. base-x uses it to constrain its own result types the same way.
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

UINTEGER_T_PRIVATE:
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
		if (n > cc) {
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

UINTEGER_T_PRIVATE:
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
#ifdef UINTEGER_T_MUL_TOOM4_THRESHOLD
	static constexpr std::size_t mul_toom4_threshold = UINTEGER_T_MUL_TOOM4_THRESHOLD;
#else
	static constexpr std::size_t mul_toom4_threshold = 400;
#endif
	static_assert(mul_karatsuba_threshold >= 2 && sqr_karatsuba_threshold >= 2, "a Karatsuba threshold below 2 limbs would recurse forever");

	// The transform (three-prime number-theoretic) multiplication takes over from Toom-4 at these
	// limb counts of the smaller operand; see the transform section below.
#ifdef UINTEGER_T_MUL_NTT_THRESHOLD
	static constexpr std::size_t mul_ntt_threshold = UINTEGER_T_MUL_NTT_THRESHOLD;
#else
	static constexpr std::size_t mul_ntt_threshold = 1400;
#endif
#ifdef UINTEGER_T_SQR_NTT_THRESHOLD
	static constexpr std::size_t sqr_ntt_threshold = UINTEGER_T_SQR_NTT_THRESHOLD;
#else
	static constexpr std::size_t sqr_ntt_threshold = 1400;
#endif

	// Toom-3 splits the larger operand into thirds of k limbs, and needs a smaller operand with
	// a third piece (bn > 2k) and enough limbs to pay for it.
	static bool k_mul_uses_toom3(std::size_t an, std::size_t bn) {
		return bn >= mul_toom3_threshold && bn > 2 * ((an + 2) / 3);
	}

	static bool k_sqr_uses_toom3(std::size_t n) {
		return n >= sqr_toom3_threshold && n > 2 * ((n + 2) / 3);
	}

	// Toom-4 splits the larger operand into quarters of k limbs and needs a smaller operand with a
	// fourth piece (bn > 3k). Its scratch, with that of the products below it, is under 16 limbs per
	// limb of the larger operand (the property test checks the bound); capping that operand at
	// SIZE_MAX / (32 sizeof(digit)) limbs keeps the scratch in bytes, and every sum of scratch sizes,
	// below the range of std::size_t. A larger product takes Toom-3.
	static constexpr std::size_t toom4_max_limbs = std::numeric_limits<std::size_t>::max() / (32 * sizeof(digit));

	static bool k_mul_uses_toom4(std::size_t an, std::size_t bn) {
		return an <= toom4_max_limbs && bn >= mul_toom4_threshold && bn > 3 * ((an + 3) / 4);
	}

	// The transform takes a product of at least twice the threshold's limbs in all, whose smaller operand has
	// two thirds of the threshold and whose larger one has under ntt_lopsided times as many: one transform
	// over the whole product costs less than chunking the longer operand, and its scratch stays a few times
	// the product (a longer operand is chunked into products the transform takes). A balanced product of
	// the threshold's limbs is the edge. It needs the transform to be sizable; see _ntt_plan.
	static constexpr std::size_t ntt_lopsided = 16;

	static bool k_mul_uses_ntt(std::size_t an, std::size_t bn) {
		std::size_t len = 0, scratch = 0;
		return bn >= mul_ntt_threshold - mul_ntt_threshold / 3 && an / ntt_lopsided < bn && an + bn >= 2 * mul_ntt_threshold &&
			_ntt_plan<std::size_t>(an, bn, false, &len, &scratch);
	}

	static bool k_sqr_uses_ntt(std::size_t n) {
		std::size_t len = 0, scratch = 0;
		return n >= sqr_ntt_threshold && _ntt_plan<std::size_t>(n, n, true, &len, &scratch);
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
		if (k_mul_uses_ntt(an, bn)) {
			return k_ntt_scratch_size(an, bn, false);
		}
		if (an >= 2 * bn) {
			std::size_t need = k_mul_scratch_size(bn, bn);
			if (an % bn) {
				need = std::max(need, k_mul_scratch_size(bn, an % bn));
			}
			return 2 * bn + need;
		}
		if (k_mul_uses_toom4(an, bn)) {
			const std::size_t k = (an + 3) / 4;
			const std::size_t m = k + 1;
			std::size_t need = std::max(k_mul_scratch_size(k, k), k_mul_scratch_size(an - 3 * k, bn - 3 * k));
			return std::max(need, 15 * m + k_mul_scratch_size(m, m));
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
		if (k_sqr_uses_ntt(n)) {
			return k_ntt_scratch_size(n, n, true);
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

	// With x = x0 + x1 B^k + x2 B^2k + x3 B^3k (x0, x1 and x2 of k limbs, x3 of x3n <= k limbs, x3n >= 1),
	// the Toom-4 evaluations at 1 and -1 as m = k + 1 limbs each: p1 = x(1), pm1 = |x(-1)|. `w` is m limbs
	// of temporary space. Returns 1 when x(-1) is negative.
	static unsigned k_toom4_eval1(digit* p1, digit* pm1, digit* w, const digit* x, std::size_t k, std::size_t x3n) {
		const std::size_t m = k + 1;
		const digit* x1 = x + k;
		const digit* x2 = x + 2 * k;
		const digit* x3 = x + 3 * k;
		// The even part pm1 = x0 + x2 and the odd part w = x1 + x3 give x(1) = pm1 + w and x(-1) = pm1 - w.
		pm1[k] = k_add_n(pm1, x, x2, k);
		digit c = k_add_n(w, x1, x3, x3n);
		if (x3n < k) {
			c = k_add_1(w + x3n, x1 + x3n, k - x3n, c);
		}
		w[k] = c;
		k_add_n(p1, pm1, w, m);
		return static_cast<unsigned>(k_absdiff(pm1, pm1, m, w, m));
	}

	// The evaluations at 2 and -2 in the same form: x(+-2) = (x0 + 4 x2) +- (2 x1 + 8 x3). Each part is
	// below 10 B^k, so it fits m limbs.
	static unsigned k_toom4_eval2(digit* p2, digit* pm2, digit* w, const digit* x, std::size_t k, std::size_t x3n) {
		const std::size_t m = k + 1;
		pm2[k] = k_addlsh_n(pm2, x, x + 2 * k, k, 2);
		w[x3n] = k_lshift(w, x + 3 * k, x3n, 3);
		std::fill(w + x3n + 1, w + m, digit(0));
		w[k] = static_cast<digit>(w[k] + k_addlsh_n(w, w, x + k, k, 1));
		k_add_n(p2, pm2, w, m);
		return static_cast<unsigned>(k_absdiff(pm2, pm2, m, w, m));
	}

	// The evaluation at 1/2 scaled to an integer, 8 x(1/2) = 8 x0 + 4 x1 + 2 x2 + x3 (below 15 B^k), as m limbs.
	static void k_toom4_evalh(digit* ph, const digit* x, std::size_t k, std::size_t x3n) {
		const std::size_t m = k + 1;
		ph[k] = k_lshift(ph, x, k, 3);
		ph[k] = static_cast<digit>(ph[k] + k_addlsh_n(ph, ph, x + k, k, 2));
		ph[k] = static_cast<digit>(ph[k] + k_addlsh_n(ph, ph, x + 2 * k, k, 1));
		k_add_at(ph, m, 0, x + 3 * k, x3n);
	}

	// Toom-4 interpolation for a product c0 + c1 B^k + ... + c6 B^6k, from the values at 0 and infinity
	// (c0 and c6) and at 1, -1, 2, -2 and 1/2. The values at p and -p give the even and odd parts of the
	// product at p, as in Toom-Cook with symmetric points (Knuth, TAOCP vol. 2, 4.3.3; Bodrato and
	// Zanoni, ISSAC 2007; Bodrato, WAIFI 2007). On entry r[0..2k) holds c0 and r[6k..6k + vn) holds c6,
	// with r[2k..6k) zero; v1, vm1, v2, vm2 and vh are the N = 2k + 2 limb products at 1, -1, 2, -2 and
	// 1/2 (the last scaled by 2^6), vm1 and vm2 meant negative when m1neg and m2neg are set; x is N limbs
	// of temporary space. All five are consumed: c1 to c5 are added into r (of rn limbs) at k to 5k.
	// The steps run on values mod B^N, so the one that can be negative (c1 - c5) needs no sign branch;
	// the divisions are exact, and the halvings and shifts only act on non-negative values.
	static void k_toom4_interp(digit* r, std::size_t rn, std::size_t k, std::size_t vn, digit* v1, digit* vm1, digit* v2, digit* vm2, digit* vh, digit* x, bool m1neg, bool m2neg) {
		const std::size_t n = 2 * k + 2;
		const digit* c0 = r;
		const digit* c6 = r + 6 * k;
		// t -= mul * p[0..pn) mod B^n.
		auto submul = [&](digit* t, const digit* p, std::size_t pn, digit mul) {
			digit b = k_submul_1(t, p, pn, mul);
			if (pn < n) {
				b = _subborrow(t[pn], b, 0, &t[pn]);
				k_sub_1(t + pn + 1, t + pn + 1, n - pn - 1, b);
			}
		};
		if (m1neg) {
			k_neg_n(vm1, vm1, n);
		}
		if (m2neg) {
			k_neg_n(vm2, vm2, n);
		}
		k_sub_n(x, v1, vm1, n);                 // v(1) - v(-1) = 2 (c1 + c3 + c5)
		k_add_n(v1, v1, vm1, n);                // v(1) + v(-1) = 2 (c0 + c2 + c4 + c6)
		k_sub_n(vm1, v2, vm2, n);               // v(2) - v(-2) = 4 (c1 + 4 c3 + 16 c5)
		k_add_n(v2, v2, vm2, n);                // v(2) + v(-2) = 2 (c0 + 4 c2 + 16 c4 + 64 c6)
		k_rshift(v1, v1, n, 1);
		k_sub_wrap(v1, n, c0, 2 * k);
		k_sub_wrap(v1, n, c6, vn);              // p = c2 + c4
		k_rshift(x, x, n, 1);                   // q = c1 + c3 + c5
		k_rshift(v2, v2, n, 1);
		k_sub_wrap(v2, n, c0, 2 * k);
		submul(v2, c6, vn, 64);
		k_rshift(v2, v2, n, 2);                 // t2 = c2 + 4 c4
		k_rshift(vm1, vm1, n, 2);               // t1 = c1 + 4 c3 + 16 c5
		k_sub_n(v2, v2, v1, n);                 // t2 - p = 3 c4
		k_divexact_3(v2, v2, n);                // c4
		k_sub_n(v1, v1, v2, n);                 // c2 = p - c4
		submul(vh, c0, 2 * k, 64);              // v(1/2) - 64 c0 - 16 c2 - 4 c4 - c6 = 2 (16 c1 + 4 c3 + c5)
		submul(vh, v1, n, 16);
		submul(vh, v2, n, 4);
		k_sub_wrap(vh, n, c6, vn);
		k_rshift(vh, vh, n, 1);                 // h = 16 c1 + 4 c3 + c5
		k_add_n(vm2, vh, vm1, n);               // h + t1 = 17 (c1 + c5) + 8 c3
		submul(vm2, x, n, 17);                  // h + t1 - 17 q = -9 c3
		k_neg_n(vm2, vm2, n);
		k_divexact_1(vm2, vm2, n, 9);           // c3
		k_sub_n(vh, vh, vm1, n);                // h - t1 = 15 (c1 - c5)
		k_divexact_1(vh, vh, n, 15);            // d = c1 - c5, negative when c1 < c5
		k_sub_n(x, x, vm2, n);                  // c1 + c5 = q - c3
		k_add_n(vm1, x, vh, n);                 // 2 c1
		k_rshift(vm1, vm1, n, 1);               // c1
		k_sub_n(x, x, vm1, n);                  // c5
		auto add = [&](const digit* p, std::size_t at) {
			std::size_t pn = n;
			while (pn && !p[pn - 1]) {
				--pn;
			}
			k_add_at(r, rn, at, p, pn);
		};
		add(vm1, k);
		add(v1, 2 * k);
		add(vm2, 3 * k);
		add(v2, 4 * k);
		add(x, 5 * k);
	}

	// r[0..an+bn) = a * b by Toom-4, for an >= bn > 3k with k = ceil(an / 4): both operands split into
	// four pieces, the product read off seven products at 0, 1, -1, 2, -2, 1/2 and infinity.
	static void k_mul_toom4(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s) {
		const std::size_t k = (an + 3) / 4;
		const std::size_t m = k + 1;
		const std::size_t n = 2 * m;
		const std::size_t a3n = an - 3 * k;
		const std::size_t b3n = bn - 3 * k;
		assert(an >= bn && b3n >= 1);

		// c0 = a0 b0 and c6 = a3 b3 land directly in r.
		k_mul(r, a, k, b, k, s);
		k_mul(r + 6 * k, a + 3 * k, a3n, b + 3 * k, b3n, s);
		std::fill(r + 2 * k, r + 6 * k, digit(0));

		digit* v1 = s;
		digit* vm1 = v1 + n;
		digit* v2 = vm1 + n;
		digit* vm2 = v2 + n;
		digit* vh = vm2 + n;
		digit* ap = vh + n;
		digit* am = ap + m;
		digit* bp = am + m;
		digit* bm = bp + m;
		digit* w = bm + m;
		s = w + m;

		unsigned sign = k_toom4_eval1(ap, am, w, a, k, a3n) ^ k_toom4_eval1(bp, bm, w, b, k, b3n);
		k_mul(v1, ap, m, bp, m, s);
		k_mul(vm1, am, m, bm, m, s);
		sign |= (k_toom4_eval2(ap, am, w, a, k, a3n) ^ k_toom4_eval2(bp, bm, w, b, k, b3n)) << 1;
		k_mul(v2, ap, m, bp, m, s);
		k_mul(vm2, am, m, bm, m, s);
		k_toom4_evalh(ap, a, k, a3n);
		k_toom4_evalh(bp, b, k, b3n);
		k_mul(vh, ap, m, bp, m, s);
		// The evaluation buffers are dead: the interpolation takes N limbs of them as its temporary.
		k_toom4_interp(r, an + bn, k, a3n + b3n, v1, vm1, v2, vm2, vh, ap, sign & 1, sign & 2);
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
		} else if (k_sqr_uses_ntt(n)) {
			k_ntt_mul(r, a, n, a, n, true, s);
		} else if (k_sqr_uses_toom3(n)) {
			k_sqr_toom3(r, a, n, s);
		} else {
			k_sqr_karatsuba(r, a, n, s);
		}
	}

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

	// One quotient digit of schoolbook division: divides the window w[0..dn] (below dp * B) by the
	// normalized dp[0..dn) (dn >= 2), leaves the remainder in w[0..dn) and returns the digit.
	static digit k_div_sb_digit(digit* w, const digit* dp, std::size_t dn, digit d1, digit d0, digit dinv) {
		digit qd;
		if (w[dn] == d1 && w[dn - 1] == d0) {
			// The estimate is the largest limb; the 3-by-2 step does not apply.
			qd = static_cast<digit>(~static_cast<digit>(0));
			digit cy = k_submul_1(w, dp, dn, qd);
			digit bor = _subborrow(w[dn], cy, 0, &w[dn]);
			if (bor) {
				--qd;
				digit c = k_add_n(w, w, dp, dn);
				w[dn] = static_cast<digit>(w[dn] + c);
			}
			return qd;
		}
		// The 3-by-2 step already holds the top two limbs of the remainder, (r1, r0): the
		// product only subtracts from the low dn-2 limbs, and its borrow comes off (r1, r0).
		// The estimate is at most one too large: a borrow out of (r1, r0) adds the divisor back.
		digit r1, r0;
		qd = k_div_3by2(w[dn], w[dn - 1], w[dn - 2], d1, d0, dinv, &r1, &r0);
		const digit cy = k_submul_1(w, dp, dn - 2, qd);
		const digit b = _subborrow(r0, cy, 0, &w[dn - 2]);
		const digit bor = _subborrow(r1, 0, b, &w[dn - 1]);
		w[dn] = 0;
		if (bor) {
			--qd;
			k_add_n(w, w, dp, dn);
		}
		return qd;
	}

	// Schoolbook division of np[0..nn) by the normalized dp[0..dn) (dn >= 2, top bit set).
	// Requires the top dn limbs of np to be below dp; writes nn-dn quotient limbs to q
	// and leaves the remainder in np[0..dn).
	static void k_div_sb(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn) {
		const digit d1 = dp[dn - 1];
		const digit d0 = dp[dn - 2];
		const digit dinv = k_inv_3by2(d1, d0);
		for (std::size_t i = nn - dn; i--;) {
			q[i] = k_div_sb_digit(np + i, dp, dn, d1, d0, dinv);
		}
	}

	// Quotient-only schoolbook division, on the contract of k_div_sb without the remainder. The
	// digit that is `i` places above the last one subtracts its product only over the top
	// min(dn-2, i + G) limbs below the 3-by-2 window (G = div_trunc_window): the limbs under them
	// stay stale, since the digits that follow read nothing below limb dn-2-G. That is about
	// qn^2/2 limb products where k_div_sb takes qn * dn, and it makes the window every later
	// 3-by-2 step reads inexact. A digit decided on an inexact window is right unless its
	// remainder lies within the error of a decision edge; the kernel detects those digits and
	// returns nonzero for one (1: the window's top limbs equal the divisor's; 2: the remainder's
	// top two limbs are zero), leaving np and q clobbered. The caller then redoes the division with
	// k_div_sb on a fresh copy of the dividend. It returns 0 when every digit is proved equal to
	// the exact one; np[0..dn) is not a remainder then.
	//
	// The bound (campaign note divq.md and ARCHITECTURE.md derive it in full). B is the limb base,
	// D the divisor, D' its top two limbs, and qn < B^(G-1) (the kernel is exact for a longer
	// quotient). A digit m below dn-2-G leaves the low k = dn-2-m-G limbs of its product out: an
	// error q_m (D mod B^k) B^m < B^(dn-1-G), so at digit i the window W is the true one plus
	// delta <= qn B^(dn-1-G-i) + 1. With q^ the estimate (floor(W / D) or one more) and
	// Y = W - q^ (D - D mod B^k), the true remainder is Y - u, u = q^ (D mod B^k) < B^(dn-1-G-i),
	// so the computed digit q^ - [Y < 0] differs from the true one only for 0 <= Y < u + delta,
	// and u + delta < B^(dn-2). Y < B^(dn-2) means its top two limbs are zero: flag 2. A step
	// without a borrow leaves Y below D' B^(dn-2), and one with a borrow leaves it below D, so the
	// next window's top limbs are at most those of D: only their equality (the case k_div_sb
	// handles apart) can break the 3-by-2 step, flag 1. Digits with i >= dn-2-G subtract the
	// whole product, are exact, and run k_div_sb_digit unchanged.
#ifdef UINTEGER_T_DIV_TRUNC_WINDOW
	static constexpr std::size_t div_trunc_window = UINTEGER_T_DIV_TRUNC_WINDOW;
#else
	static constexpr std::size_t div_trunc_window = 2;
#endif
	static_assert(div_trunc_window >= 2, "the flag test is proved for a window of two limbs or more");

	static unsigned k_div_sb_trunc(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn) {
		const std::size_t qn = nn - dn;
		// The bound needs qn < B; a longer quotient (16-bit limbs only, in practice) is exact.
		if (qn > static_cast<std::size_t>(~static_cast<digit>(0))) {
			k_div_sb(q, np, nn, dp, dn);
			return 0;
		}
		const digit d1 = dp[dn - 1];
		const digit d0 = dp[dn - 2];
		const digit dinv = k_inv_3by2(d1, d0);
		// Digits at or above `exact` subtract the whole product; those below it are truncated.
		const std::size_t exact = dn - 2 > div_trunc_window ? dn - 2 - div_trunc_window : 0;
		std::size_t i = qn;
		for (; i > exact; --i) {
			q[i - 1] = k_div_sb_digit(np + i - 1, dp, dn, d1, d0, dinv);
		}
		while (i--) {
			digit* w = np + i;
			if (w[dn] == d1 && w[dn - 1] == d0) {
				return 1;
			}
			digit r1, r0;
			digit qd = k_div_3by2(w[dn], w[dn - 1], w[dn - 2], d1, d0, dinv, &r1, &r0);
			const std::size_t len = i + div_trunc_window;
			const std::size_t k0 = dn - 2 - len;
			const digit cy = k_submul_1(w + k0, dp + k0, len, qd);
			const digit b = _subborrow(r0, cy, 0, &w[dn - 2]);
			const digit bor = _subborrow(r1, 0, b, &w[dn - 1]);
			if (bor) {
				--qd;
				k_add_n(w + k0, w + k0, dp + k0, dn - k0);
			} else if (!(w[dn - 1] | w[dn - 2])) {
				return 2;
			}
			q[i] = qd;
		}
		return 0;
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

	// Sizes the value to `n` limbs without initializing new ones: the caller writes every limb
	// it adds (and no operand limb is read from the new range) before anything reads it.
	void resize_uninit(std::size_t n) {
		_value.resize_uninit(n);
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
	// The number of bits in a digit of `base` when `base` is a power of two from 2 to 256 (1 to 8),
	// else 0; `base` must be in [1, 256]. These bases convert by slicing bits. base-x sizes the
	// blocks of its padded alphabets with it.
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

	// The number of digits of `base` that write the largest 64-bit value (64 for base 2, 20 for base
	// 10, 8 for base 256), a table over [1, 256] (0 for base 1). `digits()` sizes its output with it.
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

UINTEGER_T_PRIVATE:
	// Operator implementations
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
	static uinteger_t& lshift(uinteger_t& result, const uinteger_t& lhs, unsigned long long n) {
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

		result.resize_uninit(rn);
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
	static uinteger_t& rshift(uinteger_t& result, const uinteger_t& lhs, unsigned long long n) {
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
			result.resize_uninit(rn);
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

	// The count held in a uinteger_t, when it fits 64 bits (up to 64 / digit_bits limbs). A longer
	// count is 2^64 bits or more, beyond any size a value can have: a left shift of a nonzero
	// value by it cannot be represented and a right shift gives zero.
	static bool shift_count(const uinteger_t& rhs, unsigned long long& n) {
		if (rhs.size() > 64 / digit_bits) {
			return false;
		}
		n = 0;
		for (std::size_t i = 0; i < rhs.size(); ++i) {
			n |= static_cast<unsigned long long>(rhs.data()[i]) << (digit_bits * i);
		}
		return true;
	}

	static uinteger_t& bitwise_lshift(uinteger_t& lhs, const uinteger_t& rhs) {
		return bitwise_lshift(lhs, lhs, rhs);
	}

	static uinteger_t& bitwise_lshift(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		unsigned long long n = 0;
		if (!shift_count(rhs, n)) {
			if (lhs) {
				throw std::length_error("Error: Shift too large");
			}
			result.clear();
			return result;
		}
		return lshift(result, lhs, n);
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
		unsigned long long n = 0;
		if (!shift_count(rhs, n)) {
			result.clear();
			return result;
		}
		return rshift(result, lhs, n);
	}

	static uinteger_t bitwise_rshift(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		bitwise_rshift(result, lhs, rhs);
		return result;
	}

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


	// A quotient-only division takes k_div_sb_trunc when the quotient is no longer than the divisor
	// and under div_trunc_threshold limbs: its cost is about qn^2/2 limb products whatever the
	// divisor's length, and nothing in it reaches the divisor's low limbs. A longer quotient truncates
	// only its last dn digits, too few to repay a rare redo.
#ifdef UINTEGER_T_DIV_TRUNC_THRESHOLD
	static constexpr std::size_t div_trunc_threshold = UINTEGER_T_DIV_TRUNC_THRESHOLD;
#else
	static constexpr std::size_t div_trunc_threshold = 256;
#endif

	static bool k_div_uses_trunc(std::size_t qn, std::size_t dn) {
		return qn <= dn && qn < div_trunc_threshold;
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
		const bool trunc = !remainder && k_div_uses_trunc(qn, dn);
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
			// A flagged digit sends the division back to the exact kernel on a fresh dividend
			// (`lhs` is intact until the outputs are written).
			if (k_div_sb_trunc(q, n, nn + 1, d, dn)) {
				load();
				k_div_sb(q, n, nn + 1, d, dn);
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
#ifndef UINTEGER_T_RADIX_MAX_BITS
#define UINTEGER_T_RADIX_MAX_BITS 8192
#endif
	static constexpr std::size_t radix_max_bits = UINTEGER_T_RADIX_MAX_BITS;
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
		std::uint64_t b8;          // base^8 modulo 2^64, the step of the decoder's eight-digit groups
		std::uint64_t cinv[digit_bits > 20 ? digit_bits + 1 : 21];  // ceil(2^64 / base^m); h <= 20, which the fixed splitters index at any limb width
		radix_div div0, div1;      // chunk / bh, and the quotient / bh when k = 2h + 1
		const unsigned char* pair; // two digits per entry for base <= 64
		std::vector<unsigned char> pair_store;
		void (*split)(const radix&, digit, unsigned char*);  // _split_chunk for the shape (k, h) of this base
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
		rx->b8 = (b * b * b * b) * (b * b * b * b);
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
		rx->split = _pick_split(rx->k, rx->h);
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

	// The digit_octets bytes at p, first byte most significant, as a limb.
	static digit _load_be(const unsigned char* p) {
		digit v = 0;
		for (std::size_t i = 0; i < digit_octets; ++i) {
			v = static_cast<digit>((v << 8) | p[i]);
		}
		return v;
	}

	// Writes a limb to the digit_octets bytes at p, most significant byte first.
	static void _store_be(unsigned char* p, digit v) {
		for (std::size_t i = digit_octets; i--; v = static_cast<digit>(v >> 8)) {
			p[i] = static_cast<unsigned char>(v);
		}
	}

	// Writes the `count` low-order bytes of the n >= 1 limbs at p to dst, most significant first;
	// count is (n - 1) * digit_octets plus the bytes of the top limb, which is nonzero.
	static void _store_bytes(unsigned char* dst, const digit* p, std::size_t n, std::size_t count) {
		const std::size_t top = count - (n - 1) * digit_octets;
		for (std::size_t j = 0; j < top; ++j) {
			dst[j] = static_cast<unsigned char>(p[n - 1] >> (8 * (top - 1 - j)));
		}
		dst += top;
		for (std::size_t i = n - 1; i--; dst += digit_octets) {
			_store_be(dst, p[i]);
		}
	}

	// Appends the digits of the value to `out`: one-byte results are written in place.
	template <typename Result, typename Fill>
	static void _digits_append(Result& out, std::size_t cap, Fill fill, std::true_type) {
		const auto start = out.size();
		out.resize(start + cap);
		std::size_t count;
		try {
			count = fill(reinterpret_cast<unsigned char*>(&out[start]));
		} catch (...) {
			out.resize(start);  // the tree allocates; leave `out` as it was
			throw;
		}
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

	// Writes the value of m >= 1 digits d[0..m), each below `base`, most significant first, to
	// x and returns its limb count. x holds chunks = ceil(m / k) limbs and nothing beyond
	// them is touched, which is what lets a short value live on the stack (L1). The chunks go
	// into x itself, and Horner step j reads chunk j and leaves at most j limbs below it, so
	// nothing unread is overwritten: the value of the first j chunks is below D^j < B^j, so
	// n <= j on entry to step j; the first steps write x[n] <= x[j]; a four-chunk step at j
	// (four chunks remain, j + 3 < chunks) reads x[j .. j + 3] before it writes x[n .. n + 3]
	// with n + 3 <= j + 3. The result has n <= chunks limbs.
	static std::size_t _horner_limbs(const radix& rx, const unsigned char* d, std::size_t m, digit* x) {
		const unsigned k = rx.k;
		std::size_t first = m % k;
		if (!first) {
			first = k;
		}
		const std::size_t chunks = (m - first) / k + 1;
		const std::uint64_t b8 = rx.b8;
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
		return n;
	}

	// Up to this many chunks (a 64-byte key in base 58 is nine) the decoder works in a stack
	// buffer and sizes the result to the limbs the value has, so a short value is never
	// allocated larger than itself (an inline buffer holds it); longer ones work in the result.
	static constexpr std::size_t horner_stack_chunks = 16;

	// The value of m >= 1 digits d[0..m), each below `base`, most significant first.
	static uinteger_t _from_digit_horner(const unsigned char* d, std::size_t m, int base) {
		const radix& rx = _radix_for(base);
		const std::size_t chunks = m / rx.k + (m % rx.k != 0);
		uinteger_t result;
		if (chunks <= horner_stack_chunks) {
			digit x[horner_stack_chunks];
			const std::size_t n = _horner_limbs(rx, d, m, x);
			result._value.resize_uninit(n);
			std::copy(x, x + n, result._value.data());
		} else {
			result._value.resize(chunks);
			result._value.resize(_horner_limbs(rx, d, m, result._value.data()));
		}
		return result;
	}

	// Subquadratic conversion, a tree over the kernels above (Brent and Zimmermann, Modern
	// Computer Arithmetic, 2010, section 1.7). The powers P_j = D^(T 2^j) are built by
	// squaring from P_0 = D^T, so a leaf is T chunks, s = T k digits, below P_0. digits() splits
	// a value below P_j with one division by P_(j-1): the quotient is the high half and the
	// remainder the low half, padded with zeros to s 2^(j-1) digits. from_digits() joins two
	// halves as high * P_(j-1) + low. Each level costs a few multiplications of its size, so
	// the whole is O(M(n) log n) against the kernels' O(n^2).
	// The number of levels and T follow the size of the value, so the tree is full: the leaves
	// have between half and all of the leaf limit and the top power is about the size of the
	// value. The tree takes over above `*_tree_threshold` limbs; a leaf is at most `*_tree_leaf`
	// limbs. The defaults are bit counts over the limb width, from sweeps at both widths (the
	// kernels' limit, then 7680 and 8192 bits of leaf; every leaf size from 4096 to 12288 bits
	// is within 3%); the tests set tiny limb counts to run the tree at every size.
#ifndef UINTEGER_T_DIGITS_TREE_THRESHOLD
#define UINTEGER_T_DIGITS_TREE_THRESHOLD (8192 / (sizeof(DIGIT_T) * 8))
#endif
#ifndef UINTEGER_T_DIGITS_TREE_LEAF
#define UINTEGER_T_DIGITS_TREE_LEAF (7680 / (sizeof(DIGIT_T) * 8))
#endif
#ifndef UINTEGER_T_FROM_DIGITS_TREE_THRESHOLD
#define UINTEGER_T_FROM_DIGITS_TREE_THRESHOLD (8192 / (sizeof(DIGIT_T) * 8))
#endif
#ifndef UINTEGER_T_FROM_DIGITS_TREE_LEAF
#define UINTEGER_T_FROM_DIGITS_TREE_LEAF (8192 / (sizeof(DIGIT_T) * 8))
#endif
	static constexpr std::size_t digits_tree_threshold = UINTEGER_T_DIGITS_TREE_THRESHOLD;
	static constexpr std::size_t digits_tree_leaf = UINTEGER_T_DIGITS_TREE_LEAF;
	static constexpr std::size_t from_digits_tree_threshold = UINTEGER_T_FROM_DIGITS_TREE_THRESHOLD;
	static constexpr std::size_t from_digits_tree_leaf = UINTEGER_T_FROM_DIGITS_TREE_LEAF;
	// A leaf is two chunks at least (so P_0 has two limbs, the least a division takes) and its
	// limbs, with the few a retry adds, must fit the kernels.
	static_assert(digits_tree_leaf >= 2 && digits_tree_leaf + 4 <= radix_max_limbs, "the digits tree leaf must fit the kernels");
	static_assert(from_digits_tree_leaf >= 2, "the from_digits tree leaf must hold two limbs");

	static constexpr unsigned radix_tree_max_levels = 8 * sizeof(std::size_t);

	// The powers P_j for j < levels, and the size of the tree they make.
	struct radix_tree {
		unsigned levels;                        // J: values below P_J = P_(J-1)^2
		unsigned chunks;                        // T
		std::size_t leaf_digits;                // s = T k
		std::size_t size[radix_tree_max_levels];    // limbs of P_j
		unsigned shift[radix_tree_max_levels];      // P_j is stored shifted left by this much (digits() only)
		digit* power[radix_tree_max_levels];
		std::unique_ptr<digit[]> store;
	};

	// The levels J >= 1 and leaf chunks T >= 2 for a value of `chunks` chunks and leaves of at
	// most `leaf_limbs` limbs: the fewest levels for which T 2^J >= chunks.
	static void _tree_shape(const radix& rx, std::size_t chunks, std::size_t leaf_limbs, unsigned& levels, unsigned& leaf_chunks) {
		const std::size_t longest = std::max<std::size_t>(2, leaf_limbs * digit_bits / _bit_length(rx.D));
		unsigned j = 1;
		std::size_t t = (chunks + 1) / 2;
		while (t > longest) {
			++j;
			t = (chunks + (static_cast<std::size_t>(1) << j) - 1) >> j;
		}
		levels = j;
		leaf_chunks = static_cast<unsigned>(std::max<std::size_t>(t, 2));
	}

	// out = D^e for e >= 1 by squarings and multiplications by D, left to right. `out` and
	// `tmp` each hold the result and two limbs; returns its limb count.
	static std::size_t _tree_pow(digit D, unsigned e, digit* out, digit* tmp) {
		digit* cur = out;
		digit* next = tmp;
		cur[0] = D;
		std::size_t n = 1;
		unsigned bit = 0;
		while (e >> (bit + 1)) {
			++bit;
		}
		while (bit--) {
			{
				k_scratch scratch(k_sqr_scratch_size(n));
				k_sqr(next, cur, n, scratch.data());
			}
			n *= 2;
			if (!next[n - 1]) {
				--n;
			}
			std::swap(cur, next);
			if ((e >> bit) & 1) {
				const digit carry = k_mul_1(cur, cur, n, D);
				if (carry) {
					cur[n++] = carry;
				}
			}
		}
		if (cur != out) {
			std::copy(cur, cur + n, out);
		}
		return n;
	}

	// Builds P_0 .. P_(levels-1). With `normalize` each is shifted left to set its top bit,
	// the form k_div_dc divides by, and its shift is kept.
	static void _tree_build(const radix& rx, radix_tree& tr, unsigned levels, unsigned chunks, bool normalize) {
		assert(levels >= 1 && levels < radix_tree_max_levels && chunks >= 2);
		const std::size_t cap0 = (static_cast<std::size_t>(chunks) * _bit_length(rx.D) + digit_bits - 1) / digit_bits + 2;
		tr.levels = levels;
		tr.chunks = chunks;
		tr.leaf_digits = static_cast<std::size_t>(chunks) * rx.k;
		// Region j holds cap0 2^j limbs, which P_j fits.
		tr.store.reset(new digit[cap0 * ((static_cast<std::size_t>(1) << levels) - 1)]);
		std::size_t offset = 0;
		for (unsigned j = 0; j < levels; ++j) {
			tr.power[j] = tr.store.get() + offset;
			offset += cap0 << j;
		}
		{
			std::unique_ptr<digit[]> tmp(new digit[cap0]);
			tr.size[0] = _tree_pow(rx.D, chunks, tr.power[0], tmp.get());
		}
		for (unsigned j = 0; j < levels; ++j) {
			if (j + 1 < levels) {
				const std::size_t n = tr.size[j];
				{
					k_scratch scratch(k_sqr_scratch_size(n));
					k_sqr(tr.power[j + 1], tr.power[j], n, scratch.data());
				}
				tr.size[j + 1] = tr.power[j + 1][2 * n - 1] ? 2 * n : 2 * n - 1;
			}
			tr.shift[j] = 0;
			if (normalize) {
				const unsigned sh = static_cast<unsigned>(digit_bits - _bits(tr.power[j][tr.size[j] - 1]));
				if (sh) {
					k_lshift(tr.power[j], tr.power[j], tr.size[j], sh);
				}
				tr.shift[j] = sh;
			}
		}
	}

	// The leaf of digits(): the table for P_0's limb count, and buffers for the padded input
	// and the digits the kernel writes.
	struct radix_leaf {
		const radix_table* table;
		std::size_t limbs;
		std::unique_ptr<digit[]> pad;
		std::unique_ptr<unsigned char[]> buffer;
	};

	// The s digits of x < P_0 (nx >= 1 limbs), zero-padded on the left. The bounds are checked
	// in every build: a value past P_0 would otherwise lose its high limbs silently (SR5).
	static void _tree_leaf(const radix& rx, const radix_tree& tr, const radix_leaf& lf, const digit* x, std::size_t nx, unsigned char* out) {
		if (nx > lf.limbs) {
			throw std::logic_error("Error: Digit tree leaf out of range");
		}
		const digit* in = x;
		if (nx < lf.limbs) {
			std::copy(x, x + nx, lf.pad.get());
			std::fill(lf.pad.get() + nx, lf.pad.get() + lf.limbs, digit(0));
			in = lf.pad.get();
		}
		const std::size_t count = _encode_radix(rx, *lf.table, in, lf.limbs, lf.buffer.get());
		if (count > tr.leaf_digits) {
			throw std::logic_error("Error: Digit tree leaf out of range");
		}
		std::memset(out, 0, tr.leaf_digits - count);
		std::memcpy(out + tr.leaf_digits - count, lf.buffer.get(), count);
	}

	// The s 2^j digits of x < P_j, zero-padded, written to out. x has nx limbs and room for one
	// more, and is clobbered. Dividing by P_(j-1) (k_div_dc) takes the dividend and divisor
	// shifted left to normalize the divisor; the quotient is below P_(j-1) and the
	// remainder, shifted back, is the low half.
	static void _tree_digits(const radix& rx, const radix_tree& tr, const radix_leaf& lf, unsigned j, digit* x, std::size_t nx, unsigned char* out) {
		while (nx && !x[nx - 1]) {
			--nx;
		}
		if (!nx) {
			std::memset(out, 0, tr.leaf_digits << j);
			return;
		}
		if (!j) {
			_tree_leaf(rx, tr, lf, x, nx, out);
			return;
		}
		const std::size_t half = tr.leaf_digits << (j - 1);
		const std::size_t dn = tr.size[j - 1];
		if (nx < dn) {
			// Below P_(j-1): the high half is zero.
			std::memset(out, 0, half);
			_tree_digits(rx, tr, lf, j - 1, x, nx, out + half);
			return;
		}
		const unsigned sh = tr.shift[j - 1];
		x[nx] = sh ? k_lshift(x, x, nx, sh) : 0;
		const std::size_t nn = nx + 1;
		const std::size_t qn = nn - dn;
		std::unique_ptr<digit[]> q(new digit[qn + 1]);
		{
			k_scratch scratch(k_div_dc_scratch_size(nn, dn));
			k_div_dc(q.get(), x, nn, tr.power[j - 1], dn, scratch.data());
		}
		if (sh) {
			k_rshift(x, x, dn, sh);
		}
		_tree_digits(rx, tr, lf, j - 1, q.get(), qn, out);
		q.reset();
		_tree_digits(rx, tr, lf, j - 1, x, dn, out + half);
	}

	// Writes the digits of the n >= 1 limbs of x (top limb nonzero) to out[0..s 2^J) and strips
	// the leading zeros; returns the count.
	static std::size_t _tree_encode(const radix& rx, const radix_tree& tr, const radix_leaf& lf, const digit* x, std::size_t n, unsigned char* out) {
		std::unique_ptr<digit[]> copy(new digit[n + 1]);
		std::copy(x, x + n, copy.get());
		_tree_digits(rx, tr, lf, tr.levels, copy.get(), n, out);
		return _strip_zeros(out, tr.leaf_digits << tr.levels);
	}

	// The digits s 2^levels that a tree of leaves of `leaf_digits` digits writes, as a Size: a
	// count that does not fit throws std::length_error, as _limbs_for_digits does (a 32-bit
	// size_t wraps at 2^32 digits). Production uses std::size_t; tests pass narrower sizes.
	template <typename Size>
	static Size _tree_digits_for(std::size_t leaf_digits, unsigned levels) {
		const auto max = static_cast<unsigned long long>(std::numeric_limits<Size>::max());
		if (levels >= 64 || static_cast<unsigned long long>(leaf_digits) > (max >> levels)) {
			throw std::length_error("Error: Value too large to convert");
		}
		return static_cast<Size>(static_cast<unsigned long long>(leaf_digits) << levels);
	}

	// Plans the tree for values of `bits` bits: the powers (normalized for division) and the leaf.
	// The tree covers D^chunks, which must pass 2^bits: `slack` chunks beyond the estimate keep
	// the check below from ever failing; tests pass a negative one to run the retry. The digits
	// the tree writes must fit a Size; that is checked before the powers are allocated.
	template <typename Size = std::size_t>
	static void _tree_plan(const radix& rx, std::size_t bits, std::size_t leaf_limbs, int slack, radix_tree& tr, radix_leaf& lf) {
		const double estimate = std::ceil(static_cast<double>(bits) / std::log2(static_cast<double>(rx.D)));
		const std::size_t chunks = static_cast<std::size_t>(std::max(2.0, estimate + slack));
		unsigned levels, leaf;
		_tree_shape(rx, chunks, std::min(leaf_limbs, radix_max_limbs - 4), levels, leaf);
		for (;; ++leaf) {
			(void)_tree_digits_for<Size>(static_cast<std::size_t>(leaf) * rx.k, levels);
			_tree_build(rx, tr, levels, leaf, true);
			// P_J = P_(J-1)^2 is above 2^(2 bits(P_(J-1)) - 2); the value must be below it.
			// 64-bit arithmetic: the top power's bit count can reach 2^31 on a 32-bit size_t.
			typedef unsigned long long wide;
			if (static_cast<wide>(bits) + 2 <= 2 * (static_cast<wide>(tr.size[levels - 1]) * digit_bits - tr.shift[levels - 1])) {
				break;
			}
		}
		assert(tr.size[0] <= radix_max_limbs);
		lf.limbs = tr.size[0];
		lf.table = &_table_for(rx, lf.limbs, lf.limbs * digit_bits);
		lf.pad.reset(new digit[lf.limbs]);
		lf.buffer.reset(new unsigned char[static_cast<std::size_t>(lf.table->chunks) * rx.k]);
	}

	// Appends the digits of the n >= 1 limbs at p (top limb nonzero), through the tree with
	// leaves of at most `leaf_limbs` limbs (tests pass other sizes than the default). The bit
	// count and the digit capacity are Size arithmetic that throws std::length_error when it
	// does not fit (SR1: a 32-bit size_t wraps the bit count at 512 MiB), before anything is
	// allocated; tests pass a narrower Size to take the guard.
	template <typename Size = std::size_t, typename Result>
	static void _digits_tree(Result& out, int base, const digit* p, std::size_t n, std::size_t leaf_limbs = digits_tree_leaf) {
		using value_type = typename Result::value_type;
		using in_place = std::integral_constant<bool, std::is_integral<value_type>::value && sizeof(value_type) == 1 && !std::is_same<value_type, bool>::value>;
		const radix& rx = _radix_for(base);
		const Size bits = _digits_for_limbs<Size>(static_cast<Size>(n), static_cast<unsigned>(_bits(p[n - 1])), 1);
		radix_tree tr;
		radix_leaf lf;
		_tree_plan<Size>(rx, static_cast<std::size_t>(bits), leaf_limbs, 1, tr, lf);
		const std::size_t cap = static_cast<std::size_t>(_tree_digits_for<Size>(tr.leaf_digits, tr.levels));
		if (cap > out.max_size() - out.size()) {
			throw std::length_error("Error: Value too large to convert");
		}
		_digits_append(out, cap, [&](unsigned char* buffer) { return _tree_encode(rx, tr, lf, p, n, buffer); }, in_place());
	}

	// Limbs for a value below P_j (the high half times P_(j-1) and the carry's room).
	static std::size_t _tree_limbs(const radix_tree& tr, unsigned j) {
		return (j ? 2 * tr.size[j - 1] : tr.size[0]) + 2;
	}

	// The value of the m >= 1 digits d[0..m) (m <= s 2^j) into out, which holds _tree_limbs(j)
	// limbs; returns the limb count. The low half is the last s 2^(j-1) digits.
	static std::size_t _tree_from(const radix_tree& tr, int base, unsigned j, const unsigned char* d, std::size_t m, digit* out) {
		if (m <= tr.leaf_digits) {
			const uinteger_t leaf = _from_digit_horner(d, m, base);
			std::copy(leaf.data(), leaf.data() + leaf.size(), out);
			return leaf.size();
		}
		assert(j >= 1);
		const std::size_t half = tr.leaf_digits << (j - 1);
		if (m <= half) {
			return _tree_from(tr, base, j - 1, d, m, out);
		}
		const std::size_t mh = m - half;
		const std::size_t p = tr.size[j - 1];
		std::unique_ptr<digit[]> part(new digit[_tree_limbs(tr, j - 1)]);
		const std::size_t hn = _tree_from(tr, base, j - 1, d, mh, part.get());
		std::size_t n = 0;
		if (hn) {
			k_scratch scratch(k_mul_scratch_size(hn, p));
			k_mul(out, part.get(), hn, tr.power[j - 1], p, scratch.data());
			n = hn + p;
		}
		const std::size_t ln = _tree_from(tr, base, j - 1, d + mh, half, part.get());
		if (!hn) {
			std::copy(part.get(), part.get() + ln, out);
			return ln;
		}
		if (ln) {
			// high * P + low < (high + 1) P <= B^(hn + p), so the sum has no carry out.
			digit carry = k_add_n(out, out, part.get(), ln);
			if (carry) {
				carry = k_add_1(out + ln, out + ln, n - ln, carry);
			}
			assert(!carry); (void)(carry);
		}
		while (n && !out[n - 1]) {
			--n;
		}
		return n;
	}

	static uinteger_t _from_digit_tree(const radix& rx, const unsigned char* d, std::size_t m, int base, std::size_t leaf_limbs = from_digits_tree_leaf) {
		while (m && !*d) {
			++d;
			--m;
		}
		uinteger_t result;
		if (!m) {
			return result;
		}
		unsigned levels, leaf;
		_tree_shape(rx, m / rx.k + (m % rx.k != 0), leaf_limbs, levels, leaf);
		radix_tree tr;
		_tree_build(rx, tr, levels, leaf, false);
		std::unique_ptr<digit[]> value(new digit[_tree_limbs(tr, levels)]);
		const std::size_t n = _tree_from(tr, base, levels, d, m, value.get());
		result._value.resize(n);
		std::copy(value.get(), value.get() + n, result._value.data());
		return result;
	}

	// The value of m >= 1 digits d[0..m), each below `base`, most significant first.
	static uinteger_t _from_digit_bytes(const unsigned char* d, std::size_t m, int base) {
		const radix& rx = _radix_for(base);
		if (m / rx.k > from_digits_tree_threshold * digit_bits / _bit_length(rx.D)) {
			return _from_digit_tree(rx, d, m, base);
		}
		return _from_digit_horner(d, m, base);
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

UINTEGER_T_PRIVATE:
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

	uinteger_t(const uinteger_t& o) = default;

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

	explicit uinteger_t(const unsigned char* bytes, std::size_t sz, int base) {
		_parse(bytes, sz, base);
	}

	explicit uinteger_t(const char* bytes, std::size_t sz, int base) {
		_parse(bytes, sz, base);
	}

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
	explicit operator signed char() const {
		return static_cast<signed char>(size() ? front() : 0);
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

	// Three-way comparison of two values: -1 when lhs < rhs, 0 when they are equal, 1 when lhs > rhs.
	// The values must be trimmed (no leading zero limb), as every operation leaves them.
	static int compare(const uinteger_t& lhs, const uinteger_t& rhs) {
		auto n = lhs.size();
		if (n != rhs.size()) {
			return n > rhs.size() ? 1 : -1;
		}
		return k_cmp_n(lhs.data(), rhs.data(), n);
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

	// result = lhs * rhs. `result` may be `lhs` or `rhs`. A result that is neither keeps its storage,
	// so one reused with room for the product allocates nothing, unless the product's working space
	// exceeds 256 limbs; an operand taken as the result is rebuilt through a temporary. Returns `result`.
	static uinteger_t& mul(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		return mult(result, lhs, rhs);
	}

	// quotient = lhs / rhs and remainder = lhs % rhs in one division. Either output may be `lhs` or
	// `rhs`; `quotient` and `remainder` must be different objects. An output that is not an operand
	// keeps its storage, so outputs reused with room allocate nothing, unless the division's working
	// space exceeds 256 limbs (a dividend above 73 limbs). Throws `std::domain_error` when `rhs` is
	// zero, before it writes an output. Returns references to `quotient` and `remainder`.
	static std::pair<std::reference_wrapper<uinteger_t>, std::reference_wrapper<uinteger_t>> divmod(uinteger_t& quotient, uinteger_t& remainder, const uinteger_t& lhs, const uinteger_t& rhs) {
		divide(&quotient, &remainder, lhs, rhs);
		return std::make_pair(std::ref(quotient), std::ref(remainder));
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
			if (bits_per_digit == 8 && std::is_integral<value_type>::value && sizeof(value_type) == 1 && !std::is_same<value_type, bool>::value) {
				// Whole bytes: each limb goes out big-endian.
				_store_bytes(reinterpret_cast<unsigned char*>(&out[start]), p, n, count);
				return;
			}
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
		} else if (radix_fast && n <= radix_max_limbs && n <= digits_tree_threshold) {
			_digits_radix(out, base, p, n);
		} else if (radix_fast) {
			_digits_tree(out, base, p, n);
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
			throw std::invalid_argument("Base must be in the range [2, 36] or 256");
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
		uinteger_t result;
		result._parse(encoded, encoded_size, alphabet_base);
		return result;
	}

private:
	// Sets this value, which is zero, to the number in `encoded_size` characters of text (bases 2 to 36) or
	// bytes (base 256, most significant first).
	void _parse(const void* encoded, std::size_t encoded_size, int alphabet_base) {
		const char* data = (const char *)encoded;
		uinteger_t& result = *this;

		if (alphabet_base >= 2 && alphabet_base <= 36) {
			result = _from_digits(encoded_size, alphabet_base, [data, encoded_size, alphabet_base](std::size_t i) {
				const auto d = ord(static_cast<unsigned char>(data[i]));
				if (d < 0 || d >= alphabet_base) {
					throw std::invalid_argument("Error: Not a digit in base " + std::to_string(alphabet_base) + ": '" + std::string(1, data[i]) + "' at " + std::to_string(encoded_size - i));
				}
				return static_cast<digit>(d);
			});
		} else if (encoded_size && alphabet_base == 256) {
			// Leading zero bytes carry no value, so they are skipped: the top limb of the result is
			// nonzero, and zero bytes make zero (no limbs). Each limb is the digit_octets bytes that
			// end i limbs before the end of the text; the top limb takes what is left over.
			const unsigned char* bytes = static_cast<const unsigned char*>(encoded);
			std::size_t count = encoded_size;
			while (count && !*bytes) {
				++bytes;
				--count;
			}
			const std::size_t whole = count / digit_octets;
			const std::size_t left = count % digit_octets;
			result.resize_uninit(whole + (left ? 1 : 0));
			digit* p = result.data();
			for (std::size_t i = 0; i < whole; ++i) {
				p[i] = _load_be(bytes + count - (i + 1) * digit_octets);
			}
			if (left) {
				digit v = 0;
				for (std::size_t j = 0; j < left; ++j) {
					v = static_cast<digit>((v << 8) | bytes[j]);
				}
				p[whole] = v;
			}
		} else {
			throw std::invalid_argument("Error: Cannot convert from base " + std::to_string(alphabet_base));
		}
	}

public:
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

#undef UINTEGER_T_PRIVATE

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
inline uinteger_t operator<<(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) << rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator<<=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(lhs << rhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
inline uinteger_t operator>>(const T& lhs, const uinteger_t& rhs) {
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

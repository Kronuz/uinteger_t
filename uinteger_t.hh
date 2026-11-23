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
		static void copy_limbs(digit* to, const digit* from, size_type n);

		// Frees the heap block, if there is one. The caller sets `_p` and `_cap` next.
		void release() noexcept;

		// Moves the limbs to a new heap block of exactly `n` limbs (`n > _cap`). Throws before
		// changing anything.
		void reallocate(size_type n);

		// Room for `n` limbs (`n > _cap`): exact from the inline buffer, at least doubled from a
		// heap block, so repeated growth is linear overall.
		void grow(size_type n);

		// A heap block of exactly `n` limbs for a value about to be overwritten (`n > _cap`).
		void replace(size_type n);

		// Copy construction of a value too long to be inline (`_cap` is still `inline_capacity`).
		void copy_to_heap(const container& o);

	public:
		container() noexcept;

		container(const container& o);

		container(container&& o) noexcept;

		~container();

		// Copy assignment reuses the storage when the limbs fit; otherwise it allocates before
		// it releases, so a failure leaves `*this` unchanged.
		container& operator=(const container& o);

		container& operator=(container&& o) noexcept;

		// Replaces the limbs with a copy of [first, last), which must not lie inside this container.
		void assign(const digit* first, const digit* last);

		// Replaces the limbs with `n` copies of `v`.
		void assign(size_type n, digit v);

		size_type size() const noexcept;

		size_type capacity() const noexcept;

		bool empty() const noexcept;

		static constexpr size_type max_size() noexcept;

		digit* data() noexcept;

		const digit* data() const noexcept;

		iterator begin() noexcept;
		const_iterator begin() const noexcept;
		const_iterator cbegin() const noexcept;
		iterator end() noexcept;
		const_iterator end() const noexcept;
		const_iterator cend() const noexcept;
		reverse_iterator rbegin() noexcept;
		const_reverse_iterator rbegin() const noexcept;
		const_reverse_iterator crbegin() const noexcept;
		reverse_iterator rend() noexcept;
		const_reverse_iterator rend() const noexcept;
		const_reverse_iterator crend() const noexcept;

		reference operator[](size_type i) noexcept;
		const_reference operator[](size_type i) const noexcept;
		reference front() noexcept;
		const_reference front() const noexcept;
		reference back() noexcept;
		const_reference back() const noexcept;

		void reserve(size_type n);

		void clear() noexcept;

		void resize(size_type n);

		void resize(size_type n, digit c);

		// Resizes without initializing new limbs, for a caller that writes them before it reads
		// them. UINTEGER_T_POISON_LIMBS fills them with a pattern, so a test can catch a limb left unwritten.
		void resize_uninit(size_type n);

		void push_back(digit v);

		// Inserts `n` copies of `c` before `pos`; returns the position of the first.
		iterator insert(const_iterator pos, size_type n, digit c);
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

	void reserve(std::size_t sz);

	std::size_t grow(std::size_t n);

	void resize(std::size_t sz);

	void resize(std::size_t sz, const digit& c);

	void clear();

	digit* data() noexcept;

	const digit* data() const noexcept;

	std::size_t size() const noexcept;

	void prepend(std::size_t sz, const digit& c);

	void prepend(const digit& c);

	void prepend(const uinteger_t& num);

	void append(std::size_t sz, const digit& c);

	void append(const digit& c);

	void append(const uinteger_t& num);

	container::iterator begin() noexcept;

	container::const_iterator begin() const noexcept;

	container::iterator end() noexcept;

	container::const_iterator end() const noexcept;

	container::reverse_iterator rbegin() noexcept;

	container::const_reverse_iterator rbegin() const noexcept;

	container::reverse_iterator rend() noexcept;

	container::const_reverse_iterator rend() const noexcept;

	container::reference front();

	container::const_reference front() const;

	container::reference back();

	container::const_reference back() const;

UINTEGER_T_PRIVATE:
	// Optimized primitives for operations

	static digit _bits(digit x);

	// (hi, lo) = x * y
	static digit _mult(digit x, digit y, digit* lo);

	// The low limb of x * y. Limbs narrower than int would promote to signed int and overflow
	// (undefined behavior), so the product is taken in an unsigned type at least as wide as int.
	static digit _mullo(digit x, digit y);

	// (hi, lo) = x * y + a + c, which cannot overflow two limbs.
	static digit _multadd(digit x, digit y, digit a, digit c, digit* lo);

	static digit _divmod(digit x_hi, digit x_lo, digit y, digit* result);

	static digit _addcarry(digit x, digit y, digit c, digit* result);

	static digit _subborrow(digit x, digit y, digit c, digit* result);

	// Raw limb kernels over `digit*` arrays of explicit length (little-endian).

	// The carry passes from limb to limb as a value. One limb per pass the compiler spends two dependent
	// operations on it (adds, then a conditional increment); eight limbs per pass it chains them through
	// the carry flag (adds, then adcs back to back), which is half the latency per limb.
	static digit k_add_n(digit* r, const digit* a, const digit* b, std::size_t n);

	// r = a + c for n limbs, returns the carry out.
	static digit k_add_1(digit* r, const digit* a, std::size_t n, digit c);

	static digit k_sub_n(digit* r, const digit* a, const digit* b, std::size_t n);

	// r = a - c for n limbs, returns the borrow out.
	static digit k_sub_1(digit* r, const digit* a, std::size_t n, digit c);

	static digit k_mul_1(digit* r, const digit* a, std::size_t n, digit m);

	static digit k_addmul_1(digit* r, const digit* a, std::size_t n, digit m);

	// r -= a * m over n limbs; returns the borrow limb. One word carries between limbs and takes
	// the last operation on the chain: r[i] - lo and the high product limb are off it, so each
	// limb adds one dependent subtraction, as _multadd does for addition.
	static digit k_submul_1(digit* r, const digit* a, std::size_t n, digit m);

	// r = a << s (0 < s < digit_bits) for n limbs, returns the bits shifted out.
	static digit k_lshift(digit* r, const digit* a, std::size_t n, unsigned s);

	// r = a >> s (0 < s < digit_bits) for n limbs, returns the bits shifted out.
	static digit k_rshift(digit* r, const digit* a, std::size_t n, unsigned s);

	// r = a + (b << s) for 0 < s < digit_bits and n limbs, returns the bits shifted out of b plus the carry (below 2^s + 1).
	static digit k_addlsh_n(digit* r, const digit* a, const digit* b, std::size_t n, unsigned s);

	// r[0..an+bn) = a * b, schoolbook, two rows of b per pass over a so the products and
	// carries of both rows stay in registers. Requires an >= bn >= 1; r must not overlap a or b.
	static void k_mul_basecase(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn);

	// r[off..rn) += p[0..pn), propagating the carry through the rest of r.
	static void k_add_at(digit* r, std::size_t rn, std::size_t off, const digit* p, std::size_t pn);

	// r[0..2n) = a^2, schoolbook. The products a[i] * a[j] with i < j are formed once, two rows of
	// the triangle per pass, then doubled; the squares a[i]^2 are added last. Requires n >= 1; r must
	// not overlap a.
	static void k_sqr_basecase(digit* r, const digit* a, std::size_t n);

	// r[0..n) = |a - b| for a of n limbs and b of bn <= n limbs; returns 1 when a < b. r may be a.
	static digit k_absdiff(digit* r, const digit* a, std::size_t n, const digit* b, std::size_t bn);

	// r[0..n) = -a mod B^n (two's complement); r may be a.
	static void k_neg_n(digit* r, const digit* a, std::size_t n);

	// r[0..n) -= p[0..pn) mod B^n, for pn <= n: the borrow out is dropped.
	static void k_sub_wrap(digit* r, std::size_t n, const digit* p, std::size_t pn);

	// r[0..n) = a / 3 mod B^n when 3 divides a (as the two's complement integer a stands for).
	// Each quotient limb is the limb, less the borrow from below, times the inverse of 3 mod B;
	// the borrow into the next limb is the high part of 3 times that quotient limb.
	static void k_divexact_3(digit* r, const digit* a, std::size_t n);

	// r[0..n) = a / d mod B^n for a small odd d (below 16) that divides a, as k_divexact_3 does for 3:
	// each quotient limb is the limb, less the borrow from below, times d^-1 mod B, and the borrow
	// into the next limb is the high part of d times that quotient limb (Jebelean, "An algorithm for
	// exact division", J. Symbolic Comput. 15, 1993).
	static void k_divexact_1(digit* r, const digit* a, std::size_t n, digit d);

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
	static bool k_mul_uses_toom3(std::size_t an, std::size_t bn);

	static bool k_sqr_uses_toom3(std::size_t n);

	// Toom-4 splits the larger operand into quarters of k limbs and needs a smaller operand with a
	// fourth piece (bn > 3k). Its scratch, with that of the products below it, is under 16 limbs per
	// limb of the larger operand (the property test checks the bound); capping that operand at
	// SIZE_MAX / (32 sizeof(digit)) limbs keeps the scratch in bytes, and every sum of scratch sizes,
	// below the range of std::size_t. A larger product takes Toom-3.
	static constexpr std::size_t toom4_max_limbs = std::numeric_limits<std::size_t>::max() / (32 * sizeof(digit));

	static bool k_mul_uses_toom4(std::size_t an, std::size_t bn);

	// The transform takes a product of at least twice the threshold's limbs in all, whose smaller operand has
	// two thirds of the threshold and whose larger one has under ntt_lopsided times as many: one transform
	// over the whole product costs less than chunking the longer operand, and its scratch stays a few times
	// the product (a longer operand is chunked into products the transform takes). A balanced product of
	// the threshold's limbs is the edge. It needs the transform to be sizable; see _ntt_plan.
	static constexpr std::size_t ntt_lopsided = 16;

	static bool k_mul_uses_ntt(std::size_t an, std::size_t bn);

	static bool k_sqr_uses_ntt(std::size_t n);

	// Scratch limbs k_mul needs for operands of an and bn limbs; zero below the Karatsuba threshold.
	// It follows the same choices as k_mul, level by level.
	static std::size_t k_mul_scratch_size(std::size_t an, std::size_t bn);

	// Scratch limbs k_sqr needs for an n-limb operand; zero below the squaring Karatsuba threshold.
	static std::size_t k_sqr_scratch_size(std::size_t n);

	// With x = x0 + x1 B^k + x2 B^2k (x0 and x1 of k limbs, x2 of x2n <= k limbs, x2n >= 1), the
	// evaluations of the Toom-3 polynomial at 1, -1 and -2, as m = k + 1 limbs each: p1 = x(1),
	// pm1 = |x(-1)|, pm2 = |x(-2)|. `w` is m limbs of temporary space. Returns the signs: bit 0 is set
	// when x(-1) is negative, bit 1 when x(-2) is.
	static unsigned k_toom3_eval(digit* p1, digit* pm1, digit* pm2, digit* w, const digit* x, std::size_t k, std::size_t x2n);

	// Toom-3 interpolation (Bodrato and Zanoni, "Integer and Polynomial Multiplication: Towards Optimal
	// Toom-Cook Matrices", ISSAC 2007), for a product c0 + c1 B^k + c2 B^2k + c3 B^3k + c4 B^4k. On entry
	// r[0..2k) holds c0 and r[4k..4k + vn) holds c4, with r[2k..4k) zero; v1, vm1 and vm2 are the
	// N = 2k + 2 limb products at 1, -1 and -2, vm1 and vm2 meant negative when m1neg and m2neg are
	// set. They are consumed: the middle coefficients are added into r (of rn limbs) at k, 2k and 3k.
	// Every step is exact on the non-negative coefficients; the intermediates may be negative, so they
	// are taken mod B^N in two's complement and the divisions by 2 are only done on non-negative values.
	static void k_toom3_interp(digit* r, std::size_t rn, std::size_t k, std::size_t vn, digit* v1, digit* vm1, digit* vm2, bool m1neg, bool m2neg);

	// r[0..an+bn) = a * b by Toom-3, for an >= bn > 2k with k = ceil(an / 3): both operands split
	// into three pieces, the product read off five products at 0, 1, -1, -2 and infinity.
	static void k_mul_toom3(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s);

	// r[0..2n) = a^2 by Toom-3, for n > 2k with k = ceil(n / 3).
	static void k_sqr_toom3(digit* r, const digit* a, std::size_t n, digit* s);

	// With x = x0 + x1 B^k + x2 B^2k + x3 B^3k (x0, x1 and x2 of k limbs, x3 of x3n <= k limbs, x3n >= 1),
	// the Toom-4 evaluations at 1 and -1 as m = k + 1 limbs each: p1 = x(1), pm1 = |x(-1)|. `w` is m limbs
	// of temporary space. Returns 1 when x(-1) is negative.
	static unsigned k_toom4_eval1(digit* p1, digit* pm1, digit* w, const digit* x, std::size_t k, std::size_t x3n);

	// The evaluations at 2 and -2 in the same form: x(+-2) = (x0 + 4 x2) +- (2 x1 + 8 x3). Each part is
	// below 10 B^k, so it fits m limbs.
	static unsigned k_toom4_eval2(digit* p2, digit* pm2, digit* w, const digit* x, std::size_t k, std::size_t x3n);

	// The evaluation at 1/2 scaled to an integer, 8 x(1/2) = 8 x0 + 4 x1 + 2 x2 + x3 (below 15 B^k), as m limbs.
	static void k_toom4_evalh(digit* ph, const digit* x, std::size_t k, std::size_t x3n);

	// Toom-4 interpolation for a product c0 + c1 B^k + ... + c6 B^6k, from the values at 0 and infinity
	// (c0 and c6) and at 1, -1, 2, -2 and 1/2. The values at p and -p give the even and odd parts of the
	// product at p, as in Toom-Cook with symmetric points (Knuth, TAOCP vol. 2, 4.3.3; Bodrato and
	// Zanoni, ISSAC 2007; Bodrato, WAIFI 2007). On entry r[0..2k) holds c0 and r[6k..6k + vn) holds c6,
	// with r[2k..6k) zero; v1, vm1, v2, vm2 and vh are the N = 2k + 2 limb products at 1, -1, 2, -2 and
	// 1/2 (the last scaled by 2^6), vm1 and vm2 meant negative when m1neg and m2neg are set; x is N limbs
	// of temporary space. All five are consumed: c1 to c5 are added into r (of rn limbs) at k to 5k.
	// The steps run on values mod B^N, so the one that can be negative (c1 - c5) needs no sign branch;
	// the divisions are exact, and the halvings and shifts only act on non-negative values.
	static void k_toom4_interp(digit* r, std::size_t rn, std::size_t k, std::size_t vn, digit* v1, digit* vm1, digit* v2, digit* vm2, digit* vh, digit* x, bool m1neg, bool m2neg);

	// r[0..an+bn) = a * b by Toom-4, for an >= bn > 3k with k = ceil(an / 4): both operands split into
	// four pieces, the product read off seven products at 0, 1, -1, 2, -2, 1/2 and infinity.
	static void k_mul_toom4(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s);

	// r[0..2n) = a^2 by Karatsuba: with a = a0 + a1 B^h, the middle term 2 a0 a1 is
	// a0^2 + a1^2 - (a0 - a1)^2, so the third square is of h limbs and has no carry limb.
	static void k_sqr_karatsuba(digit* r, const digit* a, std::size_t n, digit* s);

	// r[0..2n) = a^2 for n >= 1; r must not overlap a. `s` is scratch of k_sqr_scratch_size(n) limbs.
	static void k_sqr(digit* r, const digit* a, std::size_t n, digit* s);

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
	static constexpr ntt_word ntt_prime(unsigned i);

	// A primitive 3 2^53-th root of unity modulo ntt_prime(i): x^(c / 3) for the least x that is neither a
	// square nor a cube (7, 11 and 7) and c = (p - 1) / 2^53.
	static constexpr ntt_word ntt_root(unsigned i);

	// The cutoff in words under which a block is transformed layer by layer inside the cache; larger
	// blocks split into halves after one pass over them.
#ifdef UINTEGER_T_NTT_LEAF
	static constexpr std::size_t ntt_leaf = UINTEGER_T_NTT_LEAF;
#else
	static constexpr std::size_t ntt_leaf = 2048;
#endif
	static_assert(ntt_leaf >= 4 && (ntt_leaf & (ntt_leaf - 1)) == 0, "the transform leaf size is a power of two of at least 4 words");

	// 64 x 64 -> 128 bits, independent of the limb width (the transform words are 64 bits at every width).
	static ntt_word _mul64(ntt_word x, ntt_word y, ntt_word* lo);

	static ntt_word _mulhi64(ntt_word x, ntt_word y);

	// (hi, lo) / d for hi < d: the quotient through *q, the remainder returned.
	static ntt_word _div64(ntt_word hi, ntt_word lo, ntt_word d, ntt_word* q);

	// A prime and what its arithmetic needs.
	struct ntt_mod {
		ntt_word p;      // the prime
		ntt_word p2;     // 2p
		ntt_word mu;     // floor(2^64 / p), reduces a word to [0, 2p)
		ntt_word pinv;   // p^-1 mod 2^64, for Montgomery reduction
		ntt_word v;      // reciprocal of p << 2 (Moller and Granlund), for Shoup quotients
		ntt_word r;      // 2^64 mod p
	};

	static ntt_mod ntt_make_mod(unsigned i);

	static ntt_word ntt_sub_if(ntt_word x, ntt_word m);

	// floor(w 2^64 / p) for w < p, Shoup's quotient of w: a 2-by-1 division of w 2^64 by the normalized
	// d = p << 2 with its reciprocal (Moller and Granlund, "Improved division by invariant integers",
	// IEEE Trans. Computers 60(2), 2011, Algorithm 4, with the low dividend word zero).
	static ntt_word ntt_quot(const ntt_mod& m, ntt_word w);

	// x w mod p in [0, 2p), for any word x and w < p with its quotient wq.
	static ntt_word ntt_mulq(ntt_word x, ntt_word w, ntt_word wq, ntt_word p);

	// x y mod p in [0, p) for x < 2^64 and y < p (the slow way, for constants).
	static ntt_word ntt_mulmod(const ntt_mod& m, ntt_word x, ntt_word y);

	// 2^64 / n mod p for n = 2^lg, or 3 2^lg when `three`: the factor that turns a Montgomery product into
	// a product scaled by 1/n.
	static ntt_word ntt_unscale(const ntt_mod& m, unsigned lg, bool three);

	// x / 2 mod p for x < 2p, below 1.5p.
	static ntt_word ntt_half(ntt_word x, ntt_word p);

	// The powers of the primitive n-th root w: tw[2t] = w^t and tw[2t + 1] its Shoup quotient, for t = 0..last
	// (w^(n/2) = -1 among them). The block of powers k..2k - 1 is the block 0..k - 1 times w^k, which keeps
	// the products independent of one another.
	static void ntt_table(ntt_word* tw, std::size_t last, const ntt_mod& m, ntt_word w);

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
	static void ntt_fwd_layer(ntt_word* x, std::size_t h, const ntt_ctx& c, std::size_t step);

	// One layer of the inverse transform: (u, v) -> (u - v w~, u + v w~) with w~ = w^(h - t) = -w^-t, so
	// the inverse twiddles come out of the same table, read backwards.
	static void ntt_inv_layer(ntt_word* x, std::size_t h, const ntt_ctx& c, std::size_t step);

	// The last two forward layers (blocks of 4: the twiddles are 1 and w4, the 4th root of unity).
	static void ntt_fwd_last(ntt_word* x, std::size_t n, const ntt_ctx& c);

	// The first two inverse layers, undoing ntt_fwd_last.
	static void ntt_inv_first(ntt_word* x, std::size_t n, const ntt_ctx& c);

	// Forward transform of the n <= ntt_leaf words at x (n a power of two, at most the table's length):
	// layer by layer, the whole block staying in the cache.
	static void ntt_fwd_leaf(ntt_word* x, std::size_t n, const ntt_ctx& c);

	static void ntt_inv_leaf(ntt_word* x, std::size_t n, const ntt_ctx& c);

	// Natural order in, bit-reversed order out: x(w^t) for t in bit-reversed order, for the n words at x.
	static void ntt_fwd(ntt_word* x, std::size_t n, const ntt_ctx& c);

	// Bit-reversed order in, natural order out, scaled by n.
	static void ntt_inv(ntt_word* x, std::size_t n, const ntt_ctx& c);

	// The radix-3 layer for a length 3M, M = c.n / 3 (a multiple of 4): (a, b, d) = x[t], x[t + M], x[t + 2M]
	// become a + b + d, (a + z b + z^2 d) w^t and (a + z^2 b + z d) w^2t, with z = w^M the cube root of unity.
	// With 1 + z + z^2 = 0 the two sums are u +- s (b - d) for u = a - (b + d) / 2 and s = (z - z^2) / 2, so
	// the layer takes three products per three words.
	static void ntt_fwd3_layer(ntt_word* x, const ntt_ctx& c);

	// The inverse of ntt_fwd3_layer, scaled by 3: the twists by w^-t and w^-2t first, which are -w^(n/2 - t)
	// and -w^(n/2 - 2t) (w^(n - 2t) once 2t passes n/2); the negated twiddle (p - w) has the quotient ~q.
	static void ntt_inv3_layer(ntt_word* x, const ntt_ctx& c);

	// The whole transform of c.n words at x, whatever the length: bit-reversed order within each of the
	// three blocks after the radix-3 layer of a length 3M.
	static void ntt_forward(ntt_word* x, const ntt_ctx& c);

	// The inverse, scaled by c.n.
	static void ntt_inverse(ntt_word* x, const ntt_ctx& c);

	// Words (64 / digit_bits limbs each) that hold `n` limbs.
	static constexpr std::size_t ntt_limbs_per_word = 64 / digit_bits;

	static std::size_t ntt_words(std::size_t n);

	// The transform length for `cw` coefficients: the least of 2^k and 3 2^k (k >= 2) not below cw, and 0
	// when it would pass 2^53.
	static unsigned long long ntt_length(unsigned long long cw);

	// Words of the twiddle table for a transform of length n: n / 2 + 1 entries, or 2n / 3 + 1 for 3 2^k.
	static unsigned long long ntt_table_words(unsigned long long n);

	// Plans the transform for an `an` x `bn` limb product (a square when `sqr`): its length `len` in words
	// (see ntt_length) and the `scratch` it needs in limbs (3 or 4 arrays of the length, the second
	// operand's transform unless squaring, the twiddle table and a word to align by). False when it
	// cannot be used: the length is beyond the roots of unity, or the scratch is more than a quarter of
	// what Size can address in bytes, which also keeps every sum of scratch sizes in the callers from
	// wrapping; the caller then takes Toom-4. Production code uses std::size_t; tests pass narrower sizes.
	template <typename Size>
	static bool _ntt_plan(Size an, Size bn, bool sqr, Size* len, Size* scratch);

	static std::size_t k_ntt_scratch_size(std::size_t an, std::size_t bn, bool sqr);

	// The scratch as words: aligned up to 8 bytes (the plan reserves the word).
	static ntt_word* ntt_align(digit* s);

	// The words of a (the limbs packed little-endian), each reduced to [0, 2p), then zeros up to n.
	static void ntt_load(ntt_word* x, std::size_t n, const digit* a, std::size_t an, const ntt_mod& m);

	// x = x y / n mod p, pointwise, in [0, p): x is scaled by sc = 2^64 / n mod p (ntt_unscale) with a Shoup
	// product (the Montgomery factor and the inverse length at once), then Montgomery-reduced against y.
	static void ntt_pointwise(ntt_word* x, const ntt_word* y, std::size_t n, const ntt_mod& m, ntt_word sc);

	// Stores word `w` as the limbs at word index k of r[0..rn); limbs past rn must be zero.
	static void ntt_put(digit* r, std::size_t rn, std::size_t k, ntt_word w);

	// Constants of the Chinese remainder step: p0^-1 mod p1, p0 mod p2, (p0 p1)^-1 mod p2, and p0 p1 as two words.
	static constexpr ntt_word ntt_crt_inv01 = 0x34fcccccccccccbeull;
	static constexpr ntt_word ntt_crt_p0_mod_p2 = 0x0540000000000000ull;
	static constexpr ntt_word ntt_crt_inv012 = 0x1790924924924ac7ull;
	static constexpr ntt_word ntt_crt_p01_hi = 0x0e670c0000000000ull;
	static constexpr ntt_word ntt_crt_p01_lo = 0x7980000000000001ull;

	// Garner's form of the Chinese remainder theorem for the three residue arrays (each in [0, 2p)), the
	// coefficients x = x0 + p0 t1 + p0 p1 t2 below p0 p1 p2 (192 bits), summed into r[0..rn) at 64-bit word
	// positions with the carries kept in two words.
	static void ntt_crt(digit* r, std::size_t rn, const ntt_word* const res[3], std::size_t cw, const ntt_mod mod[3]);

	// r[0..an+bn) = a * b, or a^2 when `sqr` (b is then ignored), by the transform; r must not overlap
	// the operands. `s` is scratch of k_ntt_scratch_size limbs; the kernel does not allocate.
	static void k_ntt_mul(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, bool sqr, digit* s);

	// r[0..an+bn) = a * b by Karatsuba, for an >= bn >= 2 and an < 2 bn: split at h = ceil(an / 2).
	static void k_mul_karatsuba(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s);

	// r[0..an+bn) = a * b for any sizes (an, bn >= 1); r must not overlap a or b.
	// `s` is scratch of k_mul_scratch_size(an, bn) limbs.
	static void k_mul(digit* r, const digit* a, std::size_t an, const digit* b, std::size_t bn, digit* s);

	// Division by invariant integers follows Moller and Granlund, "Improved division by
	// invariant integers" (IEEE Trans. Computers, 2011): reciprocal of a normalized limb,
	// of a normalized two-limb divisor, and the 3-by-2 division step.

	// Reciprocal of a normalized limb: floor((B^2 - 1) / d) - B.
	static digit k_inv_1(digit d);

	// Reciprocal of a normalized two-limb divisor: floor((B^3 - 1) / (d1 B + d0)) - B.
	static digit k_inv_3by2(digit d1, digit d0);

	// Divides (n2,n1,n0) by the normalized (d1,d0) given v = k_inv_3by2(d1, d0), where
	// (n2,n1) < (d1,d0). Returns the quotient limb and the two-limb remainder.
	static digit k_div_3by2(digit n2, digit n1, digit n0, digit d1, digit d0, digit v, digit* rem1, digit* rem0);

	// One quotient digit of schoolbook division: divides the window w[0..dn] (below dp * B) by the
	// normalized dp[0..dn) (dn >= 2), leaves the remainder in w[0..dn) and returns the digit.
	static digit k_div_sb_digit(digit* w, const digit* dp, std::size_t dn, digit d1, digit d0, digit dinv);

	// Schoolbook division of np[0..nn) by the normalized dp[0..dn) (dn >= 2, top bit set).
	// Requires the top dn limbs of np to be below dp; writes nn-dn quotient limbs to q
	// and leaves the remainder in np[0..dn).
	static void k_div_sb(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn);

	// Quotient-only schoolbook division, on the contract of k_div_sb without the remainder. The
	// digit that is `i` places above the last one subtracts its product only over the top
	// min(dn-2, i + G) limbs below the 3-by-2 window (G = div_trunc_window): the limbs under them
	// stay stale, since the digits that follow read nothing below limb dn-2-G. That is about
	// qn^2/2 limb products where k_div_sb takes qn * dn, and it makes the window every later
	// 3-by-2 step reads inexact. A digit decided on an inexact window is right unless its
	// remainder lies within the error of a decision edge. The kernel detects those digits (flag 1:
	// the window's top limbs equal the divisor's; flag 2: the remainder's top two limbs are zero)
	// and repairs the state: it subtracts the products the truncated digits left out, which makes
	// the limbs the exact remainder after the flagged digit, and finishes the division with the exact
	// digit step. The return value is 0 when no digit was flagged (np[0..dn) is not a remainder
	// then), the flag (1 or 2) when the repair ran (the quotient is exact and np[0..dn) is the
	// remainder), and 3 when the repair's own checks failed, which the proof below excludes: np and q
	// are then clobbered, and the caller redoes the division with the exact kernels on a fresh copy of
	// the dividend. `at`, when given, receives the flagged digit.
	//
	// The bound (ARCHITECTURE.md has the outline). B is the limb base,
	// D the divisor, D' its top two limbs, and qn < B^(G-1) (a longer quotient takes the exact
	// kernel). A digit m below dn-2-G leaves the low k = dn-2-m-G limbs of its product out: an
	// error q_m (D mod B^k) B^m < B^(dn-1-G), so at digit i the window W is the true one plus
	// delta <= qn B^(dn-1-G-i) + 1. With q^ the estimate (floor(W / D) or one more) and
	// Y = W - q^ (D - D mod B^k), the true remainder is Y - u, u = q^ (D mod B^k) < B^(dn-1-G-i),
	// so the computed digit q^ - [Y < 0] differs from the true one only for 0 <= Y < u + delta,
	// and u + delta < B^(dn-2). Y < B^(dn-2) means its top two limbs are zero: flag 2. A step
	// without a borrow leaves Y below D' B^(dn-2), and one with a borrow leaves it below D, so the
	// next window's top limbs are at most those of D: only their equality (the case k_div_sb
	// handles apart) can break the 3-by-2 step, flag 1. Digits with i >= dn-2-G subtract the
	// whole product, are exact, and run k_div_sb_digit unchanged.
	//
	// The repair. Up to a flag every digit recorded is the true one, so the limbs hold the exact
	// state plus e, the sum of q_m (D mod B^k_m) B^m over the truncated digits m applied so far, and
	// nothing else (every limb below the lowest digit applied, and every limb the truncated digits
	// skipped, is a dividend limb). Subtracting those terms leaves the exact state: the limbs below
	// the state's top are then the remainder after the last digit applied, mod B^top. At flag 1 that
	// is the remainder after digit i+1, nonnegative. At flag 2 digit i is applied with q^ (the true
	// digit is q^ or q^ - 1), and the remainder after it lies in [-D, D); it is negative exactly
	// when a subtraction wrapped (the integer is limbs minus one wrap times B^top, and at most one
	// wrap happens because the integer exceeds -B^top), and then D is added back and q^ lowered.
	// The exact digit step finishes digits i (flag 1) or i - 1 (flag 2) down to 0.
	static constexpr std::size_t div_trunc_window = 2;

	// The longest quotient the bound covers: B - 1 limbs, the largest digit (`~digit(0)` would
	// promote a 16-bit limb to a negative int). No size reaches it at 32- and 64-bit limbs; a test
	// lowers it to check the guard there.
#ifdef UINTEGER_T_DIV_TRUNC_MAX_QUOTIENT
	static constexpr std::size_t div_trunc_max_quotient = UINTEGER_T_DIV_TRUNC_MAX_QUOTIENT;
#else
	static constexpr std::size_t div_trunc_max_quotient = static_cast<std::size_t>(std::numeric_limits<digit>::max());
#endif

	static unsigned k_div_sb_trunc(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn, std::size_t* at = nullptr);

	static int k_cmp_n(const digit* a, const digit* b, std::size_t n);

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

	static std::size_t k_div_dc_bal_scratch_size(std::size_t n);

	// The product buffer is free again by the time the recursion returns, so it shares the scratch.
	static std::size_t k_div_dc_step_scratch_size(std::size_t dn, std::size_t c);

	// Scratch limbs k_div_dc needs for an nn-limb dividend and a dn-limb divisor.
	static std::size_t k_div_dc_scratch_size(std::size_t nn, std::size_t dn);

	// Divides a[0..dn+c) by the normalized b[0..dn), 1 <= c < dn, where a < b * B^c. Writes c
	// quotient limbs to q and leaves the remainder in a[0..dn); a[dn..dn+c) is clobbered.
	static void k_div_dc_step(digit* q, digit* a, std::size_t dn, std::size_t c, const digit* b, digit* s);

	// Divides a[0..2n) by the normalized b[0..n), where the top n limbs of a are below b. Writes
	// n quotient limbs to q and leaves the remainder in a[0..n); a[n..2n) is clobbered.
	static void k_div_dc_bal(digit* q, digit* a, std::size_t n, const digit* b, digit* s);

	// Divides np[0..nn) by the normalized dp[0..dn) like k_div_sb, but subquadratically: the
	// quotient is cut into dn-limb blocks from the top (the partial block first), each one a
	// balanced or short division. `s` is scratch of k_div_dc_scratch_size(nn, dn) limbs.
	static void k_div_dc(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn, digit* s);

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
	static std::size_t k_div_newton_block(std::size_t m, std::size_t dn);

	// Scratch limbs of k_div_newton_inv for an n-limb divisor.
	static std::size_t k_div_newton_inv_scratch_size(std::size_t n);

	// Plans the Newton division of an nn-limb dividend by a dn-limb divisor: the `scratch` limbs it needs
	// (the reciprocal, then the larger of its own scratch and a block's: the estimate's c-by-c product and
	// the remainder's c-by-dn product with their multiplication scratch). False when the sizes are too
	// large to add safely: the scratch is more than a quarter of what Size can address in bytes, or the
	// dividend more than a sixty-fourth of it in limbs, which also keeps every sum of sizes in the
	// callers from wrapping; k_div_dc then takes the recursive division. Production code uses
	// std::size_t; tests pass narrower sizes.
	template <typename Size>
	static bool _div_newton_plan(Size nn, Size dn, Size* scratch);

	static bool k_div_uses_newton(std::size_t nn, std::size_t dn);

	static std::size_t k_div_newton_scratch_size(std::size_t nn, std::size_t dn);

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
	static void k_div_newton_inv(digit* w, const digit* t, std::size_t n, digit* s);

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
	static k_div_corrections k_div_newton_step(digit* q, digit* a, std::size_t dn, std::size_t c, const digit* b, const digit* w, digit* s);

	// Divides np[0..nn) by the normalized dp[0..dn) like k_div_dc, with one reciprocal of the top c
	// limbs of the divisor (c = k_div_newton_block) and the quotient cut into blocks of c limbs from
	// the top, the first of m % c limbs using the top limbs of the reciprocal. `s` is scratch of
	// k_div_newton_scratch_size(nn, dn) limbs.
	static void k_div_newton(digit* q, digit* np, std::size_t nn, const digit* dp, std::size_t dn, digit* s);

	// Helper functions

	void trim(digit mask = 0);

	// Sizes the value to `n` limbs without initializing new ones: the caller writes every limb
	// it adds (and no operand limb is read from the new range) before anything reads it.
	void resize_uninit(std::size_t n);

	static constexpr char chr(int ord);

	static constexpr int ord(int chr);

public:
	// The number of bits in a digit of `base` when `base` is a power of two from 2 to 256 (1 to 8),
	// else 0; `base` must be in [1, 256]. These bases convert by slicing bits. base-x sizes the
	// blocks of its padded alphabets with it.
	static constexpr unsigned base_bits(int base);

	// The number of digits of `base` that write the largest 64-bit value (64 for base 2, 20 for base
	// 10, 8 for base 256), a table over [1, 256] (0 for base 1). `digits()` sizes its output with it.
	static constexpr unsigned base_size(int base);

	static const uinteger_t& uint_0();

	static const uinteger_t& uint_1();

UINTEGER_T_PRIVATE:
	// Operator implementations
	static uinteger_t& bitwise_and(uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& bitwise_and(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t bitwise_and(const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& bitwise_or(uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& bitwise_or(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs);
	static uinteger_t bitwise_or(const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& bitwise_xor(uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& bitwise_xor(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t bitwise_xor(const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& bitwise_inv(uinteger_t& lhs);

	static uinteger_t& bitwise_inv(uinteger_t& result, const uinteger_t& lhs);

	static uinteger_t bitwise_inv(const uinteger_t& lhs);

	// Shifts by `n` bits. The limb count is `n / digit_bits` and the bit count `n % digit_bits`
	// (digit_bits is a power of two), so there is no division and no temporary. `result` may be
	// `lhs`: the kernels move limbs in the direction that reads each one before it is overwritten.
	static uinteger_t& lshift(uinteger_t& result, const uinteger_t& lhs, unsigned long long n);

	// Shifts right by `n` bits, discarding the bits shifted out.
	static uinteger_t& rshift(uinteger_t& result, const uinteger_t& lhs, unsigned long long n);

	// The count held in a uinteger_t, when it fits 64 bits (up to 64 / digit_bits limbs). A longer
	// count is 2^64 bits or more, beyond any size a value can have: a left shift of a nonzero
	// value by it cannot be represented and a right shift gives zero.
	static bool shift_count(const uinteger_t& rhs, unsigned long long& n);

	static uinteger_t& bitwise_lshift(uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& bitwise_lshift(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t bitwise_lshift(const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& bitwise_rshift(uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& bitwise_rshift(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t bitwise_rshift(const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& long_add(uinteger_t& lhs, const uinteger_t& rhs);

	// result = lhs + rhs; `result` may be `lhs` or `rhs`.
	static uinteger_t& long_add(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& add(uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& add(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t add(const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& long_sub(uinteger_t& lhs, const uinteger_t& rhs);

	// result = lhs - rhs, wrapping modulo B^max(size) like fixed-width unsigned arithmetic;
	// `result` may be `lhs` or `rhs`.
	static uinteger_t& long_sub(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& sub(uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& sub(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t sub(const uinteger_t& lhs, const uinteger_t& rhs);

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
	static uinteger_t& long_mult(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& mult(uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t& mult(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs);

	static uinteger_t mult(const uinteger_t& lhs, const uinteger_t& rhs);

	// Sets this value to a single limb (zero is the empty value).
	void set_digit(digit v);

	// Single word division. Either output may be null when it is not wanted; an output may be `lhs`.
	static void single_divmod(uinteger_t* quotient, uinteger_t* remainder, const uinteger_t& lhs, const uinteger_t& rhs);


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

	static bool k_div_uses_trunc(std::size_t m, std::size_t dn);

	// Knuth's Algorithm D on raw limbs, with a reciprocal-based quotient digit estimate. Either
	// output may be null when it is not wanted; an output may be `lhs` or `rhs`, which are copied
	// before any output is written.
	static void knuth_divmod(uinteger_t* quotient, uinteger_t* remainder, const uinteger_t& lhs, const uinteger_t& rhs);

	// Quotient and remainder of lhs / rhs; either output may be null when it is not wanted, so
	// `a / b` and `a % b` build one result. An output may be `lhs` or `rhs`.
	static void divide(uinteger_t* quotient, uinteger_t* remainder, const uinteger_t& lhs, const uinteger_t& rhs);

	static std::pair<uinteger_t, uinteger_t> divmod(const uinteger_t& lhs, const uinteger_t& rhs);

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
	digit divmod_small(const small_divisor& dv);

	digit divmod_small(digit divisor);

	// The remainder of a[0..n) divided by the divisor, without storing the quotient.
	static digit k_mod_1(const digit* a, std::size_t n, const small_divisor& dv);

	// Sets this value to `this * mult + add` in place.
	uinteger_t& mul_add_small(digit mult, digit add);

	// Largest power of `base` that fits in a digit, and its exponent.
	static digit base_chunk(int base, unsigned& count);

	// Size arithmetic for the bit-sliced conversions. A count that does not fit in `Size`
	// throws std::length_error instead of wrapping (a 32-bit size_t reaches it at 512 MiB
	// of base-256 digits). Production code uses std::size_t; tests pass narrower sizes.

	// Limbs that hold `n` digits of `bits` bits each.
	template <typename Size>
	static Size _limbs_for_digits(Size n, unsigned bits);

	// Digits of `bits` bits each in a value of `n` >= 1 limbs whose top limb has `top`
	// significant bits.
	template <typename Size>
	static Size _digits_for_limbs(Size n, unsigned top, unsigned bits);

	// Conversion kernels for bases that are not powers of two.
	//
	// Encoding writes the value as K chunks of k digits, one per power of D = base^k (the
	// largest power below the limb size B), and splits each chunk into digits. The chunks
	// come from a fixed-point fraction, not from divisions: x / D^K is held in L = K + g limbs
	// and each pass multiplies it by D, so the carry out of a pass is the next chunk, most
	// significant first (Bouvier and Zimmermann, "Division-Free Binary-to-Decimal Conversion",
	// IEEE Trans. Computers 63(8), 2014, Algorithm 2). Their initial division is a multiply
	// by R = floor(B^(L+n) / D^K), built once per base and size with one division and
	// cached: the fraction starts at (x + 1/2) / D^K, rounded down by at most
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
	// Apple's arm64 cores keep the encoding kernel ahead of the tree to about 13000 to 20000 bits,
	// the Intel i9 to about 8700 to 11000 (docs/design/base-conversion.md, The tree).
#ifndef UINTEGER_T_RADIX_MAX_BITS
#  if defined __APPLE__ && (defined __aarch64__ || defined __arm64__)
#    define UINTEGER_T_RADIX_MAX_BITS 14336
#  else
#    define UINTEGER_T_RADIX_MAX_BITS 8192
#  endif
#endif
	static constexpr std::size_t radix_max_bits = UINTEGER_T_RADIX_MAX_BITS;
	static constexpr std::size_t radix_max_limbs = radix_max_bits / digit_bits;
	// Scratch limbs of the general kernel: n + L + 8 with L at most radix_max_bits / (digit_bits - 8) + 2.
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

	static std::uint64_t _umulh(std::uint64_t x, std::uint64_t y);

	// floor(2^e / d) as (hi, lo) and the remainder, by shift and subtract (table building only).
	static std::uint64_t _pow2_div(unsigned e, std::uint64_t d, std::uint64_t& hi, std::uint64_t& lo);

	static unsigned _bit_length(std::uint64_t v);

	// The multiplier for dividends below 2^nd by d (not a power of two, d < 2^32).
	static radix_div _make_div(std::uint64_t d, unsigned nd);

	static std::uint64_t _div_magic(const radix_div& v, std::uint64_t r);

	static radix* _make_radix(int base);

	// R for values below 2^bits (n limbs): floor(B^(L + n) / D^K), one division by the power that
	// sets K (K single-limb divisions by D give the same value, as floor(floor(a / D) / D) is
	// floor(a / D^2), in O(n^2)). `guard` and `half` override the computed guard limbs and drop
	// the 1/2 offset; tests use them to show the edge inputs catch an under-sized table.
	static radix_table* _make_radix_table(const radix& rx, std::size_t n, std::size_t bits, int guard = -1, bool half = true);

	// Tables and contexts are built on first use and published with a compare-and-swap, so
	// concurrent first calls are safe; the loser deletes its copy. They live until exit.
	template <typename T>
	static const T* _publish(std::atomic<const T*>& slot, T* fresh);

	static const radix& _radix_for(int base);

	// The table for a value of n limbs and `bits` bits.
	static const radix_table& _table_for(const radix& rx, std::size_t n, std::size_t bits);

	// The m digits of a piece v < base^m (base^m < 2^32), most significant first. f = v *
	// ceil(2^64 / base^m) is the fraction v / base^m to within base^-m, so each umulh(f, base)
	// is an exact digit (Lemire, Kaser and Kurz, "Faster remainder by direct computation",
	// 2019); two digits per step through the pair table when there is one.
	static void _emit_digits(const radix& rx, std::uint64_t v, unsigned m, unsigned char* out);

	// The k digits of a chunk r < D: pieces of h digits (at most three, k <= 2h + 1).
	static void _split_chunk_general(const radix& rx, digit r, unsigned char* out);

	// The same for one shape (K digits in pieces of H, K > H, K <= 2H + 1) fixed at compile
	// time: the digit counts are constants, so each piece unrolls into a straight run of
	// multiplies and table loads with no loop or count test left to resolve at run time.
	template <unsigned M>
	static void _emit_digits_n(const radix& rx, std::uint64_t v, unsigned char* out);

	template <unsigned K, unsigned H>
	static void _split_chunk_kh(const radix& rx, digit r, unsigned char* out);

#ifndef UINTEGER_T_SPLIT_SHAPES
#define UINTEGER_T_SPLIT_SHAPES 1
#endif
	// The splitter for chunks of k digits in pieces of h: one of the shapes that the bases 3 to 256
	// (not powers of two) take at 64-bit limbs, else the general one.
	static void (*_pick_split(unsigned k, unsigned h))(const radix&, digit, unsigned char*);

	static void _split_chunk(const radix& rx, digit r, unsigned char* out);

	static std::size_t _strip_zeros(unsigned char* out, std::size_t total);


	// N limbs, K chunks, G guard limbs and R of L + X limbs, all fixed: the fraction lives in
	// registers. The short product keeps only the partial products that reach limb N - 1.
	template <int N, int K, int G, int X>
	static std::size_t _enc_fixed(const radix& rx, const radix_table& t, const digit* x, unsigned char* out);


	template <int N, int E>
	static std::size_t _enc_fixed_k(const radix& rx, const radix_table& t, const digit* x, unsigned char* out);

	template <int N>
	static std::size_t _enc_fixed_n(const radix& rx, const radix_table& t, const digit* x, unsigned char* out);

	// Any size up to radix_max_limbs: a four-row short product (row r lagged by r limbs so the
	// four rows add into the same limb) and four fused passes per sweep.
	static std::size_t _enc_general(const radix& rx, const radix_table& t, const digit* x, std::size_t n, unsigned char* out);

	// The digits of the n >= 1 limbs of x (top limb nonzero), written to out[0..K * k) and
	// stripped of leading zeros; returns the count.
	static std::size_t _encode_radix(const radix& rx, const radix_table& t, const digit* x, std::size_t n, unsigned char* out);

	// The digit_octets bytes at p, first byte most significant, as a limb.
	static digit _load_be(const unsigned char* p);

	// Writes a limb to the digit_octets bytes at p, most significant byte first.
	static void _store_be(unsigned char* p, digit v);

	// Writes the `count` low-order bytes of the n >= 1 limbs at p to dst, most significant first;
	// count is (n - 1) * digit_octets plus the bytes of the top limb, which is nonzero.
	static void _store_bytes(unsigned char* dst, const digit* p, std::size_t n, std::size_t count);

	// Appends the digits of the value to `out`: one-byte results are written in place.
	template <typename Result, typename Fill>
	static void _digits_append(Result& out, std::size_t cap, Fill fill, std::true_type);

	template <typename Result, typename Fill>
	static void _digits_append(Result& out, std::size_t cap, Fill fill, std::false_type);

	template <typename Result>
	static void _digits_radix(Result& out, int base, const digit* p, std::size_t n);

	// Reads eight digit bytes (first byte most significant) as one number below base^8 with
	// three multiplies; the digits are below `base`, so no lane overflows.
	static std::uint64_t _swar8(const unsigned char* p, std::uint64_t b, std::uint64_t b2, std::uint64_t b4);

	// One chunk of `count` digits as a number below base^count.
	static std::uint64_t _chunk_value(const radix& rx, const unsigned char* p, unsigned count, std::uint64_t b8);

	// Writes the value of m >= 1 digits d[0..m), each below `base`, most significant first, to
	// x and returns its limb count. x holds chunks = ceil(m / k) limbs and nothing beyond
	// them is touched, which is what lets a short value live on the stack (L1). The chunks go
	// into x itself, and Horner step j reads chunk j and leaves at most j limbs below it, so
	// nothing unread is overwritten: the value of the first j chunks is below D^j < B^j, so
	// n <= j on entry to step j; the first steps write x[n] <= x[j]; a four-chunk step at j
	// (four chunks remain, j + 3 < chunks) reads x[j .. j + 3] before it writes x[n .. n + 3]
	// with n + 3 <= j + 3. The result has n <= chunks limbs.
	static std::size_t _horner_limbs(const radix& rx, const unsigned char* d, std::size_t m, digit* x);

	// Up to this many chunks (a 64-byte key in base 58 is nine) the decoder works in a stack
	// buffer and sizes the result to the limbs the value has, so a short value is never
	// allocated larger than itself (an inline buffer holds it); longer ones work in the result.
	static constexpr std::size_t horner_stack_chunks = 16;

	// The value of m >= 1 digits d[0..m), each below `base`, most significant first.
	static uinteger_t _from_digit_horner(const unsigned char* d, std::size_t m, int base);

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
	// limbs. The defaults are bit counts over the limb width, from sweeps at both widths on an
	// Apple M4 Pro and M1 Pro and an Intel i9 (docs/design/base-conversion.md, The tree): the
	// encoder's cutoff is its kernel's limit, the decoder's 12288 bits at 64-bit limbs on x86-64
	// and 8192 elsewhere. Leaves of 4096 to 14080 bits are within 3% at most sizes. The tests set
	// tiny limb counts to run the tree at every size.
#ifndef UINTEGER_T_DIGITS_TREE_THRESHOLD
#define UINTEGER_T_DIGITS_TREE_THRESHOLD (UINTEGER_T_RADIX_MAX_BITS / (sizeof(DIGIT_T) * 8))
#endif
#ifndef UINTEGER_T_DIGITS_TREE_LEAF
#define UINTEGER_T_DIGITS_TREE_LEAF (7680 / (sizeof(DIGIT_T) * 8))
#endif
#ifndef UINTEGER_T_FROM_DIGITS_TREE_THRESHOLD
#  if defined __x86_64__ || defined _M_X64
#    define UINTEGER_T_FROM_DIGITS_TREE_THRESHOLD ((sizeof(DIGIT_T) == 8 ? 12288 : 8192) / (sizeof(DIGIT_T) * 8))
#  else
#    define UINTEGER_T_FROM_DIGITS_TREE_THRESHOLD (8192 / (sizeof(DIGIT_T) * 8))
#  endif
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
	static void _tree_shape(const radix& rx, std::size_t chunks, std::size_t leaf_limbs, unsigned& levels, unsigned& leaf_chunks);

	// out = D^e for e >= 1 by squarings and multiplications by D, left to right. `out` and
	// `tmp` each hold the result and two limbs; returns its limb count.
	static std::size_t _tree_pow(digit D, unsigned e, digit* out, digit* tmp);

	// Builds P_0 .. P_(levels-1). With `normalize` each is shifted left to set its top bit,
	// the form k_div_dc divides by, and its shift is kept.
	static void _tree_build(const radix& rx, radix_tree& tr, unsigned levels, unsigned chunks, bool normalize);

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
	static void _tree_leaf(const radix& rx, const radix_tree& tr, const radix_leaf& lf, const digit* x, std::size_t nx, unsigned char* out);

	// The s 2^j digits of x < P_j, zero-padded, written to out. x has nx limbs and room for one
	// more, and is clobbered. Dividing by P_(j-1) (k_div_dc) takes the dividend and divisor
	// shifted left to normalize the divisor; the quotient is below P_(j-1) and the
	// remainder, shifted back, is the low half.
	static void _tree_digits(const radix& rx, const radix_tree& tr, const radix_leaf& lf, unsigned j, digit* x, std::size_t nx, unsigned char* out);

	// Writes the digits of the n >= 1 limbs of x (top limb nonzero) to out[0..s 2^J) and strips
	// the leading zeros; returns the count.
	static std::size_t _tree_encode(const radix& rx, const radix_tree& tr, const radix_leaf& lf, const digit* x, std::size_t n, unsigned char* out);

	// The digits s 2^levels that a tree of leaves of `leaf_digits` digits writes, as a Size: a
	// count that does not fit throws std::length_error, as _limbs_for_digits does (a 32-bit
	// size_t wraps at 2^32 digits). Production uses std::size_t; tests pass narrower sizes.
	template <typename Size>
	static Size _tree_digits_for(std::size_t leaf_digits, unsigned levels);

	// Plans the tree for values of `bits` bits: the powers (normalized for division) and the leaf.
	// The tree covers D^chunks, which must pass 2^bits: `slack` chunks beyond the estimate keep
	// the check below from ever failing; tests pass a negative one to run the retry. The digits
	// the tree writes must fit a Size; that is checked before the powers are allocated.
	template <typename Size = std::size_t>
	static void _tree_plan(const radix& rx, std::size_t bits, std::size_t leaf_limbs, int slack, radix_tree& tr, radix_leaf& lf);

	// Appends the digits of the n >= 1 limbs at p (top limb nonzero), through the tree with
	// leaves of at most `leaf_limbs` limbs (tests pass other sizes than the default). The bit
	// count and the digit capacity are Size arithmetic that throws std::length_error when it
	// does not fit (SR1: a 32-bit size_t wraps the bit count at 512 MiB), before anything is
	// allocated; tests pass a narrower Size to take the guard.
	template <typename Size = std::size_t, typename Result>
	static void _digits_tree(Result& out, int base, const digit* p, std::size_t n, std::size_t leaf_limbs = digits_tree_leaf);

	// Limbs for a value below P_j (the high half times P_(j-1) and the carry's room).
	static std::size_t _tree_limbs(const radix_tree& tr, unsigned j);

	// The value of the m >= 1 digits d[0..m) (m <= s 2^j) into out, which holds _tree_limbs(j)
	// limbs; returns the limb count. The low half is the last s 2^(j-1) digits.
	static std::size_t _tree_from(const radix_tree& tr, int base, unsigned j, const unsigned char* d, std::size_t m, digit* out);

	static uinteger_t _from_digit_tree(const radix& rx, const unsigned char* d, std::size_t m, int base, std::size_t leaf_limbs = from_digits_tree_leaf);

	// The value of m >= 1 digits d[0..m), each below `base`, most significant first.
	static uinteger_t _from_digit_bytes(const unsigned char* d, std::size_t m, int base);

	// Builds a value from `n` digits in `base` (2..256), most significant first;
	// `get(i)` returns digit i and throws when it is not valid.
	template <typename Get>
	static uinteger_t _from_digits(std::size_t n, int base, const Get& get);

UINTEGER_T_PRIVATE:
	// Constructors

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	void _uint_t(const T& value);

	template <typename T, typename... Args, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	void _uint_t(const T& value, Args... args);

public:
	uinteger_t() = default;

	uinteger_t(const uinteger_t& o) = default;

	uinteger_t(uinteger_t&& o) noexcept = default;

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t(const T& value);

	template <typename T, typename... Args, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t(const T& value, Args... args);

	template <typename T, typename... Args, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t(std::initializer_list<T> list);

	template <typename T, std::size_t N>
	explicit uinteger_t(T (&&s)[N], int base=10);

	explicit uinteger_t(const unsigned char* bytes, std::size_t sz, int base);

	explicit uinteger_t(const char* bytes, std::size_t sz, int base);

	template <typename T>
	explicit uinteger_t(const std::vector<T>& bytes, int base=10);

	explicit uinteger_t(const std::string& bytes, int base=10);

	// Assignment Operator
	uinteger_t& operator=(const uinteger_t& o) = default;
	uinteger_t& operator=(uinteger_t&& o) noexcept = default;

	// Typecast Operators
	explicit operator bool() const;
	explicit operator unsigned char() const;
	explicit operator unsigned short() const;
	explicit operator unsigned int() const;
	explicit operator unsigned long() const;
	explicit operator unsigned long long() const;
	explicit operator char() const;
	explicit operator signed char() const;
	explicit operator short() const;
	explicit operator int() const;
	explicit operator long() const;
	explicit operator long long() const;

	// Bitwise Operators
	uinteger_t operator&(const uinteger_t& rhs) const;

	uinteger_t& operator&=(const uinteger_t& rhs);

	uinteger_t operator|(const uinteger_t& rhs) const;

	uinteger_t& operator|=(const uinteger_t& rhs);

	uinteger_t operator^(const uinteger_t& rhs) const;

	uinteger_t& operator^=(const uinteger_t& rhs);

	uinteger_t operator~() const;

	uinteger_t inv();

	// Bit Shift Operators
	uinteger_t operator<<(const uinteger_t& rhs) const;

	uinteger_t& operator<<=(const uinteger_t& rhs);

	uinteger_t operator>>(const uinteger_t& rhs) const;

	uinteger_t& operator>>=(const uinteger_t& rhs);

	// Shifts by a native integer read the count directly (negative counts wrap to huge ones, as
	// converting the count to a uinteger_t does).
	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t operator<<(T rhs) const;

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t& operator<<=(T rhs);

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t operator>>(T rhs) const;

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t& operator>>=(T rhs);

	// Logical Operators
	bool operator!() const;

	bool operator&&(const uinteger_t& rhs) const;

	bool operator||(const uinteger_t& rhs) const;

	// Three-way comparison of two values: -1 when lhs < rhs, 0 when they are equal, 1 when lhs > rhs.
	// The values must be trimmed (no leading zero limb), as every operation leaves them.
	static int compare(const uinteger_t& lhs, const uinteger_t& rhs);

	// Comparison Operators
	bool operator==(const uinteger_t& rhs) const;

	bool operator!=(const uinteger_t& rhs) const;

	bool operator>(const uinteger_t& rhs) const;

	bool operator<(const uinteger_t& rhs) const;

	bool operator>=(const uinteger_t& rhs) const;

	bool operator<=(const uinteger_t& rhs) const;

	// Arithmetic Operators
	uinteger_t operator+(const uinteger_t& rhs) const;

	uinteger_t& operator+=(const uinteger_t& rhs);

	uinteger_t operator-(const uinteger_t& rhs) const;

	uinteger_t& operator-=(const uinteger_t& rhs);

	uinteger_t operator*(const uinteger_t& rhs) const;

	uinteger_t& operator*=(const uinteger_t& rhs);

	// result = lhs * rhs. `result` may be `lhs` or `rhs`. A result that is neither keeps its storage,
	// so one reused with room for the product allocates nothing, unless the product's working space
	// exceeds 256 limbs; an operand taken as the result is rebuilt through a temporary. Returns `result`.
	static uinteger_t& mul(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs);

	// quotient = lhs / rhs and remainder = lhs % rhs in one division. Either output may be `lhs` or
	// `rhs`; `quotient` and `remainder` must be different objects. An output that is not an operand
	// keeps its storage, so outputs reused with room allocate nothing, unless the division's working
	// space exceeds 256 limbs (a dividend above 73 limbs). Throws `std::domain_error` when `rhs` is
	// zero, before it writes an output. Returns references to `quotient` and `remainder`.
	static std::pair<std::reference_wrapper<uinteger_t>, std::reference_wrapper<uinteger_t>> divmod(uinteger_t& quotient, uinteger_t& remainder, const uinteger_t& lhs, const uinteger_t& rhs);

	std::pair<uinteger_t, uinteger_t> divmod(const uinteger_t& rhs) const;

	uinteger_t operator/(const uinteger_t& rhs) const;

	uinteger_t& operator/=(const uinteger_t& rhs);

	uinteger_t operator%(const uinteger_t& rhs) const;

	uinteger_t& operator%=(const uinteger_t& rhs);

	// Increment Operator
	uinteger_t& operator++();
	uinteger_t operator++(int);

	// Decrement Operator
	uinteger_t& operator--();
	uinteger_t operator--(int);

	// Nothing done since promotion doesn't work here
	uinteger_t operator+() const;

	// two's complement
	uinteger_t operator-() const;

	// Get private value at index
	const digit& value(std::size_t idx) const;

	// Get value of bit N
	bool operator[](std::size_t n) const;

	// Get bitsize of value
	std::size_t bits() const;

	// Appends the digits of the value in `base` (2..256) to `out` as byte values
	// 0..base-1, most significant first. Zero gives a single 0 digit.
	template <typename Result, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	void digits(Result& out, int base) const;

	// The inverse of digits(): `n` digits in `base` (2..256), most significant first,
	// one byte each. Throws std::invalid_argument on a digit >= base.
	template <typename T, typename = std::enable_if_t<std::is_integral<T>::value && sizeof(T) == 1>>
	static uinteger_t from_digits(const T* src, std::size_t n, int base);

	// Get string representation of value
	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result str(int alphabet_base = 10) const;

	static uinteger_t strtouint(const void* encoded, std::size_t encoded_size, int alphabet_base);

private:
	// Sets this value, which is zero, to the number in `encoded_size` characters of text (bases 2 to 36) or
	// bytes (base 256, most significant first).
	void _parse(const void* encoded, std::size_t encoded_size, int alphabet_base);

public:
	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result bin() const;

	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result oct() const;

	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result hex() const;

	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result raw() const;
};

#undef UINTEGER_T_PRIVATE

#include "uinteger_t/container.hh"
#include "uinteger_t/word.hh"
#include "uinteger_t/kernels.hh"
#include "uinteger_t/mul.hh"
#include "uinteger_t/mul_toom.hh"
#include "uinteger_t/ntt_transform.hh"
#include "uinteger_t/ntt_mul.hh"
#include "uinteger_t/div_schoolbook.hh"
#include "uinteger_t/div_recursive.hh"
#include "uinteger_t/bitwise.hh"
#include "uinteger_t/arithmetic.hh"
#include "uinteger_t/radix_tables.hh"
#include "uinteger_t/radix_encode.hh"
#include "uinteger_t/radix_decode.hh"
#include "uinteger_t/radix_tree.hh"
#include "uinteger_t/digits.hh"
#include "uinteger_t/operators.hh"

#endif

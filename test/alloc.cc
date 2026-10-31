// Allocation counts of the small-value operations. A result of up to `inline_capacity` limbs lives
// in the object and allocates nothing; a longer one costs one allocation for its limbs and nothing
// else: shifts by a native count build no temporaries, in-place operations on a value with room
// allocate nothing, and `a % b` and `a / b` build only the result they return. The sizes run from
// one limb to past the inline capacity, so both sides of the boundary are counted.
// Replaces the global operator new to count, so it checks the counts, not the timings.
#include <cstdio>
#include <cstdlib>
#include <new>
#include "uinteger_t.hh"

static long allocs = 0;

void* operator new(std::size_t n) {
	++allocs;
	if (void* p = std::malloc(n ? n : 1)) {
		return p;
	}
	throw std::bad_alloc();
}

void operator delete(void* p) noexcept {
	std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
	std::free(p);
}

static int failures = 0;

// Keeps the compiler from eliding the allocation behind a result nobody reads.
static void keep(const uinteger_t& x) {
	const void* p = x.data();
#if defined(__GNUC__)
	asm volatile("" : : "g"(p), "g"(x.size()) : "memory");
#else
	static volatile const void* sink;
	sink = p;
#endif
}

#define COUNT(want, ...) do { \
	const long before = allocs; \
	{ __VA_ARGS__; } \
	const long used = allocs - before; \
	if (used != (want)) { std::printf("FAIL %s:%d: %s: %ld allocations, expected %ld\n", __FILE__, __LINE__, #__VA_ARGS__, used, static_cast<long>(want)); ++failures; } \
} while (0)

// An n-limb value with every limb set, built from bytes at any limb width.
static uinteger_t limbs(std::size_t n, unsigned char byte = 0x5a) {
	return n ? uinteger_t(std::string(n * uinteger_t::digit_octets, static_cast<char>(byte)), 256) : uinteger_t();
}

// The allocations a value of `n` limbs costs: none while it fits the object.
static long heap(std::size_t n) {
	constexpr std::size_t inline_limbs = uinteger_t::container::inline_capacity;
	return n > inline_limbs ? 1 : 0;
}

// An operation whose result is a value: one allocation exactly when the result is too long to be inline.
#define RESULT(...) do { \
	const long want = heap(uinteger_t(__VA_ARGS__).size()); \
	COUNT(want, keep(__VA_ARGS__)); \
} while (0)

int main() {
	constexpr std::size_t inline_limbs = uinteger_t::container::inline_capacity;
	// n = 1..inline_limbs + 2 covers values that fit, fill the buffer, and spill past it; sums,
	// products and shifts of them cross the boundary in the other direction.
	for (std::size_t n = 1; n <= inline_limbs + 2; ++n) {
		const auto a = limbs(n, 0x7f);
		const auto b = limbs(n, 0x35);
		const auto one_limb = limbs(1, 0x11);
		const auto half = limbs((n + 1) / 2, 0x13);
		const auto count = uinteger_t(3);
		const std::size_t wide = uinteger_t::digit_bits + 3; // a limb and a few bits
		uinteger_t c = a;
		uinteger_t room;
		room.reserve(n + 2);

		COUNT(heap(n), uinteger_t x = a; keep(x));
		COUNT(0, uinteger_t x(12345); keep(x));
		COUNT(0, c = a; keep(c));
		COUNT(0, volatile bool r = a < b; (void)r);
		COUNT(0, volatile bool r = a == b; (void)r);
		COUNT(0, volatile bool r = static_cast<bool>(a); (void)r);
		COUNT(0, volatile std::size_t r = a.bits(); (void)r);

		RESULT(a + b);
		RESULT(a - b);
		// A sum that carries out of the top limb is still one allocation at most.
		const auto ones = limbs(n, 0xff);
		const auto one = uinteger_t(1);
		COUNT(heap(n + 1), auto s = ones + one; keep(s); if (s.size() != n + 1) ++failures);
		COUNT(heap(n + 1), auto s = ones + ones; keep(s); if (s.size() != n + 1) ++failures);
		COUNT(heap(n + 1), auto s = one + ones; keep(s); if (s.size() != n + 1) ++failures);
		RESULT(a * b);
		RESULT(a / one_limb);
		RESULT(a % one_limb);
		// Algorithm D keeps its scratch on the stack up to 64 limbs, so the long 16-bit operands are left out.
		if (uinteger_t::digit_bits >= 32 || n <= 4) {
			RESULT(a / half);
			RESULT(a % half);
			COUNT(heap((a / half).size()) + heap((a % half).size()), auto qr = a.divmod(half); keep(qr.first); keep(qr.second));
		}

		// In place with room: no allocation, inline or not.
		room = a;
		room.reserve(n + 2);
		COUNT(0, room += b; keep(room));
		COUNT(0, room -= b; keep(room));
		room = ones;
		COUNT(0, room += one; keep(room)); // the carry limb fits the capacity
		if (room.size() != n + 1) {
			std::printf("FAIL %s:%d: all ones + 1 has %zu limbs, expected %zu\n", __FILE__, __LINE__, room.size(), n + 1);
			++failures;
		}

		// Shifts by a native integer or by a one-limb value allocate only the result.
		RESULT(a << 3);
		RESULT(a << wide);
		RESULT(a >> 3);
		RESULT(a >> wide);
		RESULT(a << count);
		RESULT(a >> count);
		room = a;
		room.reserve(n + 4);
		COUNT(0, room <<= wide; keep(room));
		COUNT(0, room >>= wide; keep(room));
		COUNT(0, room <<= count; keep(room));
		COUNT(0, room >>= count; keep(room));
	}

	// The boundary in both directions. A value grown limb by limb allocates when it passes the
	// buffer and not before; a value shrunk back keeps its block, a copy of it does not need one,
	// and a move of it takes the block instead of allocating.
	{
		uinteger_t x;
		COUNT(0, for (std::size_t i = 0; i < inline_limbs; ++i) x.append(static_cast<uinteger_t::digit>(i + 1)));
		COUNT(1, x.append(static_cast<uinteger_t::digit>(inline_limbs + 1)));
		COUNT(0, x.append(static_cast<uinteger_t::digit>(inline_limbs + 2)));
		COUNT(0, x >>= (inline_limbs + 1) * uinteger_t::digit_bits); // shrinks to one limb, keeps the block
		if (x.size() != 1) {
			std::printf("FAIL %s:%d: shrunk to %zu limbs, expected 1\n", __FILE__, __LINE__, x.size());
			++failures;
		}
		COUNT(0, uinteger_t y = x; keep(y));
		COUNT(0, uinteger_t y = std::move(x); keep(y));
		COUNT(0, volatile bool r = static_cast<bool>(x); (void)r); // moved-from is zero and usable
		COUNT(0, x += uinteger_t(7); keep(x));
	}
	{
		// Copying and moving a long value: a copy allocates, a move does not.
		const auto big = limbs(inline_limbs + 3, 0x6b);
		uinteger_t src = big;
		COUNT(1, uinteger_t y = src; keep(y));
		COUNT(0, uinteger_t y = std::move(src); keep(y));
		uinteger_t dst;
		src = big;
		COUNT(0, dst = std::move(src); keep(dst));
		// Assigning a long value to a short one allocates once; a second assignment reuses the block.
		uinteger_t small(5);
		COUNT(1, small = big; keep(small));
		COUNT(0, small = big; keep(small));
		COUNT(0, small = uinteger_t(9); keep(small));
		// Self-assignment allocates nothing and keeps the value.
		const auto before = small;
		COUNT(0, small = *&small; keep(small));
		if (small != before) {
			std::printf("FAIL %s:%d: self-assignment changed the value\n", __FILE__, __LINE__);
			++failures;
		}
	}
	if (failures) {
		std::printf("alloc: %d failures\n", failures);
		return 1;
	}
	std::printf("alloc OK\n");
	return 0;
}

// Allocation counts of the small-value operations. A result of up to `inline_capacity` limbs lives
// in the object and allocates nothing; a longer one costs one allocation for its limbs and nothing
// else: shifts by a native count build no temporaries, in-place operations on a value with room
// allocate nothing, and `a % b` and `a / b` build only the result they return. The sizes run from
// one limb to past the inline capacity, so both sides of the boundary are counted.
// Replaces the global operator new to count, so it checks the counts, not the timings.
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <new>
#include "uinteger_t.hh"

static long allocs = 0;
static long frees = 0;
static long fail_in = -1;  // the allocation to fail: 0 is the next one, negative is none

void* operator new(std::size_t n) {
	if (fail_in >= 0 && fail_in-- == 0) {
		throw std::bad_alloc();
	}
	++allocs;
	if (void* p = std::malloc(n ? n : 1)) {
		return p;
	}
	throw std::bad_alloc();
}

void operator delete(void* p) noexcept {
	if (p) {
		++frees;
	}
	std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
	if (p) {
		++frees;
	}
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

// Blocks still live: every block a value took is released when it grows, shrinks, moves, is
// assigned over or destroyed, on either side of the boundary.
static long live() { return allocs - frees; }

static void check_no_leaks() {
	constexpr std::size_t inline_limbs = uinteger_t::container::inline_capacity;
	const long before = live();
	{
		for (std::size_t k = 0; k <= inline_limbs + 3; ++k) {
			for (std::size_t m = 0; m <= inline_limbs + 3; m += 1 + m / 3) {
				const auto a = limbs(k, 0x6b);
				const auto b = limbs(m, 0x3d);
				const auto big = limbs(2 * inline_limbs + 5, 0x71);
				uinteger_t x = a;                       // copy construct
				x = b;                                  // copy assign over a value
				x = big;                                // and over one that needs a larger block
				x = a;                                  // and back to a short one, keeping the block
				uinteger_t y = std::move(x);            // move construct
				x = std::move(y);                       // move assign into a moved-from value
				y = big;
				x = std::move(y);                       // move assign over a block
				x += big;                               // growth in place, block to block
				x <<= (inline_limbs + 4) * uinteger_t::digit_bits;
				x >>= (3 * inline_limbs + 9) * uinteger_t::digit_bits;
				x -= x;
				uinteger_t z(a);
				z.append(b);
				z.prepend(b);
				z.reserve(4 * inline_limbs + 7);
				z.resize(inline_limbs + 1);
				z.resize(6 * inline_limbs + 9);
				z.clear();
				z = z + a;
				z = z * b;
				z = (z + big) * (z + big);
				z = (z / (b + uinteger_t(1))) % (a + uinteger_t(7));
				std::swap(x, z);
				uinteger_t w = a * b;
				w = w + w;
			}
		}
	}
	if (live() != before) {
		std::printf("FAIL %s:%d: %ld blocks leaked\n", __FILE__, __LINE__, live() - before);
		++failures;
	}
}

// An allocation that fails leaves the operands as they were and leaks nothing: assignment
// allocates before it releases, growth before it moves.
static bool same(const uinteger_t& x, const uinteger_t& y) {
	return x.size() == y.size() && std::equal(x.data(), x.data() + x.size(), y.data());
}

template <class Op>
static void fails_cleanly(const char* what, const uinteger_t& target, Op op) {
	for (long k = 0; k < 4; ++k) {
		const long before = live();
		{
			uinteger_t x = target;
			const uinteger_t keep_copy = x;
			fail_in = k;
			bool threw = false;
			try {
				op(x);
			} catch (const std::bad_alloc&) {
				threw = true;
			}
			fail_in = -1;
			if (threw && !same(x, keep_copy)) {
				std::printf("FAIL %s:%d: %s with allocation %ld failing changed the value\n", __FILE__, __LINE__, what, k);
				++failures;
			}
			x += uinteger_t(1);  // still a usable value
		}
		if (live() != before) {
			std::printf("FAIL %s:%d: %s with allocation %ld failing leaked %ld blocks\n", __FILE__, __LINE__, what, k, live() - before);
			++failures;
		}
	}
}

static void check_failed_allocations() {
	constexpr std::size_t inline_limbs = uinteger_t::container::inline_capacity;
	const auto small = limbs(1, 0x21);
	const auto full = limbs(inline_limbs, 0x22);
	const auto over = limbs(inline_limbs + 1, 0x23);
	const auto big = limbs(3 * inline_limbs + 5, 0x24);
	const uinteger_t* targets[] = {&small, &full, &over, &big};
	for (const auto* t : targets) {
		fails_cleanly("x = big", *t, [&](uinteger_t& x) { x = big; });
		fails_cleanly("x = over", *t, [&](uinteger_t& x) { x = over; });
		fails_cleanly("x += big", *t, [&](uinteger_t& x) { x += big; });
		fails_cleanly("x *= big", *t, [&](uinteger_t& x) { x *= big; });
		fails_cleanly("x <<= wide", *t, [&](uinteger_t& x) { x <<= 5 * inline_limbs * uinteger_t::digit_bits + 3; });
		fails_cleanly("x.append", *t, [&](uinteger_t& x) { x.append(big); });
		fails_cleanly("x.prepend", *t, [&](uinteger_t& x) { x.prepend(big); });
		fails_cleanly("x.reserve", *t, [&](uinteger_t& x) { x.reserve(x.size() + 3 * inline_limbs + 2); });
		fails_cleanly("y = x + big", *t, [&](uinteger_t& x) { uinteger_t y = x + big; x = y; });
	}
	// A copy that cannot allocate throws and leaves nothing behind.
	const long before = live();
	bool threw = false;
	fail_in = 0;
	try {
		uinteger_t c = big;
		keep(c);
	} catch (const std::bad_alloc&) {
		threw = true;
	}
	fail_in = -1;
	if (!threw || live() != before) {
		std::printf("FAIL %s:%d: a failed copy: threw %d, %ld blocks leaked\n", __FILE__, __LINE__, threw, live() - before);
		++failures;
	}
}

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
	check_no_leaks();
	check_failed_allocations();
	if (failures) {
		std::printf("alloc: %d failures\n", failures);
		return 1;
	}
	std::printf("alloc OK\n");
	return 0;
}

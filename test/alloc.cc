// Allocation counts of the small-value operations. A result costs one allocation for its limbs and
// nothing else: shifts by a native count build no temporaries, in-place operations on a value
// with room allocate nothing, and `a % b` and `a / b` build only the result they return.
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

int main() {
	for (std::size_t n = 1; n <= 4; ++n) {
		const auto a = limbs(n, 0x7f);
		const auto b = limbs(n, 0x35);
		const auto one_limb = limbs(1, 0x11);
		const auto half = limbs((n + 1) / 2, 0x13);
		const auto count = uinteger_t(3);
		const std::size_t wide = uinteger_t::digit_bits + 3; // a limb and a few bits
		uinteger_t c = a;
		uinteger_t room;
		room.reserve(n + 2);

		COUNT(1, uinteger_t x = a; keep(x));
		COUNT(1, uinteger_t x(12345); keep(x));
		COUNT(0, c = a; keep(c));
		COUNT(0, volatile bool r = a < b; (void)r);
		COUNT(0, volatile bool r = a == b; (void)r);
		COUNT(0, volatile bool r = static_cast<bool>(a); (void)r);
		COUNT(0, volatile std::size_t r = a.bits(); (void)r);

		COUNT(1, keep(a + b));
		COUNT(1, keep(a - b));
		COUNT(1, keep(a * b));
		COUNT(1, keep(a / one_limb));
		COUNT(1, keep(a % one_limb));
		COUNT(1, keep(a / half));
		COUNT(1, keep(a % half));
		COUNT(2, auto qr = a.divmod(half); keep(qr.first); keep(qr.second));

		// In place with room: no allocation.
		room = a;
		room.reserve(n + 2);
		COUNT(0, room += b; keep(room));
		COUNT(0, room -= b; keep(room));

		// Shifts by a native integer or by a one-limb value allocate only the result.
		COUNT(1, keep(a << 3));
		COUNT(1, keep(a << wide));
		COUNT(1, keep(a >> 3));
		COUNT(n > 1 ? 1 : 0, keep(a >> wide));
		COUNT(1, keep(a << count));
		COUNT(1, keep(a >> count));
		room = a;
		room.reserve(n + 4);
		COUNT(0, room <<= wide; keep(room));
		COUNT(0, room >>= wide; keep(room));
		COUNT(0, room <<= count; keep(room));
		COUNT(0, room >>= count; keep(room));
	}
	if (failures) {
		std::printf("alloc: %d failures\n", failures);
		return 1;
	}
	std::printf("alloc OK\n");
	return 0;
}

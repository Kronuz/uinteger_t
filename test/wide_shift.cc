// Shift counts held in a uinteger_t that take more than one limb at the narrower limb widths: a
// count of up to 64 bits is read exactly (two limbs at 32-bit limbs, four at 16-bit), so
// `7 << 2^32` at 32-bit limbs and `7 << 2^16` at 16-bit limbs shift by that many bits (512 MiB and
// 8 KiB results), and only a count of 2^64 bits or more is treated as beyond any size a value can
// have: a left shift of a nonzero value throws std::length_error, a right shift gives zero.
// At 32-bit limbs the test builds a 512 MiB value, so it is its own CTest target.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
#define UINT_T_PUBLIC_IMPLEMENTATION
#include "uinteger_t.hh"

using U = uinteger_t;
using digit = U::digit;
static constexpr unsigned long long bits = U::digit_bits;
static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); if (++failures > 10) std::exit(1); } } while (0)

// A count as a uinteger_t, limb by limb (the integral constructor keeps only the low limb).
static U make_count(unsigned long long n) {
	U c;
	for (unsigned long long done = 0; done < 64; done += bits) {
		c <<= bits;
		c |= U(static_cast<digit>(n >> (64 - bits - done)));
	}
	return c;
}

// x == v * 2^count, checked limb by limb without the shift operators.
static bool is_shifted(const U& x, unsigned long long count, unsigned v) {
	const unsigned long long q = count / bits;
	const unsigned r = static_cast<unsigned>(count % bits);
	const unsigned long long t = static_cast<unsigned long long>(v) << r;
	const unsigned long long mask = ~0ull >> (64 - bits);
	const bool two = ((t >> (bits - 1)) >> 1) != 0;
	const std::size_t top = static_cast<std::size_t>(q) + 1 + (two ? 1 : 0);
	if (x.size() != top) return false;
	const digit* p = x.data();
	for (unsigned long long i = 0; i < q; ++i) {
		if (p[i]) return false;
	}
	if (p[q] != static_cast<digit>(t & mask)) return false;
	return !two || p[q + 1] == static_cast<digit>((t >> (bits - 1)) >> 1);
}

static void check_shift_count() {
	unsigned long long n = 1;
	CHECK(U::shift_count(U(), n) && n == 0, "count of zero");
	CHECK(U::shift_count(make_count(5), n) && n == 5, "count of five");
	CHECK(U::shift_count(make_count(1ull << 32), n) && n == (1ull << 32), "count 2^32 (%zu limbs)", make_count(1ull << 32).size());
	CHECK(U::shift_count(make_count(~0ull), n) && n == ~0ull, "count 2^64 - 1");
	U two64(1);
	two64 <<= 64;
	CHECK(!U::shift_count(two64, n), "count 2^64 is not read");
	CHECK(!U::shift_count(two64 + U(5), n), "count 2^64 + 5 is not read");
	CHECK(!U::shift_count(two64 << 40, n), "count 2^104 is not read");
}

static void check_wide_counts() {
	const U seven(7);

	// A count of 2^64 bits or more: left shift of a nonzero value throws, of zero gives zero,
	// right shift gives zero.
	U two64(1);
	two64 <<= 64;
	bool threw = false;
	try {
		(void)(seven << two64);
	} catch (const std::length_error&) {
		threw = true;
	}
	CHECK(threw, "7 << 2^64 throws std::length_error");
	CHECK(!(U() << two64), "0 << 2^64 is zero");
	CHECK(!(seven >> two64), "7 >> 2^64 is zero");
	CHECK(!(seven >> (two64 << 20)), "7 >> 2^84 is zero");

	// Counts below 2^64 that need more than one limb: right shifts of small values are zero.
	if (bits < 64) {
		CHECK(!(seven >> make_count(1ull << 40)), "7 >> 2^40 is zero");
		CHECK(!(seven >> make_count((1ull << 40) + 3)), "7 >> 2^40 + 3 is zero");
	}

	// Counts of exactly one more limb than a native count: the shift happens.
	std::vector<unsigned long long> counts;
	if (bits == 16) {
		counts = {1ull << 16, (1ull << 16) + 1, (1ull << 16) + 15, (1ull << 16) + 16, (1ull << 17) + 3, (1ull << 20) + 7};
	} else if (bits == 32 && sizeof(std::size_t) >= 8) {
		counts = {1ull << 32};
	}
	for (auto count : counts) {
		const U c = make_count(count);
		CHECK(c.size() > 1, "count %llu takes %zu limbs", count, c.size());
		{
			const U x = seven << c;
			CHECK(is_shifted(x, count, 7), "7 << %llu (%zu limbs)", count, x.size());
			CHECK(x.bits() == count + 3, "7 << %llu has %zu bits", count, x.bits());
			CHECK((x >> c) == seven, "(7 << %llu) >> %llu", count, count);
			CHECK((x >> make_count(count + 1)) == U(3), "(7 << %llu) >> %llu", count, count + 1);
			CHECK(!(x >> make_count(count + 3)), "(7 << %llu) >> %llu is zero", count, count + 3);
		}
		{
			U y = seven;
			y <<= c;
			CHECK(is_shifted(y, count, 7), "7 <<= %llu (%zu limbs)", count, y.size());
			y >>= c;
			CHECK(y == seven, "(7 <<= %llu) >>= %llu", count, count);
		}
	}
}

int main() {
	check_shift_count();
	check_wide_counts();
	if (failures) {
		std::printf("wide_shift: %d failures\n", failures);
		return 1;
	}
	std::printf("wide_shift OK (%u-bit limbs)\n", static_cast<unsigned>(bits));
	return 0;
}

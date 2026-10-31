// The public members that no other test reaches without UINTEGER_T_PUBLIC_IMPLEMENTATION: compare, the
// output-reusing mul and divmod, is_result, base_bits and base_size. This file does not define the hook,
// so it does not compile if one of them is private. The kernels' own tests (property.cc) run these
// forms over random values at every algorithm; this one pins the documented behavior on exact values.
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "uinteger_t.hh"

using U = uinteger_t;

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

static U hex(const std::string& digits) {
	return U(digits, 16);
}

static U ones(std::size_t n) {  // 2^(4n) - 1
	return hex(std::string(n, 'f'));
}

static U pow2(std::size_t k) {  // 2^k
	return hex("1" + std::string(k / 4, '0')) << (k % 4);
}

static void check_compare() {
	static_assert(std::is_same<decltype(U::compare(std::declval<const U&>(), std::declval<const U&>())), int>::value, "compare returns int");
	const std::vector<U> values = {U(), U(1), U(2), U(255), ones(15), ones(16), pow2(64), pow2(64) + U(1), ones(31), ones(32), pow2(128), pow2(128) + U(1), pow2(300), pow2(300) - U(1)};
	for (std::size_t i = 0; i < values.size(); ++i) {
		for (std::size_t j = 0; j < values.size(); ++j) {
			// The values are ordered by their digits in hex: shorter is smaller, then digit by digit.
			const auto x = values[i].str(16), y = values[j].str(16);
			const int want = x.size() != y.size() ? (x.size() < y.size() ? -1 : 1) : (x < y ? -1 : (x == y ? 0 : 1));
			CHECK(U::compare(values[i], values[j]) == want, "compare(%s, %s) = %d, expected %d", x.c_str(), y.c_str(), U::compare(values[i], values[j]), want);
			CHECK((values[i] < values[j]) == (want < 0) && (values[i] == values[j]) == (want == 0), "operators disagree with compare for %s, %s", x.c_str(), y.c_str());
		}
	}
}

static void check_mul() {
	const U max64 = ones(16);
	const U expect = hex("fffffffffffffffe0000000000000001");  // (2^64 - 1)^2
	const U big_a = ones(32), big_b = pow2(128) + U(1);
	const U big_expect = ones(64);  // (2^128 - 1)(2^128 + 1) = 2^256 - 1

	U r = ones(40);  // a longer value, overwritten
	CHECK(&U::mul(r, max64, max64) == &r && r == expect, "mul(r, a, a): %s", r.str(16).c_str());
	r = max64;
	U::mul(r, r, max64);
	CHECK(r == expect, "mul(r = a, a, b)");
	r = max64;
	U::mul(r, max64, r);
	CHECK(r == expect, "mul(r = b, a, b)");
	r = max64;
	U::mul(r, r, r);
	CHECK(r == expect, "mul(r = a = b, a, a)");

	r = U();
	U::mul(r, big_a, big_b);
	CHECK(r == big_expect, "mul(r, a, b) over zero");
	r = big_a;
	U::mul(r, r, big_b);
	CHECK(r == big_expect, "mul(r = a, a, b) at two limbs");
	r = big_b;
	U::mul(r, big_a, r);
	CHECK(r == big_expect, "mul(r = b, a, b) at two limbs");

	r = ones(20);
	U::mul(r, U(), big_a);
	CHECK(!r, "mul(r, 0, b) is zero");
	U::mul(r, big_a, U());
	CHECK(!r, "mul(r, a, 0) is zero");
	r = ones(20);
	U::mul(r, U(1), big_a);
	CHECK(r == big_a, "mul(r, 1, b) is b");
	U::mul(r, big_b, U(1));
	CHECK(r == big_b, "mul(r, a, 1) is a");
	CHECK(U::compare(r, big_b) == 0, "the result is trimmed");
}

static void check_divmod() {
	const U a = pow2(256);        // 2^256
	const U b = ones(32);         // 2^128 - 1
	const U q_expect = pow2(128) + U(1);
	const U r_expect = U(1);      // 2^256 = (2^128 - 1)(2^128 + 1) + 1

	U q = ones(70), r = ones(3);  // outputs of other sizes, overwritten
	auto out = U::divmod(q, r, a, b);
	CHECK(&out.first.get() == &q && &out.second.get() == &r, "divmod returns references to its outputs");
	CHECK(q == q_expect && r == r_expect, "divmod(q, r, a, b)");

	// Each output taking an operand's place: the operand is the output object itself.
	q = a;
	r = U();
	U::divmod(q, r, q, b);
	CHECK(q == q_expect && r == r_expect, "divmod(q, r, q = a, b)");
	q = b;
	r = U();
	U::divmod(q, r, a, q);
	CHECK(q == q_expect && r == r_expect, "divmod(q, r, a, q = b)");
	q = U();
	r = a;
	U::divmod(q, r, r, b);
	CHECK(q == q_expect && r == r_expect, "divmod(q, r, r = a, b)");
	q = U();
	r = b;
	U::divmod(q, r, a, r);
	CHECK(q == q_expect && r == r_expect, "divmod(q, r, a, r = b)");
	q = a;
	r = b;
	U::divmod(q, r, q, r);
	CHECK(q == q_expect && r == r_expect, "divmod(q, r, q = a, r = b)");
	q = b;
	r = a;
	U::divmod(q, r, r, q);
	CHECK(q == q_expect && r == r_expect, "divmod(q, r, r = a, q = b)");
	q = b;
	U::divmod(q, r, q, q);
	CHECK(q == U(1) && !r, "divmod(q, r, q = b, q)");
	r = b;
	U::divmod(q, r, r, r);
	CHECK(q == U(1) && !r, "divmod(q, r, r = b, r)");

	// A divisor of one limb, a divisor of one, a dividend below the divisor, and equal operands.
	const U small(1000003);
	q = ones(9);
	r = ones(9);
	U::divmod(q, r, a, small);
	CHECK(q * small + r == a && r < small, "divmod by one limb");
	U::divmod(q, r, a, U(1));
	CHECK(q == a && !r, "divmod by one");
	U::divmod(q, r, b, a);
	CHECK(!q && r == b, "divmod of a smaller dividend");
	q = b;
	U::divmod(q, r, q, a);
	CHECK(!q && r == b, "divmod of a smaller dividend, quotient in its place");
	r = b;
	U::divmod(q, r, r, a);
	CHECK(!q && r == b, "divmod of a smaller dividend, remainder in its place");
	U::divmod(q, r, a, a);
	CHECK(q == U(1) && !r, "divmod of equal operands");
	U::divmod(q, r, U(), a);
	CHECK(!q && !r, "divmod of zero");

	// A zero divisor throws before an output is written.
	q = a;
	r = b;
	bool threw = false;
	try {
		U::divmod(q, r, a, U());
	} catch (const std::domain_error&) {
		threw = true;
	}
	CHECK(threw && q == a && r == b, "divmod by zero throws and leaves the outputs");
}

static void check_base_tables() {
	static_assert(U::is_result<std::string>::value, "std::string is a result");
	static_assert(U::is_result<std::vector<unsigned char>>::value, "a vector of bytes is a result");
	static_assert(!U::is_result<int>::value && !U::is_result<U>::value, "an integer and a uinteger_t are not results");
	static_assert(U::base_bits(16) == 4, "base_bits is usable in a constant expression");

	for (int base = 2; base <= 256; ++base) {
		unsigned bits = 0;
		for (int b = base; b > 1; b >>= 1) {
			++bits;
		}
		const bool power = (base & (base - 1)) == 0;
		CHECK(U::base_bits(base) == (power ? bits : 0), "base_bits(%d) = %u", base, U::base_bits(base));

		unsigned digits = 0;  // the digits of the largest 64-bit value in this base
		for (std::uint64_t v = ~std::uint64_t(0); v; v /= static_cast<std::uint64_t>(base)) {
			++digits;
		}
		CHECK(U::base_size(base) == digits, "base_size(%d) = %u, expected %u", base, U::base_size(base), digits);
	}
}

int main() {
	check_compare();
	check_mul();
	check_divmod();
	check_base_tables();
	if (failures) {
		std::printf("public api: %d failures\n", failures);
		return 1;
	}
	std::printf("public api OK\n");
	return 0;
}

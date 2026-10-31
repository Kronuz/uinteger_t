// The conversion tree (digits() and from_digits() above the kernels) against repeated
// division and against the kernels, with leaves of many sizes so the recursion runs several
// levels deep on small values. The internals are private, so this includes the header with
// `private` opened up. The tree's own parameters (leaf limbs) are arguments here; CMake also
// builds property.cc with tiny thresholds to run the public entry points through the tree.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#define private public
#include "uinteger_t.hh"
#undef private

using U = uinteger_t;
using digit = U::digit;
static constexpr std::size_t digit_bits = U::digit_bits;

static std::mt19937_64 rng(20261101);
static int failures = 0;
// Light mode (a third argument other than 0; CMake passes it in Debug builds, where CI runs the
// sanitizers unoptimized): the sections that visit many bases and edge values visit a sample of
// them, and the size sweeps take larger steps. Every distinct check runs.
static bool light = false;

#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); if (++failures > 10) std::exit(1); } } while (0)

// A value of `limbs` limbs: random limbs, runs of ones and zeros, or sparse ones.
static U random_value(std::size_t limbs) {
	std::vector<std::uint32_t> w(limbs * (digit_bits / 32));
	const int style = static_cast<int>(rng() % 6);
	for (auto& x : w) {
		switch (style) {
			case 0: x = static_cast<std::uint32_t>(rng()); break;
			case 1: x = ~0u; break;
			case 2: x = (rng() & 3) ? 0 : static_cast<std::uint32_t>(rng()); break;
			case 3: x = (rng() & 1) ? ~0u : 0; break;
			default: x = static_cast<std::uint32_t>(rng()); break;
		}
	}
	if (!w.empty() && !w.back()) w.back() = 1;
	// Big-endian bytes, most significant word first: one parse, not a shift per word.
	std::string bytes;
	bytes.reserve(w.size() * 4);
	for (auto i = w.size(); i--;) {
		for (int s = 24; s >= 0; s -= 8) bytes.push_back(static_cast<char>(w[i] >> s));
	}
	return bytes.empty() ? U() : U(bytes, 256);
}

// Digits by single-limb division: independent of the kernels and the tree.
static std::vector<unsigned char> chunk_digits(const U& v, int base) {
	std::vector<unsigned char> d;
	U q = v;
	while (q) d.push_back(static_cast<unsigned char>(q.divmod_small(static_cast<digit>(base))));
	if (d.empty()) d.push_back(0);
	std::reverse(d.begin(), d.end());
	return d;
}

static bool is_power_of_two(int base) {
	return (base & (base - 1)) == 0;
}

// Whether a section that visits every base visits this one: all of them, or in light mode the
// bases below 12, every eleventh, the powers of two and the top five (they span every digit
// count per limb and both table shapes: with and without a guard limb).
static bool sampled(int base) {
	return !light || base < 12 || base % 11 == 0 || is_power_of_two(base) || base > 250;
}

// b^j and D^j and their neighbours up to `max_limbs` limbs, powers of two around limb
// boundaries, and all-ones: the splits of the tree fall on these. Light mode takes every fifth
// of the powers and every third of the powers of two, and the latter only when `bit_edges`
// (they do not depend on the base).
static std::vector<U> radix_edges(int base, std::size_t max_limbs, unsigned stride = 1, bool bit_edges = true) {
	std::vector<U> v;
	unsigned k;
	const digit D = U::base_chunk(base, k);
	if (light) stride *= 5;
	for (const auto& mult : {U(base), U(D)}) {
		U p(1);
		for (unsigned i = 0; p.size() <= max_limbs; ++i, p *= mult) {
			if (i % stride) continue;
			if (p > U(1)) v.push_back(p - U(1));
			v.push_back(p);
			v.push_back(p + U(1));
		}
	}
	for (std::size_t bit = 1; bit_edges && bit <= max_limbs * digit_bits; bit += (light ? 3 : 1) * digit_bits / 2) {
		const U p = U(1) << bit;
		v.push_back(p - U(1));
		v.push_back(p);
		v.push_back(p + U(1));
	}
	return v;
}

// One value through the tree both ways, with leaves of `leaf` limbs.
static void check_value(const U& v, int base, std::size_t leaf, const std::vector<unsigned char>* known = nullptr) {
	if (!v) return;
	const auto ref = known ? *known : chunk_digits(v, base);
	std::vector<unsigned char> got;
	U::_digits_tree(got, base, v.data(), v.size(), leaf);
	CHECK(got == ref, "tree digits of %zu limbs, %zu bits, base %d, leaf %zu: %zu digits, reference %zu", v.size(), v.bits(), base, leaf, got.size(), ref.size());

	// Appends after what is there, to a string and to a vector of wider digits.
	std::string tail(3, '\x07');
	U::_digits_tree(tail, base, v.data(), v.size(), leaf);
	CHECK(tail.size() == ref.size() + 3 && tail.compare(0, 3, std::string(3, '\x07')) == 0 && std::equal(ref.begin(), ref.end(), tail.begin() + 3, [](unsigned char a, char b) { return a == static_cast<unsigned char>(b); }),
	      "tree digits append, %zu limbs, base %d, leaf %zu", v.size(), base, leaf);
	std::vector<int> wide;
	U::_digits_tree(wide, base, v.data(), v.size(), leaf);
	CHECK(wide.size() == ref.size() && std::equal(ref.begin(), ref.end(), wide.begin()), "tree digits into ints, %zu limbs, base %d, leaf %zu", v.size(), base, leaf);

	const auto& rx = U::_radix_for(base);
	CHECK(U::_from_digit_tree(rx, ref.data(), ref.size(), base, leaf) == v, "tree from_digits of %zu limbs, base %d, leaf %zu", v.size(), base, leaf);
	std::vector<unsigned char> padded(1 + rng() % 70, 0);
	padded.insert(padded.end(), ref.begin(), ref.end());
	CHECK(U::_from_digit_tree(rx, padded.data(), padded.size(), base, leaf) == v, "tree from_digits with leading zeros, %zu limbs, base %d, leaf %zu", v.size(), base, leaf);
}

// The tree encoder on a buffer full of garbage: it writes every digit of s 2^J, the zero
// padding included. With a negative slack the plan falls short of the value and must retry
// with a longer leaf until the top power passes; `retried` counts those plans.
static int retried = 0;

static void check_plan(const U& v, int base, std::size_t leaf, int slack) {
	if (!v) return;
	const auto& rx = U::_radix_for(base);
	U::radix_tree tr;
	U::radix_leaf lf;
	U::_tree_plan(rx, v.bits(), leaf, slack, tr, lf);
	// The same shape without the retry: a different one means the check made the plan longer.
	const double estimate = std::ceil(static_cast<double>(v.bits()) / std::log2(static_cast<double>(rx.D)));
	unsigned levels, chunks;
	U::_tree_shape(rx, static_cast<std::size_t>(std::max(2.0, estimate + slack)), std::min(leaf, U::radix_max_limbs - 4), levels, chunks);
	retried += (levels != tr.levels || chunks != tr.chunks);
	const std::size_t cap = tr.leaf_digits << tr.levels;
	std::vector<unsigned char> buffer(cap, 0xaa);
	const std::size_t count = U::_tree_encode(rx, tr, lf, v.data(), v.size(), buffer.data());
	const auto ref = chunk_digits(v, base);
	CHECK(count == ref.size() && std::equal(ref.begin(), ref.end(), buffer.begin()), "tree encode into a dirty buffer, %zu limbs, base %d, leaf %zu, slack %d", v.size(), base, leaf, slack);
}

// The powers the tree builds are D^(T 2^j), in the sizes and shifts it records.
static void check_powers(int base, unsigned levels, unsigned chunks) {
	const auto& rx = U::_radix_for(base);
	for (bool normalize : {false, true}) {
		U::radix_tree tr;
		U::_tree_build(rx, tr, levels, chunks, normalize);
		U expect(1);
		for (unsigned i = 0; i < chunks; ++i) expect *= U(rx.D);
		CHECK(tr.leaf_digits == static_cast<std::size_t>(chunks) * rx.k && tr.levels == levels && tr.chunks == chunks, "tree shape, base %d", base);
		for (unsigned j = 0; j < levels; ++j) {
			CHECK(tr.size[j] == expect.size(), "power %u of D^%u, base %d: %zu limbs, expected %zu", j, chunks, base, tr.size[j], expect.size());
			if (tr.size[j] != expect.size()) break;
			U got;
			got.resize(tr.size[j], 0);
			std::copy(tr.power[j], tr.power[j] + tr.size[j], got.data());
			if (normalize) {
				CHECK(tr.shift[j] == digit_bits - U::_bits(expect.back()), "shift of power %u, base %d", j, base);
				CHECK((tr.power[j][tr.size[j] - 1] >> (digit_bits - 1)) == 1, "power %u normalized, base %d", j, base);
				got >>= tr.shift[j];
			}
			CHECK(got == expect, "power %u of D^%u, base %d", j, chunks, base);
			expect = expect * expect;
		}
	}
}

// The levels and leaf of a value: T 2^J chunks cover it, and the leaf is within its limit.
static void check_shape() {
	for (int base : {3, 10, 58, 255}) {
		const auto& rx = U::_radix_for(base);
		const std::size_t dbits = U::_bit_length(rx.D);
		for (std::size_t leaf_limbs : {2, 3, 5, 16, 64, 124}) {
			for (std::size_t chunks = 1; chunks < 5000; chunks += 1 + chunks / 7) {
				unsigned levels, leaf;
				U::_tree_shape(rx, chunks, leaf_limbs, levels, leaf);
				CHECK(levels >= 1 && leaf >= 2, "shape of %zu chunks, base %d", chunks, base);
				CHECK((static_cast<std::size_t>(leaf) << levels) >= chunks, "shape of %zu chunks covers %u * 2^%u, base %d", chunks, leaf, levels, base);
				CHECK(leaf <= std::max<std::size_t>(2, leaf_limbs * digit_bits / dbits), "shape leaf %u of %zu chunks, base %d, limit %zu limbs", leaf, chunks, base, leaf_limbs);
				// Fewest levels: one fewer would need a longer leaf than allowed.
				if (levels > 1) {
					CHECK(((chunks + (std::size_t(1) << (levels - 1)) - 1) >> (levels - 1)) > std::max<std::size_t>(2, leaf_limbs * digit_bits / dbits), "shape of %zu chunks is not the fewest levels, base %d", chunks, base);
				}
			}
		}
	}
	check_powers(10, 1, 2);
	check_powers(10, 3, 2);
	check_powers(10, 4, 5);
	check_powers(3, 5, 3);
	check_powers(255, 4, 7);
	check_powers(58, 6, 33);
	check_powers(62, 3, 64);
}

template <typename F>
static bool throws_length(F f) {
	try { f(); } catch (const std::length_error&) { return true; }
	return false;
}

template <typename F>
static bool throws_logic(F f) {
	try { f(); } catch (const std::logic_error&) { return true; }
	return false;
}

// s 2^levels digits as a Size: _tree_digits_for throws exactly when the product does not fit
// (checked by doubling, a different route from the shift it guards).
template <typename Size>
static void check_digits_for(unsigned long long leaf_digits, unsigned levels) {
	const unsigned long long max = std::numeric_limits<Size>::max();
	unsigned long long v = leaf_digits;
	bool fits = v <= max;
	for (unsigned i = 0; fits && i < levels; ++i) {
		fits = v <= max / 2;
		v *= 2;
	}
	Size got = 0;
	const bool threw = throws_length([&] { got = U::_tree_digits_for<Size>(static_cast<std::size_t>(leaf_digits), levels); });
	CHECK(threw == !fits, "digits for %llu << %u at %zu bits: %s", leaf_digits, levels, 8 * sizeof(Size), threw ? "threw" : "did not throw");
	CHECK(threw || got == v, "digits for %llu << %u at %zu bits: got %llu", leaf_digits, levels, 8 * sizeof(Size), static_cast<unsigned long long>(got));
}

// The next size of the 16-bit sweep: every size within 64 limbs of the limit, where the capacity guard
// first takes effect, and geometric steps elsewhere. Light mode takes every eighth size of that window
// and steps of half the size (each size builds the plan's powers), but not past the first size of the
// window.
static std::size_t next_size(std::size_t limbs) {
	const std::size_t limit = 65535 / digit_bits;
	if (limbs + 64 >= limit && limbs <= limit) return limbs + (light ? 8 : 1);
	std::size_t step = 1 + limbs / (light ? 2 : 24);
	if (light && limbs + step + 64 > limit && limbs + 64 < limit) step = limit - 64 - limbs;
	return limbs + step;
}

// The tree's size arithmetic at a size type narrower than std::size_t, the model of a 32-bit
// size_t (SR1: the bit count of a 512 MiB value wrapped there and digits() returned short digits
// without an error). A bit count or digit capacity that does not fit throws std::length_error and
// leaves the output alone; the rest converts.
static void check_size_guards() {
	for (unsigned levels = 0; levels <= 70; ++levels) {
		for (unsigned long long leaf : {1ull, 2ull, 3ull, 1000ull, 4095ull, 4096ull, 65535ull, 65536ull, 4294967295ull}) {
			check_digits_for<std::uint16_t>(leaf, levels);
			check_digits_for<std::uint32_t>(leaf, levels);
		}
	}
	int bits_guard = 0, cap_guard = 0, converted = 0;
	for (int base : {3, 255}) {
		const auto& rx = U::_radix_for(base);
		for (std::size_t leaf : {2, 124}) {
			// Every size within 64 limbs of the 16-bit limit, where the capacity passes it first.
			for (std::size_t limbs = 1; limbs <= 2 * 65536 / digit_bits + 40; limbs = next_size(limbs)) {
				const auto v = random_value(limbs);
				std::string out = "ab";
				const bool bits_over = v.bits() > 65535;
				bool cap_over = false;
				if (!bits_over) {
					U::radix_tree tr;
					U::radix_leaf lf;
					U::_tree_plan(rx, v.bits(), leaf, 1, tr, lf);
					cap_over = (tr.leaf_digits << tr.levels) > 65535;
					// The plan itself refuses, before it builds the powers.
					U::radix_tree tr16;
					U::radix_leaf lf16;
					CHECK(throws_length([&] { U::_tree_plan<std::uint16_t>(rx, v.bits(), leaf, 1, tr16, lf16); }) == cap_over, "16-bit tree plan of %zu limbs, base %d, leaf %zu", limbs, base, leaf);
				}
				// Light mode converts every eighth size that passes both guards above 160 limbs;
				// the plan check above is the capacity guard at every size.
				if (light && limbs > 160 && limbs % 8 && !bits_over && !cap_over) continue;
				const bool threw = throws_length([&] { U::_digits_tree<std::uint16_t>(out, base, v.data(), v.size(), leaf); });
				CHECK(threw == (bits_over || cap_over), "16-bit tree of %zu limbs, base %d, leaf %zu: %s", limbs, base, leaf, threw ? "threw" : "did not throw");
				if (threw) {
					CHECK(out == "ab", "16-bit tree of %zu limbs, base %d: output touched", limbs, base);
					bits_guard += bits_over;
					cap_guard += !bits_over;
				} else {
					// The digits against repeated division where that is cheap, else by the round trip.
					if (limbs <= 160) {
						const auto ref = chunk_digits(v, base);
						CHECK(out.size() == 2 + ref.size() && out.compare(0, 2, "ab") == 0 && std::equal(ref.begin(), ref.end(), out.begin() + 2, [](unsigned char a, char b) { return a == static_cast<unsigned char>(b); }),
						      "16-bit tree digits of %zu limbs, base %d, leaf %zu", limbs, base, leaf);
					} else {
						CHECK(out.compare(0, 2, "ab") == 0 && U::from_digits(out.data() + 2, out.size() - 2, base) == v, "16-bit tree digits round trip, %zu limbs, base %d, leaf %zu", limbs, base, leaf);
					}
					++converted;
				}
			}
		}
	}
	CHECK(bits_guard > 0 && cap_guard > 0 && converted > 0, "narrow sizes took the bit guard %d, the capacity guard %d and converted %d times", bits_guard, cap_guard, converted);

	// A value past P_0 reaching a leaf is an error in every build, not a short digit string (SR5).
	const auto& rx = U::_radix_for(10);
	U::radix_tree tr;
	U::radix_leaf lf;
	U::_tree_plan(rx, 4000, 16, 1, tr, lf);
	std::vector<digit> over(lf.limbs + 1, 1);
	std::vector<unsigned char> buffer(tr.leaf_digits);
	CHECK(throws_logic([&] { U::_tree_leaf(rx, tr, lf, over.data(), over.size(), buffer.data()); }), "a leaf longer than P_0 throws");
	CHECK(!throws_logic([&] { U::_tree_leaf(rx, tr, lf, over.data(), lf.limbs, buffer.data()); }), "a leaf of P_0's size does not throw");
}

int main(int argc, char** argv) {
	const int rounds = argc > 1 ? std::atoi(argv[1]) : 1;
	const std::size_t big = argc > 2 ? static_cast<std::size_t>(std::atoi(argv[2])) : 150;
	light = argc > 3 && std::atoi(argv[3]) != 0;

	check_shape();
	check_size_guards();
	// Every base that is not a power of two on small values, with the tiniest leaves.
	for (int base = 3; base <= 255; ++base) {
		if (is_power_of_two(base) || !sampled(base)) continue;
		for (const auto& v : radix_edges(base, 9, base < 8 ? 11 : 3)) {
			const auto ref = chunk_digits(v, base);
			for (std::size_t leaf : {2, 4}) check_value(v, base, leaf, &ref);
		}
		for (std::size_t limbs = 1; limbs <= 14; ++limbs) {
			const auto v = random_value(limbs);
			for (std::size_t leaf : {2, 5}) check_value(v, base, leaf);
		}
	}

	// A spread of bases over sizes up to `big` limbs, past the kernels' limit, with leaves from
	// two limbs to the largest the kernels take. Light mode takes five bases (digit counts per
	// limb 40, 19, 12, 10 and 8, with and without a guard limb) and the powers of two once.
	const std::vector<int> spread = light ? std::vector<int>{3, 10, 36, 58, 255} : std::vector<int>{3, 5, 6, 7, 10, 11, 13, 36, 57, 58, 62, 85, 100, 255};
	for (int base : spread) {
		for (const auto& v : radix_edges(base, big, base < 8 ? 29 : 7, !light || base == spread.front())) {
			const auto ref = chunk_digits(v, base);
			for (std::size_t leaf : {2, 33, 124}) check_value(v, base, leaf, &ref);
		}
		for (std::size_t limbs = 1; limbs <= big; limbs += 1 + limbs / 9) {
			for (int r = 0; r < rounds; ++r) {
				const auto v = random_value(limbs);
				const auto ref = chunk_digits(v, base);
				for (std::size_t leaf : {2, 5, 20, 64, 124}) check_value(v, base, leaf, &ref);
			}
		}
	}

	// The padding is written, not left to a zeroed buffer, and a plan that falls short retries.
	for (int base : {3, 10, 58, 255}) {
		if (light && base != 10 && base != 255) continue;
		for (const auto& v : radix_edges(base, 40, base < 8 ? 29 : 5, !light)) {
			for (std::size_t leaf : {2, 9, 40}) {
				check_plan(v, base, leaf, 1);
				check_plan(v, base, leaf, -4);
			}
		}
		for (std::size_t limbs : {1, 3, 8, 21, 64, 100}) {
			const auto v = random_value(limbs);
			for (std::size_t leaf : {2, 9, 40}) {
				check_plan(v, base, leaf, 1);
				check_plan(v, base, leaf, -4);
			}
		}
	}
	CHECK(retried > 0, "no plan retried");

	// Where the kernels also run, the tree gives their digits.
	for (int base : {10, 58, 255}) {
		for (std::size_t limbs : {1, 2, 5, 9, 33, 64, 127, 128}) {
			const auto v = random_value(limbs);
			std::string kernel, tree;
			U::_digits_radix(kernel, base, v.data(), v.size());
			U::_digits_tree(tree, base, v.data(), v.size(), 16);
			CHECK(kernel == tree, "tree against the kernel at %zu limbs, base %d", limbs, base);
		}
	}

	if (failures) {
		std::printf("radix_tree: %d failures\n", failures);
		return 1;
	}
	std::printf("radix_tree OK (up to %zu limbs%s)\n", big, light ? ", light" : "");
	return 0;
}

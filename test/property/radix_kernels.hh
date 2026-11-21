// The radix encoding kernel and every digit splitter against the general one. Included by property.cc.

#pragma once

#include "common.hh"
#include "digits_reference.hh"

static void check_radix_kernel() {
	// Every base that is not a power of two, at the sizes of the unrolled kernels.
	for (int base = 3; base <= 255; ++base) {
		if (is_power_of_two(base) || !sampled(base)) continue;
		for (const auto& v : radix_edges(base, 5)) check_against_chunks(v, base);
	}
	for (int base : {3, 5, 6, 7, 10, 11, 13, 36, 57, 58, 62, 85, 100, 255}) {
		for (const auto& v : radix_edges(base, 9)) check_against_chunks(v, base);
		// Through the general kernel, around its size limit (above it the tree runs), and above.
		// Light mode runs the sizes up to 33 limbs on every base and the larger ones (the
		// reference division is quadratic) on four bases of different digit counts per limb.
		const bool few = light && base != 3 && base != 10 && base != 58 && base != 255;
		const std::size_t top = uinteger_t::radix_max_limbs;
		for (std::size_t limbs : {std::size_t(10), std::size_t(11), std::size_t(12), std::size_t(15), std::size_t(16), std::size_t(17), std::size_t(31), std::size_t(32),
		                          std::size_t(33), std::size_t(64), std::size_t(65), top - 1, top, top + 1, top + 2, top + 22}) {
			if (light && (limbs == 65 || limbs == top - 1 || limbs == top + 2 || limbs == top + 22)) continue;
			if (few && limbs > 33) continue;
			for (const auto& v : radix_edges_at(base, limbs)) check_against_chunks(v, base);
		}
	}
	// The tables built without a guard limb or without the 1/2 offset must fail on these inputs.
	// A base needs its guard limb at every size once D / B is large; 3 and 255 do at either
	// limb width, base 10 only at 64-bit. The edges reach up to 9 limbs in light mode (12 in full
	// mode), and the 1/2 offset check takes three bases.
	const std::size_t mutant_limbs = light ? 9 : 12;
	for (int base : {3, 255, 10}) {
		unsigned k;
		const double r = static_cast<double>(uinteger_t::base_chunk(base, k)) / std::ldexp(1.0, static_cast<int>(digit_bits));
		CHECK(mutant_failures(base, -1, true, mutant_limbs) == 0, "tables of base %d", base);
		if (r >= 0.5) CHECK(mutant_failures(base, 0, true, mutant_limbs) > 0, "no guard limb survives in base %d", base);
	}
	for (int base : {10, 36, 58, 62, 255}) {
		if (light && (base == 36 || base == 62)) continue;
		CHECK(mutant_failures(base, -1, true, mutant_limbs) == 0, "tables of base %d", base);
		CHECK(mutant_failures(base, -1, false, mutant_limbs) > 0, "no 1/2 offset survives in base %d", base);
	}
	std::unique_ptr<uinteger_t::radix_table> t58(uinteger_t::_make_radix_table(uinteger_t::_radix_for(58), 4, 4 * digit_bits));
	std::unique_ptr<uinteger_t::radix_table> t255(uinteger_t::_make_radix_table(uinteger_t::_radix_for(255), 4, 4 * digit_bits));
	CHECK(t58->guard == 0, "base 58 guard");
	CHECK(t255->guard == 1, "base 255 guard");
	check_radix_tables();
	check_conversion_defaults();
}

// The digit splitters. radix::split points at the splitter of the base's chunk shape (k digits in
// pieces of h): one of the compile-time shapes in _pick_split, or _split_chunk_general. Each base
// is checked three ways: the pointer is the one its shape calls for (the list of shapes below is
// the test's own, apart from _pick_split's), the splitter it holds writes the k digits of the
// chunk that repeated division gives, and the general splitter agrees with it byte for byte, with
// guard bytes on both sides. The buffers are filled with 0xff, which is no digit (the largest
// base here is 255), so a digit the splitter does not write shows.

// The k digits of r (r < base^k), most significant first, by repeated division.
static std::vector<unsigned char> split_reference(std::uint64_t r, int base, unsigned k) {
	std::vector<unsigned char> d(k);
	for (unsigned i = k; i--;) {
		d[i] = static_cast<unsigned char>(r % static_cast<unsigned>(base));
		r /= static_cast<unsigned>(base);
	}
	return d;
}

static void check_split_chunk(const U::radix& rx, std::uint64_t r) {
	constexpr std::size_t guard = 8;
	unsigned char fixed[guard + 64 + guard], general[guard + 64 + guard];
	std::memset(fixed, 0xff, sizeof fixed);
	std::memset(general, 0xff, sizeof general);
	rx.split(rx, static_cast<digit>(r), fixed + guard);
	U::_split_chunk_general(rx, static_cast<digit>(r), general + guard);
	const auto ref = split_reference(r, rx.base, rx.k);
	const bool exact = std::memcmp(fixed + guard, ref.data(), rx.k) == 0;
	CHECK(exact, "split of %llu in base %d (k %u, h %u) differs from the digits by division", static_cast<unsigned long long>(r), rx.base, rx.k, rx.h);
	CHECK(std::memcmp(fixed, general, sizeof fixed) == 0, "split of %llu in base %d (k %u, h %u): the splitter and the general one differ (or a guard byte was written)", static_cast<unsigned long long>(r), rx.base, rx.k, rx.h);
	// A splitter that writes outside its k digits shows in the guards.
	bool guards = true;
	for (std::size_t i = 0; i < guard; ++i) {
		guards = guards && fixed[i] == 0xff && fixed[guard + rx.k + i] == 0xff && general[i] == 0xff && general[guard + rx.k + i] == 0xff;
	}
	CHECK(guards, "split of %llu in base %d wrote outside its k digits", static_cast<unsigned long long>(r), rx.base);
}

// The chunks r < D = base^k where the pieces and their divisions are tightest: 0, 1, 2, the base and
// its neighbours, every power of base^h (the piece boundaries) and its neighbours, the largest
// chunk D - 1 and D - 2, and for each boundary p the multiples m p + {0, 1, p / 2, p - 1} with m
// small and m at the top of the range.
static std::vector<std::uint64_t> split_edges(const U::radix& rx) {
	const std::uint64_t D = rx.D, bh = rx.bh, b = static_cast<std::uint64_t>(rx.base);
	std::vector<std::uint64_t> v;
	auto add = [&](std::uint64_t x) { if (x < D) v.push_back(x); };
	for (std::uint64_t x : {std::uint64_t(0), std::uint64_t(1), std::uint64_t(2), b - 1, b, b + 1, D - 1, D - 2, D - bh, D - bh - 1}) {
		add(x);
	}
	for (std::uint64_t p = bh;; p *= bh) {
		add(p - 1);
		add(p);
		add(p + 1);
		const std::uint64_t top = (D - 1) / p;
		for (std::uint64_t m : {std::uint64_t(1), std::uint64_t(2), top - 1, top}) {
			if (m == 0 || m > top) continue;
			for (std::uint64_t lo : {std::uint64_t(0), std::uint64_t(1), p / 2, p - 1}) add(m * p + lo);
		}
		if (p > (D - 1) / bh) break;
	}
	return v;
}

// A chunk below D made of three random pieces, each often 0, 1 or its largest value.
static std::uint64_t split_structured(const U::radix& rx) {
	const std::uint64_t D = rx.D, bh = rx.bh;
	auto pick = [](std::uint64_t n) {  // a value in [0, n]
		switch (rng() % 5) {
			case 0: return std::uint64_t(0);
			case 1: return std::uint64_t(1) < n ? std::uint64_t(1) : n;
			case 2: return n;
			default: return n == std::numeric_limits<std::uint64_t>::max() ? static_cast<std::uint64_t>(rng()) : static_cast<std::uint64_t>(rng() % (n + 1));
		}
	};
	const std::uint64_t top = pick((D - 1) / (bh * bh));
	std::uint64_t left = D - 1 - top * bh * bh;
	const std::uint64_t mid = pick(std::min(bh - 1, left / bh));
	left -= mid * bh;
	const std::uint64_t low = pick(std::min(bh - 1, left));
	return top * bh * bh + mid * bh + low;
}

// The checks of one base: the edges, then random chunks and chunks of random pieces.
static void check_split_base(const U::radix& rx, int draws) {
	for (std::uint64_t r : split_edges(rx)) check_split_chunk(rx, r);
	for (int i = 0; i < draws; ++i) {
		check_split_chunk(rx, static_cast<std::uint64_t>(rng()) % rx.D);
		check_split_chunk(rx, split_structured(rx));
	}
}

// The shapes of the fixed splitters, each against every base that has it.
static int split_covered[257];

template <unsigned K, unsigned H>
static void check_split_shape(int draws) {
	for (int base = 3; base <= 256; ++base) {
		if (is_power_of_two(base)) continue;
		const auto& rx = U::_radix_for(base);
		if (rx.k != K || rx.h != H) continue;
		++split_covered[base];
		void (*expect)(const U::radix&, digit, unsigned char*) = UINTEGER_T_SPLIT_SHAPES ? &U::_split_chunk_kh<K, H> : &U::_split_chunk_general;
		CHECK(rx.split == expect, "base %d, shape (%u, %u): radix::split is not the %s splitter", base, K, H, UINTEGER_T_SPLIT_SHAPES ? "fixed" : "general");
		check_split_base(rx, draws);
	}
}

static void check_split_shapes() {
	// The draws per base: every edge runs in light mode too.
	const int draws = light ? 200 : 5000;
	std::fill(std::begin(split_covered), std::end(split_covered), 0);
	check_split_shape<8, 4>(draws);
	check_split_shape<9, 4>(draws);
	check_split_shape<10, 5>(draws);
	check_split_shape<11, 5>(draws);
	check_split_shape<12, 6>(draws);
	check_split_shape<13, 6>(draws);
	check_split_shape<14, 7>(draws);
	check_split_shape<15, 7>(draws);
	check_split_shape<16, 8>(draws);
	check_split_shape<17, 8>(draws);
	check_split_shape<18, 9>(draws);
	check_split_shape<19, 9>(draws);
	check_split_shape<20, 10>(draws);
	check_split_shape<22, 11>(draws);
	check_split_shape<24, 12>(draws);
	check_split_shape<27, 13>(draws);
	check_split_shape<40, 20>(draws);
	int shaped = 0, other = 0;
	for (int base = 3; base <= 256; ++base) {
		if (is_power_of_two(base)) continue;
		const auto& rx = U::_radix_for(base);
		CHECK(split_covered[base] <= 1, "base %d is in two shapes", base);
		if (split_covered[base]) {
			++shaped;
			continue;
		}
		// Any other shape (every base at 32-bit limbs, where k = h) takes the general splitter.
		++other;
		CHECK(rx.split == &U::_split_chunk_general, "base %d (k %u, h %u) has no fixed shape but radix::split is not the general splitter", base, rx.k, rx.h);
		check_split_base(rx, draws);
	}
	CHECK(shaped + other == 247, "bases 3 to 256 that are not powers of two: %d", shaped + other);
	// At 64-bit limbs the 17 shapes cover all 247 bases (UINTEGER_T_SPLIT_SHAPES=0 only changes
	// which splitter they get, checked above).
	if (digit_bits == 64) {
		CHECK(shaped == 247 && other == 0, "64-bit limbs: %d bases in a fixed shape, %d not", shaped, other);
	}
}

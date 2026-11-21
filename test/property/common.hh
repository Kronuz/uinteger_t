// The shared state, the failure counter, 32-bit word helpers, the reference multiply and guard-limb buffers. Included by property.cc.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>
// Whether this build takes the header's default conversion cutoffs (check_conversion_defaults).
#if !defined UINTEGER_T_RADIX_MAX_BITS && !defined UINTEGER_T_DIGITS_TREE_THRESHOLD && !defined UINTEGER_T_FROM_DIGITS_TREE_THRESHOLD
#define PROPERTY_DEFAULT_CUTOFFS 1
#endif
// The size helpers, kernels and cutoffs are private; the size-guard and buffer checks below reach them directly.
#define UINTEGER_T_PUBLIC_IMPLEMENTATION
#include "uinteger_t.hh"

static std::mt19937_64 rng(20261030);
static int failures = 0;
// Light mode (a third argument other than 0; CMake passes it in Debug builds, where CI runs the
// sanitizers unoptimized): the sections whose cost does not depend on the iteration count run on a
// sample of their inputs. Every distinct check runs; the number of bases, the sizes far above a
// cutoff (the builds with tiny cutoffs reach those recursions at every size) and the repeated
// shapes are sampled.
static bool light = false;

#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); if (++failures > 10) std::exit(1); } } while (0)

using U = uinteger_t;
using digit = U::digit;
static constexpr std::size_t limb_words = sizeof(digit) / 4;

// Little-endian 32-bit words, trimmed.
using words = std::vector<std::uint32_t>;

static void trim(words& w) {
	while (!w.empty() && !w.back()) w.pop_back();
}

static words random_words(std::size_t n) {
	words w(n);
	int style = static_cast<int>(rng() % 6);
	for (auto& x : w) {
		auto r = static_cast<std::uint32_t>(rng());
		switch (style) {
			case 0: x = r; break;
			case 1: x = 0xffffffffu; break;
			case 2: x = rng() % 4 ? 0 : r; break;
			case 3: x = (rng() & 1) ? 0xffffffffu : 0; break;
			case 4: x = r >> (rng() % 32); break;
			default: x = r; break;
		}
	}
	if (n) w.back() |= 1u << (rng() % 32);
	return w;
}

static uinteger_t to_uint(const words& w) {
	std::string bytes;
	for (auto i = w.size(); i--;) {
		for (int s = 24; s >= 0; s -= 8) bytes.push_back(static_cast<char>(w[i] >> s));
	}
	return bytes.empty() ? uinteger_t(0) : uinteger_t(bytes, 256);
}

static words from_uint(const uinteger_t& u) {
	words w;
	if (!u) return w;
	auto bytes = u.str(256);
	std::uint32_t cur = 0;
	int shift = 0;
	for (auto i = bytes.size(); i--;) {
		cur |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << shift;
		shift += 8;
		if (shift == 32) { w.push_back(cur); cur = 0; shift = 0; }
	}
	if (shift) w.push_back(cur);
	trim(w);
	return w;
}

static words ref_mul(const words& a, const words& b) {
	words r(a.size() + b.size(), 0);
	for (std::size_t j = 0; j < b.size(); ++j) {
		std::uint64_t c = 0;
		for (std::size_t i = 0; i < a.size(); ++i) {
			std::uint64_t t = static_cast<std::uint64_t>(a[i]) * b[j] + r[i + j] + c;
			r[i + j] = static_cast<std::uint32_t>(t);
			c = t >> 32;
		}
		r[a.size() + j] = static_cast<std::uint32_t>(c);
	}
	trim(r);
	return r;
}

// Sizes in 32-bit words that hit the schoolbook, Karatsuba, odd-split and lopsided paths.
static std::size_t pick_words(std::size_t big) {
	switch (rng() % 5) {
		case 0: return 1 + rng() % 8;
		case 1: return 1 + rng() % 80;
		case 2: return 60 + rng() % 140;
		case 3: return 1 + rng() % big;
		default: return 1 + rng() % 300;
	}
}

static void check_mul(const words& a, const words& b) {
	auto ua = to_uint(a), ub = to_uint(b);
	auto expect = ref_mul(a, b);
	CHECK(from_uint(ua * ub) == expect, "mul %zu x %zu words", a.size(), b.size());
	CHECK(from_uint(ub * ua) == expect, "mul commuted %zu x %zu words", b.size(), a.size());
	auto acc = ua;
	acc *= ub;
	CHECK(from_uint(acc) == expect, "mul in place %zu x %zu words", a.size(), b.size());
}

// Squares: the same operand takes the squaring kernels, a copy of it the general multiply.
static void check_sqr(const words& a) {
	auto ua = to_uint(a);
	auto expect = ref_mul(a, a);
	CHECK(from_uint(ua * ua) == expect, "square %zu words", a.size());
	auto copy = ua;
	CHECK(from_uint(ua * copy) == expect, "square as product of copies %zu words", a.size());
	auto acc = ua;
	acc *= acc;
	CHECK(from_uint(acc) == expect, "square in place %zu words", a.size());
}

static std::vector<digit> random_limbs(std::size_t n) {
	std::vector<digit> limbs(n);
	if (sizeof(digit) < 4) {
		// Several limbs per word.
		const std::size_t per = sizeof(digit) < 4 ? 4 / sizeof(digit) : 1;
		auto w = random_words((n + per - 1) / per);
		for (std::size_t i = 0; i < n; ++i) limbs[i] = static_cast<digit>(w[i / per] >> (8 * sizeof(digit) * (i % per)));
		return limbs;
	}
	auto w = random_words(n * limb_words);
	for (std::size_t i = 0; i < n; ++i) {
		std::uint64_t x = 0;
		for (std::size_t j = 0; j < limb_words; ++j) x |= static_cast<std::uint64_t>(w[i * limb_words + j]) << (32 * j);
		limbs[i] = static_cast<digit>(x);
	}
	return limbs;
}

static words limbs_to_words(const digit* p, std::size_t n) {
	words w;
	if (sizeof(digit) < 4) {
		const std::size_t per = sizeof(digit) < 4 ? 4 / sizeof(digit) : 1;
		w.assign((n + per - 1) / per, 0);
		for (std::size_t i = 0; i < n; ++i) w[i / per] |= static_cast<std::uint32_t>(p[i]) << (8 * sizeof(digit) * (i % per));
		trim(w);
		return w;
	}
	for (std::size_t i = 0; i < n; ++i) {
		for (std::size_t j = 0; j < limb_words; ++j) w.push_back(static_cast<std::uint32_t>(static_cast<std::uint64_t>(p[i]) >> (32 * j)));
	}
	trim(w);
	return w;
}

// The raw kernels on dirty result and scratch buffers with a guard after each: a kernel that
// writes past its result, outgrows its scratch size or reads scratch it never wrote fails.
static const digit guard_pattern = static_cast<digit>(0x5a5a5a5a5a5a5a5aull);
static const std::size_t guard_limbs = 4;

static void dirty(std::vector<digit>& buf, std::size_t n) {
	for (std::size_t i = 0; i < n; ++i) buf[i] = static_cast<digit>(rng());
	for (std::size_t i = 0; i < guard_limbs; ++i) buf[n + i] = guard_pattern;
}

static bool guard_intact(const std::vector<digit>& buf, std::size_t n) {
	for (std::size_t i = 0; i < guard_limbs; ++i) if (buf[n + i] != guard_pattern) return false;
	return true;
}

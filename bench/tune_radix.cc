// Times the conversion kernels against the tree, to set where the tree takes over (the cutoffs in
// docs/design/base-conversion.md, The tree). For each size in bits it converts a random value in bases
// 10, 58 and 3 both ways, through the kernels (_digits_radix, _from_digit_horner) and through the tree
// (_digits_tree, _from_digit_tree), and prints microseconds per call and kernel time over tree time
// (below 1 the kernel is faster). The encoding kernel takes values up to UINTEGER_T_RADIX_MAX_BITS only,
// so build with that above the largest size:
//   c++ -std=c++17 -O2 -I. -DUINTEGER_T_RADIX_MAX_BITS=40960 bench/tune_radix.cc -o tune_radix
//   ./tune_radix 8704 10240 12288 14336 16384 18432 20480 24576 28672 32768 40960
// and add -DDIGIT_T=std::uint32_t -DHALF_DIGIT_T=std::uint16_t for 32-bit limbs.
#define UINTEGER_T_PUBLIC_IMPLEMENTATION
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
#include "uinteger_t.hh"

using U = uinteger_t;
using Clock = std::chrono::steady_clock;

// Best of five rounds of at least 15 ms each, in microseconds per call.
template <class F>
static double best_us(F f) {
	double best = 1e300;
	for (int r = 0; r < 5; ++r) {
		for (long n = 1;; n *= 2) {
			const auto s = Clock::now();
			for (long i = 0; i < n; ++i) f();
			const double us = std::chrono::duration<double, std::micro>(Clock::now() - s).count();
			if (us > 15000) {
				best = std::min(best, us / static_cast<double>(n));
				break;
			}
		}
	}
	return best;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s BITS...\n", argv[0]);
		return 2;
	}
	std::mt19937_64 g(1);
	std::printf("%7s %4s %12s %12s %8s %12s %12s %8s\n", "bits", "base", "encode_kern", "encode_tree", "ratio", "decode_horn", "decode_tree", "ratio");
	for (int i = 1; i < argc; ++i) {
		const std::size_t bits = std::strtoul(argv[i], nullptr, 10);
		if (bits > U::radix_max_bits) {
			std::fprintf(stderr, "%zu bits is above UINTEGER_T_RADIX_MAX_BITS (%zu); rebuild with it raised\n", bits, static_cast<std::size_t>(U::radix_max_bits));
			return 2;
		}
		std::string hex(bits / 4, '0');
		for (auto& c : hex) c = "0123456789abcdef"[g() & 15];
		hex[0] = '8';
		const U a(hex, 16);
		for (int base : {10, 58, 3}) {
			std::vector<unsigned char> kern, tree;
			U::_digits_radix(kern, base, a.data(), a.size());
			U::_digits_tree(tree, base, a.data(), a.size());
			const auto& rx = U::_radix_for(base);
			if (kern != tree || U::_from_digit_horner(kern.data(), kern.size(), base) != a ||
			    U::_from_digit_tree(rx, kern.data(), kern.size(), base) != a) {
				std::fprintf(stderr, "mismatch at %zu bits, base %d\n", bits, base);
				return 1;
			}
			const double ek = best_us([&] { std::vector<unsigned char> o; U::_digits_radix(o, base, a.data(), a.size()); });
			const double et = best_us([&] { std::vector<unsigned char> o; U::_digits_tree(o, base, a.data(), a.size()); });
			const double dk = best_us([&] { U v = U::_from_digit_horner(kern.data(), kern.size(), base); (void)v; });
			const double dt = best_us([&] { U v = U::_from_digit_tree(rx, kern.data(), kern.size(), base); (void)v; });
			std::printf("%7zu %4d %12.3f %12.3f %8.2f %12.3f %12.3f %8.2f\n", bits, base, ek, et, ek / et, dk, dt, dk / dt);
		}
	}
	return 0;
}

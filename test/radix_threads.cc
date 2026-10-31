// Concurrent first use of the digit kernels. Contexts and tables are built on first use and
// published with an atomic compare-and-swap; this races many threads onto bases and sizes none
// of them has used, so a missing happens-before edge shows up under ThreadSanitizer (CI runs
// it) and a torn table shows up as a wrong digit string anywhere.
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include "uinteger_t.hh"

struct Case {
	uinteger_t value;
	int base;
	std::string expect;  // digit bytes, from division only (the kernels are not involved)
};

static std::string reference(const uinteger_t& v, int base) {
	std::string d;
	const uinteger_t b(base);
	uinteger_t q = v;
	while (q) {
		d.push_back(static_cast<char>(static_cast<unsigned>(q % b)));
		q = q / b;
	}
	if (d.empty()) d.push_back('\0');
	return std::string(d.rbegin(), d.rend());
}

static uinteger_t make_value(unsigned bits, unsigned salt) {
	// A fixed pattern of the given bit length with a top bit set, the same at either limb width.
	uinteger_t v(1);
	std::uint32_t x = 0x9e3779b9u * (salt + 1);
	for (unsigned i = 1; i < bits; ++i) {
		x = x * 1664525u + 1013904223u;
		v = (v << 1) | uinteger_t((x >> 17) & 1);
	}
	return v;
}

int main() {
	std::vector<Case> cases;
	const unsigned limb_bits = static_cast<unsigned>(uinteger_t::digit_bits);
	for (int base : {3, 7, 10, 36, 58, 62, 100, 255}) {
		for (unsigned bits : {1u, 7u, 20u, limb_bits - 1, limb_bits, limb_bits + 1, 3 * limb_bits + 5, 5 * limb_bits, 8 * limb_bits, 8 * limb_bits + 1, 9 * limb_bits, 12 * limb_bits + 7, 40 * limb_bits}) {
			for (unsigned salt = 0; salt < 3; ++salt) {
				uinteger_t v = make_value(bits, salt);
				cases.push_back({v, base, reference(v, base)});
			}
		}
	}

	const unsigned nthreads = 8;
	std::atomic<bool> go(false);
	std::atomic<int> failures(0);
	std::vector<std::thread> threads;
	for (unsigned t = 0; t < nthreads; ++t) {
		threads.emplace_back([&, t] {
			while (!go.load(std::memory_order_acquire)) {
			}
			for (std::size_t i = 0; i < cases.size(); ++i) {
				// Every thread starts at a different case, so first uses overlap in many orders.
				const Case& c = cases[(i + t * 17) % cases.size()];
				std::string out;
				c.value.digits(out, c.base);
				if (out != c.expect || uinteger_t::from_digits(out.data(), out.size(), c.base) != c.value) {
					failures.fetch_add(1);
				}
			}
		});
	}
	go.store(true, std::memory_order_release);
	for (auto& th : threads) th.join();

	if (failures.load()) {
		std::printf("radix threads: %d failures\n", failures.load());
		return 1;
	}
	std::printf("radix threads OK (%zu cases, %u threads)\n", cases.size(), nthreads);
	return 0;
}

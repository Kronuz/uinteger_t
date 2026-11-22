// Base conversion tables: digit characters, base sizes, divisor magic, radix contexts and their lazy publication. Included by uinteger_t.hh.

#pragma once

#include "../uinteger_t.hh"

inline constexpr char uinteger_t::chr(int ord) {
	constexpr const char _[256] = {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b',
		'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
		'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
	};
	return _[ord];
}

inline constexpr int uinteger_t::ord(int chr) {
	constexpr const int _[256] = {
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,

		-1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
		25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1,
		-1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
		25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1,

		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	};
	return _[chr];
}

inline constexpr unsigned uinteger_t::base_bits(int base) {
	constexpr const unsigned _[256] = {
		0, 1, 0, 2, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 4,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6,

		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7,

		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8,
	};
	return _[base - 1];
}

inline constexpr unsigned uinteger_t::base_size(int base) {
	constexpr const unsigned _[256] = {
		0, 64, 41, 32, 28, 25, 23, 22, 21, 20, 19, 18, 18, 17, 17, 16,
		16, 16, 16, 15, 15, 15, 15, 14, 14, 14, 14, 14, 14, 14, 13, 13,
		13, 13, 13, 13, 13, 13, 13, 13, 12, 12, 12, 12, 12, 12, 12, 12,
		12, 12, 12, 12, 12, 12, 12, 12, 11, 11, 11, 11, 11, 11, 11, 11,

		11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
		11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
		10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
		10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,

		10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  9,  9,  9,  9,  9,  9,
		9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
		9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
		9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,

		9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
		9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
		9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
		9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  8,
	};
	return _[base - 1];
}

inline uinteger_t::digit uinteger_t::base_chunk(int base, unsigned& count) {
	const digit max = std::numeric_limits<digit>::max();
	digit chunk = 1;
	count = 0;
	while (chunk <= max / static_cast<digit>(base)) {
		chunk *= static_cast<digit>(base);
		++count;
	}
	return chunk;
}

template <typename Size>
inline Size uinteger_t::_limbs_for_digits(Size n, unsigned bits) {
	const auto max = static_cast<unsigned long long>(std::numeric_limits<Size>::max());
	if (static_cast<unsigned long long>(n) > (max - (digit_bits - 1)) / bits) {
		throw std::length_error("Error: Too many digits");
	}
	return static_cast<Size>((static_cast<unsigned long long>(n) * bits + digit_bits - 1) / digit_bits);
}

template <typename Size>
inline Size uinteger_t::_digits_for_limbs(Size n, unsigned top, unsigned bits) {
	const auto max = static_cast<unsigned long long>(std::numeric_limits<Size>::max());
	if (max < top + bits - 1 || static_cast<unsigned long long>(n) - 1 > (max - top - (bits - 1)) / digit_bits) {
		throw std::length_error("Error: Value too large to convert");
	}
	const auto nbits = (static_cast<unsigned long long>(n) - 1) * digit_bits + top;
	return static_cast<Size>((nbits + bits - 1) / bits);
}

inline std::uint64_t uinteger_t::_umulh(std::uint64_t x, std::uint64_t y) {
#if defined HAVE____INT128_T
	return static_cast<std::uint64_t>((static_cast<__uint128_t>(x) * y) >> 64);
#elif defined HAVE___UMUL128
	unsigned __int64 h;
	_umul128(static_cast<unsigned __int64>(x), static_cast<unsigned __int64>(y), &h);
	return static_cast<std::uint64_t>(h);
#elif defined HAVE___UMULH
	return static_cast<std::uint64_t>(__umulh(static_cast<unsigned __int64>(x), static_cast<unsigned __int64>(y)));
#else
	const std::uint64_t mask = 0xffffffffu;
	const std::uint64_t x0 = x & mask, x1 = x >> 32, y0 = y & mask, y1 = y >> 32;
	const std::uint64_t p00 = x0 * y0, p01 = x0 * y1, p10 = x1 * y0, p11 = x1 * y1;
	const std::uint64_t mid = (p00 >> 32) + (p01 & mask) + (p10 & mask);
	return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
#endif
}

inline std::uint64_t uinteger_t::_pow2_div(unsigned e, std::uint64_t d, std::uint64_t& hi, std::uint64_t& lo) {
	hi = 0;
	lo = 0;
	std::uint64_t rem = 0;
	for (unsigned i = e + 1; i--;) {
		const bool top = (rem >> 63) != 0;
		rem = (rem << 1) | (i == e ? 1 : 0);
		hi = (hi << 1) | (lo >> 63);
		lo <<= 1;
		if (top || rem >= d) {
			rem -= d;
			lo |= 1;
		}
	}
	return rem;
}

inline unsigned uinteger_t::_bit_length(std::uint64_t v) {
	unsigned n = 0;
	for (; v; v >>= 1) {
		++n;
	}
	return n;
}

inline uinteger_t::radix_div uinteger_t::_make_div(std::uint64_t d, unsigned nd) {
	radix_div v = {0, 0, false, false};
	const unsigned l = _bit_length(d);  // ceil(log2 d)
	std::uint64_t hi, lo;
	if (nd + l < 64 && nd < 64) {
		return v;
	}
	const unsigned e = (nd < 64 ? nd : 64) + l;
	if (_pow2_div(e, d, hi, lo)) {  // round up
		if (++lo == 0) {
			++hi;
		}
	}
	if (nd < 64) {
		if (!hi) {
			v.m = lo;
			v.s = e - 64;
			v.ok = true;
		}
	} else if (hi == 1) {
		v.m = lo;  // the multiplier is 2^64 + lo
		v.s = l - 1;
		v.wide = true;
		v.ok = true;
	}
	return v;
}

inline std::uint64_t uinteger_t::_div_magic(const radix_div& v, std::uint64_t r) {
	const std::uint64_t t = _umulh(v.m, r);
	return v.wide ? (t + ((r - t) >> 1)) >> v.s : t >> v.s;
}

inline uinteger_t::radix* uinteger_t::_make_radix(int base) {
	std::unique_ptr<radix> rx(new radix());
	rx->base = base;
	rx->D = base_chunk(base, rx->k);
	const std::uint64_t b = static_cast<std::uint64_t>(base);
	std::uint64_t bh = 1;
	unsigned h = 0;
	while (bh * b < (static_cast<std::uint64_t>(1) << 32)) {
		bh *= b;
		++h;
	}
	rx->h = h;
	rx->bh = bh;
	rx->b8 = (b * b * b * b) * (b * b * b * b);
	std::uint64_t power = 1;
	for (unsigned m = 1; m <= digit_bits && m <= h && m <= rx->k; ++m) {
		power *= b;
		std::uint64_t hi, lo;
		if (_pow2_div(64, power, hi, lo)) {
			++lo;
		}
		rx->cinv[m] = lo;
	}
	const std::uint64_t dmax = static_cast<std::uint64_t>(rx->D) - 1;
	rx->div0 = _make_div(bh, _bit_length(dmax));
	rx->div1 = _make_div(bh, _bit_length(dmax / bh));
	rx->split = _pick_split(rx->k, rx->h);
	if (base <= 64) {
		rx->pair_store.resize(static_cast<std::size_t>(2 * base * base));
		for (int i = 0; i < base * base; ++i) {
			rx->pair_store[2 * i] = static_cast<unsigned char>(i / base);
			rx->pair_store[2 * i + 1] = static_cast<unsigned char>(i % base);
		}
		rx->pair = rx->pair_store.data();
	} else {
		rx->pair = nullptr;
	}
	rx->E[0] = rx->D;
	rx->E[1] = rx->E[2] = rx->E[3] = 0;
	for (int r = 0; r < 3; ++r) {
		digit carry = 0;
		for (int i = 0; i < 4; ++i) {
			carry = _multadd(rx->E[i], rx->D, carry, 0, &rx->E[i]);
		}
	}
	for (auto& slot : rx->by_bits) {
		slot.store(nullptr, std::memory_order_relaxed);
	}
	for (auto& slot : rx->by_limbs) {
		slot.store(nullptr, std::memory_order_relaxed);
	}
	return rx.release();
}

inline uinteger_t::radix_table* uinteger_t::_make_radix_table(const radix& rx, std::size_t n, std::size_t bits, int guard, bool half) {
	unsigned K = 0;
	uinteger_t power(1);
	while (power.bits() <= bits) {
		power.mul_add_small(rx.D, 0);
		++K;
	}
	// Error budget in units of x: (n + 2) r^K for the initial product and R, plus r^m for the
	// limb dropped by each of the K - 1 passes, with r = D / B; g guard limbs divide it by B^g.
	double B = 1;
	for (unsigned i = 0; i < digit_bits; ++i) {
		B *= 2;
	}
	const double r = static_cast<double>(rx.D) / B;
	double power_r = 1, bound = 0;
	for (unsigned m = 1; m < K; ++m) {
		power_r *= r;
		bound += power_r;
	}
	bound += static_cast<double>(n + 2) * power_r * r;
	const unsigned g = guard >= 0 ? static_cast<unsigned>(guard) : (bound < 0.45 ? 0 : 1);
	assert(guard >= 0 || bound / (g ? B : 1) < 0.45);
	const std::size_t L = K + g;
	uinteger_t a;
	a.resize(L + n + 1, 0);
	a.data()[L + n] = 1;
	a /= power;
	const std::size_t an = a.size();
	assert(an <= L + 1);
	const std::size_t rl = an > L ? an : L;
	std::unique_ptr<radix_table> t(new radix_table());
	t->chunks = K;
	t->guard = g;
	t->fraction = static_cast<unsigned>(L);
	t->rlen = static_cast<unsigned>(rl);
	t->store.assign(4 + rl + 8 + rl, 0);
	digit* R = t->store.data() + 4;
	std::copy(a.data(), a.data() + an, R);
	digit* R2 = R + rl + 8;
	if (half) {
		for (std::size_t l = 0; l < rl; ++l) {
			R2[l] = (R[l] >> 1) | (l + 1 < rl ? static_cast<digit>(R[l + 1] << (digit_bits - 1)) : 0);
		}
	}
	t->R = R;
	t->R2 = R2;
	return t.release();
}

template <typename T>
inline const T* uinteger_t::_publish(std::atomic<const T*>& slot, T* fresh) {
	std::unique_ptr<T> mine(fresh);
	const T* expected = nullptr;
	if (slot.compare_exchange_strong(expected, fresh, std::memory_order_acq_rel, std::memory_order_acquire)) {
		mine.release();
		return fresh;
	}
	return expected;
}

inline const uinteger_t::radix& uinteger_t::_radix_for(int base) {
	static std::atomic<const radix*> contexts[257];
	std::atomic<const radix*>& slot = contexts[base];
	const radix* rx = slot.load(std::memory_order_acquire);
	if (!rx) {
		rx = _publish(slot, _make_radix(base));
	}
	return *rx;
}

inline const uinteger_t::radix_table& uinteger_t::_table_for(const radix& rx, std::size_t n, std::size_t bits) {
	const bool by_bits = n <= radix_unrolled;
	std::atomic<const radix_table*>& slot = by_bits ? rx.by_bits[bits] : rx.by_limbs[n];
	const radix_table* t = slot.load(std::memory_order_acquire);
	if (!t) {
		t = _publish(slot, _make_radix_table(rx, n, by_bits ? bits : n * digit_bits));
	}
	return *t;
}

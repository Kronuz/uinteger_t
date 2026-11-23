// Base conversion in: byte loads and the Horner decoder. Included by uinteger_t.hh.

#pragma once

#include "../uinteger_t.hh"

inline uinteger_t::digit uinteger_t::_load_be(const unsigned char* p) {
	digit v = 0;
	for (std::size_t i = 0; i < digit_octets; ++i) {
		v = static_cast<digit>((v << 8) | p[i]);
	}
	return v;
}

inline std::uint64_t uinteger_t::_swar8(const unsigned char* p, std::uint64_t b, std::uint64_t b2, std::uint64_t b4) {
	std::uint64_t v;
	std::memcpy(&v, p, 8);
#if defined __BYTE_ORDER__ && defined __ORDER_BIG_ENDIAN__ && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	v = __builtin_bswap64(v);
#endif
	v = (v & 0x00ff00ff00ff00ffull) * b + ((v >> 8) & 0x00ff00ff00ff00ffull);
	v = (v & 0x0000ffff0000ffffull) * b2 + ((v >> 16) & 0x0000ffff0000ffffull);
	return (v & 0x00000000ffffffffull) * b4 + (v >> 32);
}

inline std::uint64_t uinteger_t::_chunk_value(const radix& rx, const unsigned char* p, unsigned count, std::uint64_t b8) {
	const std::uint64_t b = static_cast<std::uint64_t>(rx.base);
	std::uint64_t acc = 0;
	unsigned i = 0;
	if (count >= 8) {
		const std::uint64_t b2 = b * b;
		acc = _swar8(p, b, b2, b2 * b2);
		for (i = 8; i + 8 <= count; i += 8) {
			acc = acc * b8 + _swar8(p + i, b, b2, b2 * b2);
		}
	}
	for (; i < count; ++i) {
		acc = acc * b + p[i];
	}
	return acc;
}

inline std::size_t uinteger_t::_horner_limbs(const radix& rx, const unsigned char* d, std::size_t m, digit* x) {
	const unsigned k = rx.k;
	std::size_t first = m % k;
	if (!first) {
		first = k;
	}
	const std::size_t chunks = (m - first) / k + 1;
	const std::uint64_t b8 = rx.b8;
	x[0] = static_cast<digit>(_chunk_value(rx, d, static_cast<unsigned>(first), b8));
	const unsigned char* p = d + first;
	for (std::size_t j = 1; j < chunks; ++j, p += k) {
		x[j] = static_cast<digit>(_chunk_value(rx, p, k, b8));
	}
	const digit D = rx.D;
	std::size_t n = 0, j = 0;
	for (; (chunks - j) % 4; ++j) {
		digit carry = x[j];
		for (std::size_t i = 0; i < n; ++i) {
			carry = _multadd(x[i], D, carry, 0, &x[i]);
		}
		if (carry) {
			x[n++] = carry;
		}
	}
	for (; j < chunks; j += 4) {
		// a = ((c0 D + c1) D + c2) D + c3 as four limbs, then x = x D^4 + a.
		digit a0 = x[j], a1, a2, a3, carry;
		a1 = _multadd(a0, D, x[j + 1], 0, &a0);
		carry = _multadd(a0, D, x[j + 2], 0, &a0);
		a2 = _multadd(a1, D, carry, 0, &a1);
		carry = _multadd(a0, D, x[j + 3], 0, &a0);
		carry = _multadd(a1, D, carry, 0, &a1);
		a3 = _multadd(a2, D, carry, 0, &a2);
		for (std::size_t i = 0; i < n; ++i) {
			const digit xi = x[i];
			digit l0, l1, l2, l3;
			digit h = _multadd(xi, rx.E[0], a0, 0, &l0);
			h = _multadd(xi, rx.E[1], a1, h, &l1);
			h = _multadd(xi, rx.E[2], a2, h, &l2);
			h = _multadd(xi, rx.E[3], a3, h, &l3);
			x[i] = l0;
			a0 = l1;
			a1 = l2;
			a2 = l3;
			a3 = h;
		}
		x[n] = a0;
		x[n + 1] = a1;
		x[n + 2] = a2;
		x[n + 3] = a3;
		n += 4;
		while (n && !x[n - 1]) {
			--n;
		}
	}
	return n;
}

inline uinteger_t uinteger_t::_from_digit_horner(const unsigned char* d, std::size_t m, int base) {
	const radix& rx = _radix_for(base);
	const std::size_t chunks = m / rx.k + (m % rx.k != 0);
	uinteger_t result;
	if (chunks <= horner_stack_chunks) {
		digit x[horner_stack_chunks];
		const std::size_t n = _horner_limbs(rx, d, m, x);
		result._value.resize_uninit(n);
		std::copy(x, x + n, result._value.data());
	} else {
		result._value.resize(chunks);
		result._value.resize(_horner_limbs(rx, d, m, result._value.data()));
	}
	return result;
}

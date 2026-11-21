// Base conversion: byte stores, digit output and the Horner decoder. Included inside class uinteger_t by uinteger_t.hh.

	// The digit_octets bytes at p, first byte most significant, as a limb.
	static digit _load_be(const unsigned char* p) {
		digit v = 0;
		for (std::size_t i = 0; i < digit_octets; ++i) {
			v = static_cast<digit>((v << 8) | p[i]);
		}
		return v;
	}

	// Writes a limb to the digit_octets bytes at p, most significant byte first.
	static void _store_be(unsigned char* p, digit v) {
		for (std::size_t i = digit_octets; i--; v = static_cast<digit>(v >> 8)) {
			p[i] = static_cast<unsigned char>(v);
		}
	}

	// Writes the `count` low-order bytes of the n >= 1 limbs at p to dst, most significant first;
	// count is (n - 1) * digit_octets plus the bytes of the top limb, which is nonzero.
	static void _store_bytes(unsigned char* dst, const digit* p, std::size_t n, std::size_t count) {
		const std::size_t top = count - (n - 1) * digit_octets;
		for (std::size_t j = 0; j < top; ++j) {
			dst[j] = static_cast<unsigned char>(p[n - 1] >> (8 * (top - 1 - j)));
		}
		dst += top;
		for (std::size_t i = n - 1; i--; dst += digit_octets) {
			_store_be(dst, p[i]);
		}
	}

	// Appends the digits of the value to `out`: one-byte results are written in place.
	template <typename Result, typename Fill>
	static void _digits_append(Result& out, std::size_t cap, Fill fill, std::true_type) {
		const auto start = out.size();
		out.resize(start + cap);
		std::size_t count;
		try {
			count = fill(reinterpret_cast<unsigned char*>(&out[start]));
		} catch (...) {
			out.resize(start);  // the tree allocates; leave `out` as it was
			throw;
		}
		out.resize(start + count);
	}

	template <typename Result, typename Fill>
	static void _digits_append(Result& out, std::size_t cap, Fill fill, std::false_type) {
		using value_type = typename Result::value_type;
		std::vector<unsigned char> buffer(cap);
		const std::size_t count = fill(buffer.data());
		for (std::size_t i = 0; i < count; ++i) {
			out.push_back(static_cast<value_type>(buffer[i]));
		}
	}

	template <typename Result>
	static void _digits_radix(Result& out, int base, const digit* p, std::size_t n) {
		using value_type = typename Result::value_type;
		using in_place = std::integral_constant<bool, std::is_integral<value_type>::value && sizeof(value_type) == 1 && !std::is_same<value_type, bool>::value>;
		const radix& rx = _radix_for(base);
		const std::size_t bits = (n - 1) * digit_bits + static_cast<std::size_t>(_bits(p[n - 1]));
		const radix_table& t = _table_for(rx, n, bits);
		const std::size_t cap = static_cast<std::size_t>(t.chunks) * rx.k;
		if (cap > out.max_size() - out.size()) {
			throw std::length_error("Error: Value too large to convert");
		}
		_digits_append(out, cap, [&](unsigned char* buffer) { return _encode_radix(rx, t, p, n, buffer); }, in_place());
	}

	// Reads eight digit bytes (first byte most significant) as one number below base^8 with
	// three multiplies; the digits are below `base`, so no lane overflows.
	static std::uint64_t _swar8(const unsigned char* p, std::uint64_t b, std::uint64_t b2, std::uint64_t b4) {
		std::uint64_t v;
		std::memcpy(&v, p, 8);
	#if defined __BYTE_ORDER__ && defined __ORDER_BIG_ENDIAN__ && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		v = __builtin_bswap64(v);
	#endif
		v = (v & 0x00ff00ff00ff00ffull) * b + ((v >> 8) & 0x00ff00ff00ff00ffull);
		v = (v & 0x0000ffff0000ffffull) * b2 + ((v >> 16) & 0x0000ffff0000ffffull);
		return (v & 0x00000000ffffffffull) * b4 + (v >> 32);
	}

	// One chunk of `count` digits as a number below base^count.
	static std::uint64_t _chunk_value(const radix& rx, const unsigned char* p, unsigned count, std::uint64_t b8) {
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

	// Writes the value of m >= 1 digits d[0..m), each below `base`, most significant first, to
	// x and returns its limb count. x holds chunks = ceil(m / k) limbs and nothing beyond
	// them is touched, which is what lets a short value live on the stack (L1). The chunks go
	// into x itself, and Horner step j reads chunk j and leaves at most j limbs below it, so
	// nothing unread is overwritten: the value of the first j chunks is below D^j < B^j, so
	// n <= j on entry to step j; the first steps write x[n] <= x[j]; a four-chunk step at j
	// (four chunks remain, j + 3 < chunks) reads x[j .. j + 3] before it writes x[n .. n + 3]
	// with n + 3 <= j + 3. The result has n <= chunks limbs.
	static std::size_t _horner_limbs(const radix& rx, const unsigned char* d, std::size_t m, digit* x) {
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

	// Up to this many chunks (a 64-byte key in base 58 is nine) the decoder works in a stack
	// buffer and sizes the result to the limbs the value has, so a short value is never
	// allocated larger than itself (an inline buffer holds it); longer ones work in the result.
	static constexpr std::size_t horner_stack_chunks = 16;

	// The value of m >= 1 digits d[0..m), each below `base`, most significant first.
	static uinteger_t _from_digit_horner(const unsigned char* d, std::size_t m, int base) {
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

// Value helpers (trim, digit characters, base tables) and the bitwise and shift operations. Included inside class uinteger_t by uinteger_t.hh.

	// Helper functions

	void trim(digit mask = 0) {
		auto n = _value.size();
		digit* p = _value.data();

		// Masks the last value of internal vector
		mask &= (digit_bits - 1);
		if (mask && n) {
			p[n - 1] &= (static_cast<digit>(1) << mask) - 1;
		}

		// Removes all unused zeros from the internal vector
		auto m = n;
		while (m && !p[m - 1]) {
			--m;
		}
		if (m != n) {
			_value.resize(m); // shrink
		}
	}

	// Sizes the value to `n` limbs without initializing new ones: the caller writes every limb
	// it adds (and no operand limb is read from the new range) before anything reads it.
	void resize_uninit(std::size_t n) {
		_value.resize_uninit(n);
	}

	static constexpr char chr(int ord) {
		constexpr const char _[256] = {
			'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b',
			'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
			'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
		};
		return _[ord];
	}

	static constexpr int ord(int chr) {
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

public:
	// The number of bits in a digit of `base` when `base` is a power of two from 2 to 256 (1 to 8),
	// else 0; `base` must be in [1, 256]. These bases convert by slicing bits. base-x sizes the
	// blocks of its padded alphabets with it.
	static constexpr unsigned base_bits(int base) {
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

	// The number of digits of `base` that write the largest 64-bit value (64 for base 2, 20 for base
	// 10, 8 for base 256), a table over [1, 256] (0 for base 1). `digits()` sizes its output with it.
	static constexpr unsigned base_size(int base) {
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

	static const uinteger_t& uint_0() {
		static uinteger_t uint_0(0);
		return uint_0;
	}

	static const uinteger_t& uint_1() {
		static uinteger_t uint_1(1);
		return uint_1;
	}

UINTEGER_T_PRIVATE:
	// Operator implementations
	static uinteger_t& bitwise_and(uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		if (lhs_sz > rhs_sz) {
			lhs.resize(rhs_sz); // shrink
		}

		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs.end();

		auto rhs_it = rhs.begin();

		for (; lhs_it != lhs_it_e; ++lhs_it, ++rhs_it) {
			*lhs_it &= *rhs_it;
		}

		// Finish up
		lhs.trim();
		return lhs;
	}

	static uinteger_t& bitwise_and(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		auto result_sz = std::max(lhs_sz, rhs_sz);
		result.resize(result_sz);

		// not using `end()` because resize of `result.resize()` could have
		// resized `lhs` or `rhs` if `result` is also either `rhs` or `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		auto rhs_it = rhs.begin();
		auto rhs_it_e = rhs_it + rhs_sz;

		auto it = result.begin();

		if (lhs_sz < rhs_sz) {
			for (; lhs_it != lhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				*it = *lhs_it & *rhs_it;
			}
			for (; rhs_it != rhs_it_e; ++rhs_it, ++it) {
				*it = 0;
			}
		} else {
			for (; rhs_it != rhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				*it = *lhs_it & *rhs_it;
			}
			for (; lhs_it != lhs_it_e; ++lhs_it, ++it) {
				*it = 0;
			}
		}

		// Finish up
		result.trim();
		return result;
	}

	static uinteger_t bitwise_and(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		bitwise_and(result, lhs, rhs);
		return result;
	}

	static uinteger_t& bitwise_or(uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		if (lhs_sz < rhs_sz) {
			lhs.resize(rhs_sz, 0); // grow
		}

		auto lhs_it = lhs.begin();

		auto rhs_it = rhs.begin();
		auto rhs_it_e = rhs.end();

		for (; rhs_it != rhs_it_e; ++lhs_it, ++rhs_it) {
			*lhs_it |= *rhs_it;
		}

		// Finish up
		lhs.trim();
		return lhs;
	}

	static uinteger_t& bitwise_or(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		auto result_sz = std::max(lhs_sz, rhs_sz);
		result.resize(result_sz);

		// not using `end()` because resize of `result.resize()` could have
		// resized `lhs` or `rhs` if `result` is also either `rhs` or `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		auto rhs_it = rhs.begin();
		auto rhs_it_e = rhs_it + rhs_sz;

		auto it = result.begin();

		if (lhs_sz < rhs_sz) {
			for (; lhs_it != lhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				*it = *lhs_it | *rhs_it;
			}
			for (; rhs_it != rhs_it_e; ++rhs_it, ++it) {
				*it = *rhs_it;
			}
		} else {
			for (; rhs_it != rhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				*it = *lhs_it | *rhs_it;
			}
			for (; lhs_it != lhs_it_e; ++lhs_it, ++it) {
				*it = *lhs_it;
			}
		}

		// Finish up
		result.trim();
		return result;
	}
	static uinteger_t bitwise_or(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		bitwise_or(result, lhs, rhs);
		return result;
	}

	static uinteger_t& bitwise_xor(uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		if (lhs_sz < rhs_sz) {
			lhs.resize(rhs_sz, 0); // grow
		}

		auto lhs_it = lhs.begin();

		auto rhs_it = rhs.begin();
		auto rhs_it_e = rhs.end();

		for (; rhs_it != rhs_it_e; ++lhs_it, ++rhs_it) {
			*lhs_it ^= *rhs_it;
		}

		// Finish up
		lhs.trim();
		return lhs;
	}

	static uinteger_t& bitwise_xor(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		auto lhs_sz = lhs.size();
		auto rhs_sz = rhs.size();

		auto result_sz = std::max(lhs_sz, rhs_sz);
		result.resize(result_sz);

		// not using `end()` because resize of `result.resize()` could have
		// resized `lhs` or `rhs` if `result` is also either `rhs` or `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		auto rhs_it = rhs.begin();
		auto rhs_it_e = rhs_it + rhs_sz;

		auto it = result.begin();

		if (lhs_sz < rhs_sz) {
			for (; lhs_it != lhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				*it = *lhs_it ^ *rhs_it;
			}
			for (; rhs_it != rhs_it_e; ++rhs_it, ++it) {
				*it = *rhs_it;
			}
		} else {
			for (; rhs_it != rhs_it_e; ++lhs_it, ++rhs_it, ++it) {
				*it = *lhs_it ^ *rhs_it;
			}
			for (; lhs_it != lhs_it_e; ++lhs_it, ++it) {
				*it = *lhs_it;
			}
		}

		// Finish up
		result.trim();
		return result;
	}

	static uinteger_t bitwise_xor(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		bitwise_xor(result, lhs, rhs);
		return result;
	}

	static uinteger_t& bitwise_inv(uinteger_t& lhs) {
		auto lhs_sz = lhs.size();

		auto b = lhs.bits();

		if (!lhs_sz) {
			lhs.append(0);
		}

		// not using `end()` because resize of `result.resize()` could have
		// resized `lhs` if `result` is also `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		for (; lhs_it != lhs_it_e; ++lhs_it) {
			*lhs_it = ~*lhs_it;
		}

		// Finish up
		lhs.trim(b ? b : 1);
		return lhs;
	}

	static uinteger_t& bitwise_inv(uinteger_t& result, const uinteger_t& lhs) {
		auto lhs_sz = lhs.size();

		auto b = lhs.bits();

		auto result_sz = lhs_sz ? lhs_sz : 1;
		result.resize(result_sz);

		// not using `end()` because resize of `result.resize()` could have
		// resized `lhs` if `result` is also `lhs`.
		auto lhs_it = lhs.begin();
		auto lhs_it_e = lhs_it + lhs_sz;

		auto it = result.begin();
		auto it_e = it + result_sz;

		for (; lhs_it != lhs_it_e; ++lhs_it, ++it) {
			*it = ~*lhs_it;
		}
		for (; it != it_e; ++it) {
			*it = ~static_cast<digit>(0);
		}

		// Finish up
		result.trim(b ? b : 1);
		return result;
	}

	static uinteger_t bitwise_inv(const uinteger_t& lhs) {
		uinteger_t result;
		bitwise_inv(result, lhs);
		return result;
	}

	// Shifts by `n` bits. The limb count is `n / digit_bits` and the bit count `n % digit_bits`
	// (digit_bits is a power of two), so there is no division and no temporary. `result` may be
	// `lhs`: the kernels move limbs in the direction that reads each one before it is overwritten.
	static uinteger_t& lshift(uinteger_t& result, const uinteger_t& lhs, unsigned long long n) {
		const std::size_t an = lhs.size();
		if (!an) {
			result.clear();
			return result;
		}
		if (!n) {
			if (&result != &lhs) {
				result = lhs;
			}
			return result;
		}
		const unsigned long long limbs = n / digit_bits;
		const unsigned bits = static_cast<unsigned>(n % digit_bits);
		const auto max = static_cast<unsigned long long>(result._value.max_size());
		if (limbs >= max - an) {
			throw std::length_error("Error: Shift too large");
		}
		// A bit shift needs one more limb exactly when the top limb's bits overflow it.
		const std::size_t extra = bits && _bits(lhs.back()) + bits > digit_bits;
		const std::size_t rn = an + static_cast<std::size_t>(limbs) + extra;

		result.resize_uninit(rn);
		digit* r = result.data();
		const digit* a = lhs.data();
		if (bits) {
			const digit out = k_lshift(r + limbs, a, an, bits);
			if (extra) {
				r[rn - 1] = out;
			}
		} else {
			std::copy_backward(a, a + an, r + rn);
		}
		std::fill_n(r, limbs, digit(0));

		// Finish up
		result.trim();
		return result;
	}

	// Shifts right by `n` bits, discarding the bits shifted out.
	static uinteger_t& rshift(uinteger_t& result, const uinteger_t& lhs, unsigned long long n) {
		const std::size_t an = lhs.size();
		const unsigned long long limbs = n / digit_bits;
		if (limbs >= an) {
			result.clear();
			return result;
		}
		if (!n) {
			if (&result != &lhs) {
				result = lhs;
			}
			return result;
		}
		const unsigned bits = static_cast<unsigned>(n % digit_bits);
		const std::size_t rn = an - static_cast<std::size_t>(limbs);

		if (&result != &lhs) {
			result.resize_uninit(rn);
		}
		digit* r = result.data();
		const digit* a = lhs.data() + limbs;
		if (bits) {
			k_rshift(r, a, rn, bits);
		} else {
			std::copy(a, a + rn, r);
		}
		if (&result == &lhs) {
			result.resize(rn); // shrink
		}

		// Finish up
		result.trim();
		return result;
	}

	// The count held in a uinteger_t, when it fits 64 bits (up to 64 / digit_bits limbs). A longer
	// count is 2^64 bits or more, beyond any size a value can have: a left shift of a nonzero
	// value by it cannot be represented and a right shift gives zero.
	static bool shift_count(const uinteger_t& rhs, unsigned long long& n) {
		if (rhs.size() > 64 / digit_bits) {
			return false;
		}
		n = 0;
		for (std::size_t i = 0; i < rhs.size(); ++i) {
			n |= static_cast<unsigned long long>(rhs.data()[i]) << (digit_bits * i);
		}
		return true;
	}

	static uinteger_t& bitwise_lshift(uinteger_t& lhs, const uinteger_t& rhs) {
		return bitwise_lshift(lhs, lhs, rhs);
	}

	static uinteger_t& bitwise_lshift(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		unsigned long long n = 0;
		if (!shift_count(rhs, n)) {
			if (lhs) {
				throw std::length_error("Error: Shift too large");
			}
			result.clear();
			return result;
		}
		return lshift(result, lhs, n);
	}

	static uinteger_t bitwise_lshift(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		bitwise_lshift(result, lhs, rhs);
		return result;
	}

	static uinteger_t& bitwise_rshift(uinteger_t& lhs, const uinteger_t& rhs) {
		return bitwise_rshift(lhs, lhs, rhs);
	}

	static uinteger_t& bitwise_rshift(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		unsigned long long n = 0;
		if (!shift_count(rhs, n)) {
			result.clear();
			return result;
		}
		return rshift(result, lhs, n);
	}

	static uinteger_t bitwise_rshift(const uinteger_t& lhs, const uinteger_t& rhs) {
		uinteger_t result;
		bitwise_rshift(result, lhs, rhs);
		return result;
	}

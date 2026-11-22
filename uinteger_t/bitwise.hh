// Bitwise operations and shifts on values. Included by uinteger_t.hh.

#pragma once

#include "../uinteger_t.hh"

inline uinteger_t& uinteger_t::bitwise_and(uinteger_t& lhs, const uinteger_t& rhs) {
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

inline uinteger_t& uinteger_t::bitwise_and(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
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

inline uinteger_t uinteger_t::bitwise_and(const uinteger_t& lhs, const uinteger_t& rhs) {
	uinteger_t result;
	bitwise_and(result, lhs, rhs);
	return result;
}

inline uinteger_t& uinteger_t::bitwise_or(uinteger_t& lhs, const uinteger_t& rhs) {
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

inline uinteger_t& uinteger_t::bitwise_or(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
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

inline uinteger_t uinteger_t::bitwise_or(const uinteger_t& lhs, const uinteger_t& rhs) {
	uinteger_t result;
	bitwise_or(result, lhs, rhs);
	return result;
}

inline uinteger_t& uinteger_t::bitwise_xor(uinteger_t& lhs, const uinteger_t& rhs) {
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

inline uinteger_t& uinteger_t::bitwise_xor(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
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

inline uinteger_t uinteger_t::bitwise_xor(const uinteger_t& lhs, const uinteger_t& rhs) {
	uinteger_t result;
	bitwise_xor(result, lhs, rhs);
	return result;
}

inline uinteger_t& uinteger_t::bitwise_inv(uinteger_t& lhs) {
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

inline uinteger_t& uinteger_t::bitwise_inv(uinteger_t& result, const uinteger_t& lhs) {
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

inline uinteger_t uinteger_t::bitwise_inv(const uinteger_t& lhs) {
	uinteger_t result;
	bitwise_inv(result, lhs);
	return result;
}

inline uinteger_t& uinteger_t::lshift(uinteger_t& result, const uinteger_t& lhs, unsigned long long n) {
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

inline uinteger_t& uinteger_t::rshift(uinteger_t& result, const uinteger_t& lhs, unsigned long long n) {
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

inline bool uinteger_t::shift_count(const uinteger_t& rhs, unsigned long long& n) {
	if (rhs.size() > 64 / digit_bits) {
		return false;
	}
	n = 0;
	for (std::size_t i = 0; i < rhs.size(); ++i) {
		n |= static_cast<unsigned long long>(rhs.data()[i]) << (digit_bits * i);
	}
	return true;
}

inline uinteger_t& uinteger_t::bitwise_lshift(uinteger_t& lhs, const uinteger_t& rhs) {
	return bitwise_lshift(lhs, lhs, rhs);
}

inline uinteger_t& uinteger_t::bitwise_lshift(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
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

inline uinteger_t uinteger_t::bitwise_lshift(const uinteger_t& lhs, const uinteger_t& rhs) {
	uinteger_t result;
	bitwise_lshift(result, lhs, rhs);
	return result;
}

inline uinteger_t& uinteger_t::bitwise_rshift(uinteger_t& lhs, const uinteger_t& rhs) {
	return bitwise_rshift(lhs, lhs, rhs);
}

inline uinteger_t& uinteger_t::bitwise_rshift(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
	unsigned long long n = 0;
	if (!shift_count(rhs, n)) {
		result.clear();
		return result;
	}
	return rshift(result, lhs, n);
}

inline uinteger_t uinteger_t::bitwise_rshift(const uinteger_t& lhs, const uinteger_t& rhs) {
	uinteger_t result;
	bitwise_rshift(result, lhs, rhs);
	return result;
}

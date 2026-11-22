// Subquadratic base conversion: the power tree, the tree encoder and the from-digits decoder. Included by uinteger_t.hh.

#pragma once

#include "../uinteger_t.hh"

inline void uinteger_t::_tree_shape(const radix& rx, std::size_t chunks, std::size_t leaf_limbs, unsigned& levels, unsigned& leaf_chunks) {
	const std::size_t longest = std::max<std::size_t>(2, leaf_limbs * digit_bits / _bit_length(rx.D));
	unsigned j = 1;
	std::size_t t = (chunks + 1) / 2;
	while (t > longest) {
		++j;
		t = (chunks + (static_cast<std::size_t>(1) << j) - 1) >> j;
	}
	levels = j;
	leaf_chunks = static_cast<unsigned>(std::max<std::size_t>(t, 2));
}

inline std::size_t uinteger_t::_tree_pow(digit D, unsigned e, digit* out, digit* tmp) {
	digit* cur = out;
	digit* next = tmp;
	cur[0] = D;
	std::size_t n = 1;
	unsigned bit = 0;
	while (e >> (bit + 1)) {
		++bit;
	}
	while (bit--) {
		{
			k_scratch scratch(k_sqr_scratch_size(n));
			k_sqr(next, cur, n, scratch.data());
		}
		n *= 2;
		if (!next[n - 1]) {
			--n;
		}
		std::swap(cur, next);
		if ((e >> bit) & 1) {
			const digit carry = k_mul_1(cur, cur, n, D);
			if (carry) {
				cur[n++] = carry;
			}
		}
	}
	if (cur != out) {
		std::copy(cur, cur + n, out);
	}
	return n;
}

inline void uinteger_t::_tree_build(const radix& rx, radix_tree& tr, unsigned levels, unsigned chunks, bool normalize) {
	assert(levels >= 1 && levels < radix_tree_max_levels && chunks >= 2);
	const std::size_t cap0 = (static_cast<std::size_t>(chunks) * _bit_length(rx.D) + digit_bits - 1) / digit_bits + 2;
	tr.levels = levels;
	tr.chunks = chunks;
	tr.leaf_digits = static_cast<std::size_t>(chunks) * rx.k;
	// Region j holds cap0 2^j limbs, which P_j fits.
	tr.store.reset(new digit[cap0 * ((static_cast<std::size_t>(1) << levels) - 1)]);
	std::size_t offset = 0;
	for (unsigned j = 0; j < levels; ++j) {
		tr.power[j] = tr.store.get() + offset;
		offset += cap0 << j;
	}
	{
		std::unique_ptr<digit[]> tmp(new digit[cap0]);
		tr.size[0] = _tree_pow(rx.D, chunks, tr.power[0], tmp.get());
	}
	for (unsigned j = 0; j < levels; ++j) {
		if (j + 1 < levels) {
			const std::size_t n = tr.size[j];
			{
				k_scratch scratch(k_sqr_scratch_size(n));
				k_sqr(tr.power[j + 1], tr.power[j], n, scratch.data());
			}
			tr.size[j + 1] = tr.power[j + 1][2 * n - 1] ? 2 * n : 2 * n - 1;
		}
		tr.shift[j] = 0;
		if (normalize) {
			const unsigned sh = static_cast<unsigned>(digit_bits - _bits(tr.power[j][tr.size[j] - 1]));
			if (sh) {
				k_lshift(tr.power[j], tr.power[j], tr.size[j], sh);
			}
			tr.shift[j] = sh;
		}
	}
}

inline void uinteger_t::_tree_leaf(const radix& rx, const radix_tree& tr, const radix_leaf& lf, const digit* x, std::size_t nx, unsigned char* out) {
	if (nx > lf.limbs) {
		throw std::logic_error("Error: Digit tree leaf out of range");
	}
	const digit* in = x;
	if (nx < lf.limbs) {
		std::copy(x, x + nx, lf.pad.get());
		std::fill(lf.pad.get() + nx, lf.pad.get() + lf.limbs, digit(0));
		in = lf.pad.get();
	}
	const std::size_t count = _encode_radix(rx, *lf.table, in, lf.limbs, lf.buffer.get());
	if (count > tr.leaf_digits) {
		throw std::logic_error("Error: Digit tree leaf out of range");
	}
	std::memset(out, 0, tr.leaf_digits - count);
	std::memcpy(out + tr.leaf_digits - count, lf.buffer.get(), count);
}

inline void uinteger_t::_tree_digits(const radix& rx, const radix_tree& tr, const radix_leaf& lf, unsigned j, digit* x, std::size_t nx, unsigned char* out) {
	while (nx && !x[nx - 1]) {
		--nx;
	}
	if (!nx) {
		std::memset(out, 0, tr.leaf_digits << j);
		return;
	}
	if (!j) {
		_tree_leaf(rx, tr, lf, x, nx, out);
		return;
	}
	const std::size_t half = tr.leaf_digits << (j - 1);
	const std::size_t dn = tr.size[j - 1];
	if (nx < dn) {
		// Below P_(j-1): the high half is zero.
		std::memset(out, 0, half);
		_tree_digits(rx, tr, lf, j - 1, x, nx, out + half);
		return;
	}
	const unsigned sh = tr.shift[j - 1];
	x[nx] = sh ? k_lshift(x, x, nx, sh) : 0;
	const std::size_t nn = nx + 1;
	const std::size_t qn = nn - dn;
	std::unique_ptr<digit[]> q(new digit[qn + 1]);
	{
		k_scratch scratch(k_div_dc_scratch_size(nn, dn));
		k_div_dc(q.get(), x, nn, tr.power[j - 1], dn, scratch.data());
	}
	if (sh) {
		k_rshift(x, x, dn, sh);
	}
	_tree_digits(rx, tr, lf, j - 1, q.get(), qn, out);
	q.reset();
	_tree_digits(rx, tr, lf, j - 1, x, dn, out + half);
}

inline std::size_t uinteger_t::_tree_encode(const radix& rx, const radix_tree& tr, const radix_leaf& lf, const digit* x, std::size_t n, unsigned char* out) {
	std::unique_ptr<digit[]> copy(new digit[n + 1]);
	std::copy(x, x + n, copy.get());
	_tree_digits(rx, tr, lf, tr.levels, copy.get(), n, out);
	return _strip_zeros(out, tr.leaf_digits << tr.levels);
}

template <typename Size>
inline Size uinteger_t::_tree_digits_for(std::size_t leaf_digits, unsigned levels) {
	const auto max = static_cast<unsigned long long>(std::numeric_limits<Size>::max());
	if (levels >= 64 || static_cast<unsigned long long>(leaf_digits) > (max >> levels)) {
		throw std::length_error("Error: Value too large to convert");
	}
	return static_cast<Size>(static_cast<unsigned long long>(leaf_digits) << levels);
}

template <typename Size>
inline void uinteger_t::_tree_plan(const radix& rx, std::size_t bits, std::size_t leaf_limbs, int slack, radix_tree& tr, radix_leaf& lf) {
	const double estimate = std::ceil(static_cast<double>(bits) / std::log2(static_cast<double>(rx.D)));
	const std::size_t chunks = static_cast<std::size_t>(std::max(2.0, estimate + slack));
	unsigned levels, leaf;
	_tree_shape(rx, chunks, std::min(leaf_limbs, radix_max_limbs - 4), levels, leaf);
	for (;; ++leaf) {
		(void)_tree_digits_for<Size>(static_cast<std::size_t>(leaf) * rx.k, levels);
		_tree_build(rx, tr, levels, leaf, true);
		// P_J = P_(J-1)^2 is above 2^(2 bits(P_(J-1)) - 2); the value must be below it.
		// 64-bit arithmetic: the top power's bit count can reach 2^31 on a 32-bit size_t.
		typedef unsigned long long wide;
		if (static_cast<wide>(bits) + 2 <= 2 * (static_cast<wide>(tr.size[levels - 1]) * digit_bits - tr.shift[levels - 1])) {
			break;
		}
	}
	assert(tr.size[0] <= radix_max_limbs);
	lf.limbs = tr.size[0];
	lf.table = &_table_for(rx, lf.limbs, lf.limbs * digit_bits);
	lf.pad.reset(new digit[lf.limbs]);
	lf.buffer.reset(new unsigned char[static_cast<std::size_t>(lf.table->chunks) * rx.k]);
}

template <typename Size, typename Result>
inline void uinteger_t::_digits_tree(Result& out, int base, const digit* p, std::size_t n, std::size_t leaf_limbs) {
	using value_type = typename Result::value_type;
	using in_place = std::integral_constant<bool, std::is_integral<value_type>::value && sizeof(value_type) == 1 && !std::is_same<value_type, bool>::value>;
	const radix& rx = _radix_for(base);
	const Size bits = _digits_for_limbs<Size>(static_cast<Size>(n), static_cast<unsigned>(_bits(p[n - 1])), 1);
	radix_tree tr;
	radix_leaf lf;
	_tree_plan<Size>(rx, static_cast<std::size_t>(bits), leaf_limbs, 1, tr, lf);
	const std::size_t cap = static_cast<std::size_t>(_tree_digits_for<Size>(tr.leaf_digits, tr.levels));
	if (cap > out.max_size() - out.size()) {
		throw std::length_error("Error: Value too large to convert");
	}
	_digits_append(out, cap, [&](unsigned char* buffer) { return _tree_encode(rx, tr, lf, p, n, buffer); }, in_place());
}

inline std::size_t uinteger_t::_tree_limbs(const radix_tree& tr, unsigned j) {
	return (j ? 2 * tr.size[j - 1] : tr.size[0]) + 2;
}

inline std::size_t uinteger_t::_tree_from(const radix_tree& tr, int base, unsigned j, const unsigned char* d, std::size_t m, digit* out) {
	if (m <= tr.leaf_digits) {
		const uinteger_t leaf = _from_digit_horner(d, m, base);
		std::copy(leaf.data(), leaf.data() + leaf.size(), out);
		return leaf.size();
	}
	assert(j >= 1);
	const std::size_t half = tr.leaf_digits << (j - 1);
	if (m <= half) {
		return _tree_from(tr, base, j - 1, d, m, out);
	}
	const std::size_t mh = m - half;
	const std::size_t p = tr.size[j - 1];
	std::unique_ptr<digit[]> part(new digit[_tree_limbs(tr, j - 1)]);
	const std::size_t hn = _tree_from(tr, base, j - 1, d, mh, part.get());
	std::size_t n = 0;
	if (hn) {
		k_scratch scratch(k_mul_scratch_size(hn, p));
		k_mul(out, part.get(), hn, tr.power[j - 1], p, scratch.data());
		n = hn + p;
	}
	const std::size_t ln = _tree_from(tr, base, j - 1, d + mh, half, part.get());
	if (!hn) {
		std::copy(part.get(), part.get() + ln, out);
		return ln;
	}
	if (ln) {
		// high * P + low < (high + 1) P <= B^(hn + p), so the sum has no carry out.
		digit carry = k_add_n(out, out, part.get(), ln);
		if (carry) {
			carry = k_add_1(out + ln, out + ln, n - ln, carry);
		}
		assert(!carry); (void)(carry);
	}
	while (n && !out[n - 1]) {
		--n;
	}
	return n;
}

inline uinteger_t uinteger_t::_from_digit_tree(const radix& rx, const unsigned char* d, std::size_t m, int base, std::size_t leaf_limbs) {
	while (m && !*d) {
		++d;
		--m;
	}
	uinteger_t result;
	if (!m) {
		return result;
	}
	unsigned levels, leaf;
	_tree_shape(rx, m / rx.k + (m % rx.k != 0), leaf_limbs, levels, leaf);
	radix_tree tr;
	_tree_build(rx, tr, levels, leaf, false);
	std::unique_ptr<digit[]> value(new digit[_tree_limbs(tr, levels)]);
	const std::size_t n = _tree_from(tr, base, levels, d, m, value.get());
	result._value.resize(n);
	std::copy(value.get(), value.get() + n, result._value.data());
	return result;
}

inline uinteger_t uinteger_t::_from_digit_bytes(const unsigned char* d, std::size_t m, int base) {
	const radix& rx = _radix_for(base);
	if (m / rx.k > from_digits_tree_threshold * digit_bits / _bit_length(rx.D)) {
		return _from_digit_tree(rx, d, m, base);
	}
	return _from_digit_horner(d, m, base);
}

template <typename Get>
inline uinteger_t uinteger_t::_from_digits(std::size_t n, int base, const Get& get) {
	if (base < 2 || base > 256) {
		throw std::invalid_argument("Error: Cannot convert from base " + std::to_string(base));
	}
	uinteger_t result;
	if (!n) {
		return result;
	}
	if (const auto bits_per_digit = base_bits(base)) {
		// Pack each digit straight into its place in the limbs.
		const std::size_t limbs = _limbs_for_digits<std::size_t>(n, bits_per_digit);
		result.resize(limbs, 0);
		digit* p = result.data();
		for (std::size_t i = 0; i < n; ++i) {
			const digit d = get(i);
			const std::size_t offset = (n - 1 - i) * bits_per_digit;
			const std::size_t shift = offset % digit_bits;
			const std::size_t limb = offset / digit_bits;
			p[limb] |= static_cast<digit>(d << shift);
			if (shift + bits_per_digit > digit_bits) {
				p[limb + 1] |= static_cast<digit>(d >> (digit_bits - shift));
			}
		}
		result.trim();
	} else if (radix_fast) {
		// Gather the digit bytes (get() validates each one), then convert them at once.
		unsigned char stack_digits[256];
		std::unique_ptr<unsigned char[]> heap_digits;
		unsigned char* digits = stack_digits;
		if (n > sizeof(stack_digits)) {
			heap_digits.reset(new unsigned char[n]);
			digits = heap_digits.get();
		}
		for (std::size_t i = 0; i < n; ++i) {
			digits[i] = static_cast<unsigned char>(get(i));
		}
		return _from_digit_bytes(digits, n, base);
	} else {
		// Accumulate a limb's worth of digits and fold it in with one pass.
		unsigned chunk_digits;
		const digit chunk_max = base_chunk(base, chunk_digits);
		result.reserve(n / chunk_digits + 1);
		digit chunk = 1;
		digit acc = 0;
		unsigned count = 0;
		for (std::size_t i = 0; i < n; ++i) {
			acc = acc * static_cast<digit>(base) + get(i);
			chunk *= static_cast<digit>(base);
			if (++count == chunk_digits) {
				result.mul_add_small(chunk_max, acc);
				chunk = 1;
				acc = 0;
				count = 0;
			}
		}
		if (count) {
			result.mul_add_small(chunk, acc);
		}
	}
	return result;
}

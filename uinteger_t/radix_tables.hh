// Base conversion: digit counts, radix tables and their lazy publication. Included inside class uinteger_t by uinteger_t.hh.

	// Sets this value to `this * mult + add` in place.
	uinteger_t& mul_add_small(digit mult, digit add) {
		auto n = size();
		auto p = data();
		digit carry = add;
		for (std::size_t i = 0; i < n; ++i) {
			carry = _multadd(p[i], mult, carry, 0, &p[i]);
		}
		if (carry) {
			append(carry);
		} else if (!mult) {
			trim();
		}
		return *this;
	}

	// Largest power of `base` that fits in a digit, and its exponent.
	static digit base_chunk(int base, unsigned& count) {
		const digit max = std::numeric_limits<digit>::max();
		digit chunk = 1;
		count = 0;
		while (chunk <= max / static_cast<digit>(base)) {
			chunk *= static_cast<digit>(base);
			++count;
		}
		return chunk;
	}

	// Size arithmetic for the bit-sliced conversions. A count that does not fit in `Size`
	// throws std::length_error instead of wrapping (a 32-bit size_t reaches it at 512 MiB
	// of base-256 digits). Production code uses std::size_t; tests pass narrower sizes.

	// Limbs that hold `n` digits of `bits` bits each.
	template <typename Size>
	static Size _limbs_for_digits(Size n, unsigned bits) {
		const auto max = static_cast<unsigned long long>(std::numeric_limits<Size>::max());
		if (static_cast<unsigned long long>(n) > (max - (digit_bits - 1)) / bits) {
			throw std::length_error("Error: Too many digits");
		}
		return static_cast<Size>((static_cast<unsigned long long>(n) * bits + digit_bits - 1) / digit_bits);
	}

	// Digits of `bits` bits each in a value of `n` >= 1 limbs whose top limb has `top`
	// significant bits.
	template <typename Size>
	static Size _digits_for_limbs(Size n, unsigned top, unsigned bits) {
		const auto max = static_cast<unsigned long long>(std::numeric_limits<Size>::max());
		if (max < top + bits - 1 || static_cast<unsigned long long>(n) - 1 > (max - top - (bits - 1)) / digit_bits) {
			throw std::length_error("Error: Value too large to convert");
		}
		const auto nbits = (static_cast<unsigned long long>(n) - 1) * digit_bits + top;
		return static_cast<Size>((nbits + bits - 1) / bits);
	}

	// Conversion kernels for bases that are not powers of two.
	//
	// Encoding writes the value as K chunks of k digits, one per power of D = base^k (the
	// largest power below the limb size B), and splits each chunk into digits. The chunks
	// come from a fixed-point fraction, not from divisions: x / D^K is held in L = K + g limbs
	// and each pass multiplies it by D, so the carry out of a pass is the next chunk, most
	// significant first (Bouvier and Zimmermann, "Division-Free Binary-to-Decimal Conversion",
	// IEEE Trans. Computers 63(8), 2014, Algorithm 2). Their initial division is a multiply
	// by R = floor(B^(L+n) / D^K), built once per base and size with one division and
	// cached: the fraction starts at (x + 1/2) / D^K, rounded down by at most
	// n + 2 units of its last limb, and every pass drops one limb. While the accumulated error
	// stays below 1/2 each chunk is exact; it is bounded in _make_radix_table, and the guard
	// limbs g add the precision a base with D close to B needs.
	// Values up to radix_unrolled limbs run fully unrolled kernels with the fraction in
	// registers, tables chosen by bit length so K is as small as the value allows. Larger values
	// up to radix_max_bits run four fused passes and a four-row short product, one table per
	// limb count.
	//
	// Decoding builds each chunk of k digits with SWAR multiplies (eight digit bytes in three
	// multiplies) and folds the chunks in four per pass, Horner style.

	static constexpr bool radix_fast = digit_bits == 32 || digit_bits == 64;
	// Apple's arm64 cores keep the encoding kernel ahead of the tree to about 13000 to 20000 bits,
	// the Intel i9 to about 8700 to 11000 (docs/design/base-conversion.md, The tree).
#ifndef UINTEGER_T_RADIX_MAX_BITS
#  if defined __APPLE__ && (defined __aarch64__ || defined __arm64__)
#    define UINTEGER_T_RADIX_MAX_BITS 14336
#  else
#    define UINTEGER_T_RADIX_MAX_BITS 8192
#  endif
#endif
	static constexpr std::size_t radix_max_bits = UINTEGER_T_RADIX_MAX_BITS;
	static constexpr std::size_t radix_max_limbs = radix_max_bits / digit_bits;
	// Scratch limbs of the general kernel: n + L + 8 with L at most radix_max_bits / (digit_bits - 8) + 2.
	static constexpr std::size_t radix_work = radix_max_limbs + radix_max_bits / (digit_bits > 8 ? digit_bits - 8 : 1) + 24;
#ifndef UINTEGER_T_RADIX_UNROLL
#define UINTEGER_T_RADIX_UNROLL 6
#endif
	static constexpr std::size_t radix_unrolled = UINTEGER_T_RADIX_UNROLL;
	static_assert(radix_unrolled <= 8, "the unrolled digit kernels go up to 8 limbs");

	// R and floor(R / 2) for inputs below 2^bits, with the chunk and limb counts they imply.
	struct radix_table {
		unsigned chunks;           // K: D^K >= 2^bits
		unsigned guard;            // g
		unsigned fraction;         // L = K + g
		unsigned rlen;             // limbs of R: L, or L + 1 when D^K < B^n
		std::vector<digit> store;  // 4 zero limbs, R, 8 zero limbs, floor(R / 2)
		const digit* R;
		const digit* R2;
	};

	// floor(r / d) for r below 2^nd with a multiply and a shift (Granlund and Montgomery,
	// "Division by invariant integers using multiplication", PLDI 1994). `wide` is the
	// 65-bit multiplier form that covers any 64-bit r.
	struct radix_div {
		std::uint64_t m;
		unsigned s;
		bool wide;
		bool ok;
	};

	// The context of one base: chunk size, digit extraction constants and the tables.
	struct radix {
		int base;
		unsigned k;                // digits per chunk, D = base^k
		unsigned h;                // digits per piece: base^h < 2^32
		digit D;
		digit E[4];                // D^4, for the four-chunk Horner step
		std::uint64_t bh;          // base^h
		std::uint64_t b8;          // base^8 modulo 2^64, the step of the decoder's eight-digit groups
		std::uint64_t cinv[digit_bits > 20 ? digit_bits + 1 : 21];  // ceil(2^64 / base^m); h <= 20, which the fixed splitters index at any limb width
		radix_div div0, div1;      // chunk / bh, and the quotient / bh when k = 2h + 1
		const unsigned char* pair; // two digits per entry for base <= 64
		std::vector<unsigned char> pair_store;
		void (*split)(const radix&, digit, unsigned char*);  // _split_chunk for the shape (k, h) of this base
		mutable std::atomic<const radix_table*> by_bits[radix_unrolled * digit_bits + 1];
		mutable std::atomic<const radix_table*> by_limbs[radix_max_limbs + 1];
	};

	static std::uint64_t _umulh(std::uint64_t x, std::uint64_t y) {
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

	// floor(2^e / d) as (hi, lo) and the remainder, by shift and subtract (table building only).
	static std::uint64_t _pow2_div(unsigned e, std::uint64_t d, std::uint64_t& hi, std::uint64_t& lo) {
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

	static unsigned _bit_length(std::uint64_t v) {
		unsigned n = 0;
		for (; v; v >>= 1) {
			++n;
		}
		return n;
	}

	// The multiplier for dividends below 2^nd by d (not a power of two, d < 2^32).
	static radix_div _make_div(std::uint64_t d, unsigned nd) {
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

	static std::uint64_t _div_magic(const radix_div& v, std::uint64_t r) {
		const std::uint64_t t = _umulh(v.m, r);
		return v.wide ? (t + ((r - t) >> 1)) >> v.s : t >> v.s;
	}

	static radix* _make_radix(int base) {
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

	// R for values below 2^bits (n limbs): floor(B^(L + n) / D^K), one division by the power that
	// sets K (K single-limb divisions by D give the same value, as floor(floor(a / D) / D) is
	// floor(a / D^2), in O(n^2)). `guard` and `half` override the computed guard limbs and drop
	// the 1/2 offset; tests use them to show the edge inputs catch an under-sized table.
	static radix_table* _make_radix_table(const radix& rx, std::size_t n, std::size_t bits, int guard = -1, bool half = true) {
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

	// Tables and contexts are built on first use and published with a compare-and-swap, so
	// concurrent first calls are safe; the loser deletes its copy. They live until exit.
	template <typename T>
	static const T* _publish(std::atomic<const T*>& slot, T* fresh) {
		std::unique_ptr<T> mine(fresh);
		const T* expected = nullptr;
		if (slot.compare_exchange_strong(expected, fresh, std::memory_order_acq_rel, std::memory_order_acquire)) {
			mine.release();
			return fresh;
		}
		return expected;
	}

	static const radix& _radix_for(int base) {
		static std::atomic<const radix*> contexts[257];
		std::atomic<const radix*>& slot = contexts[base];
		const radix* rx = slot.load(std::memory_order_acquire);
		if (!rx) {
			rx = _publish(slot, _make_radix(base));
		}
		return *rx;
	}

	// The table for a value of n limbs and `bits` bits.
	static const radix_table& _table_for(const radix& rx, std::size_t n, std::size_t bits) {
		const bool by_bits = n <= radix_unrolled;
		std::atomic<const radix_table*>& slot = by_bits ? rx.by_bits[bits] : rx.by_limbs[n];
		const radix_table* t = slot.load(std::memory_order_acquire);
		if (!t) {
			t = _publish(slot, _make_radix_table(rx, n, by_bits ? bits : n * digit_bits));
		}
		return *t;
	}

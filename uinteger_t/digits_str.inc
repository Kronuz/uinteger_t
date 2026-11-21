// Value access, bits, digits, from_digits, parsing and the string forms. Class body of uinteger_t, included inside it by uinteger_t.hh; not a header.

	// Get private value at index
	const digit& value(std::size_t idx) const {
		static const digit zero = 0;
		return idx < size() ? *(begin() + idx) : zero;
	}

	// Get value of bit N
	bool operator[](std::size_t n) const {
		auto nd = n / digit_bits;
		auto nm = n % digit_bits;
		return nd < size() ? (*(begin() + nd) >> nm) & 1 : 0;
	}

	// Get bitsize of value
	std::size_t bits() const {
		auto sz = size();
		if (sz) {
			return _bits(back()) + (sz - 1) * digit_bits;
		}
		return 0;
	}

	// Appends the digits of the value in `base` (2..256) to `out` as byte values
	// 0..base-1, most significant first. Zero gives a single 0 digit.
	template <typename Result, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	void digits(Result& out, int base) const {
		using value_type = typename Result::value_type;
		if (base < 2 || base > 256) {
			throw std::invalid_argument("Base must be in the range [2, 256]");
		}
		const digit* p = data();
		auto n = size();
		while (n && !p[n - 1]) {
			--n;
		}
		if (!n) {
			out.push_back(static_cast<value_type>(0));
			return;
		}
		const auto start = out.size();
		if (const auto bits_per_digit = base_bits(base)) {
			// Slice the digits straight out of the limbs.
			const digit mask = (static_cast<digit>(1) << bits_per_digit) - 1;
			const std::size_t count = _digits_for_limbs<std::size_t>(n, static_cast<unsigned>(_bits(p[n - 1])), bits_per_digit);
			if (count > out.max_size() - start) {
				throw std::length_error("Error: Value too large to convert");
			}
			out.resize(start + count);
			if (bits_per_digit == 8 && std::is_integral<value_type>::value && sizeof(value_type) == 1 && !std::is_same<value_type, bool>::value) {
				// Whole bytes: each limb goes out big-endian.
				_store_bytes(reinterpret_cast<unsigned char*>(&out[start]), p, n, count);
				return;
			}
			std::size_t limb = 0;
			unsigned shift = 0;
			for (std::size_t i = count; i--;) {
				digit v = p[limb] >> shift;
				shift += bits_per_digit;
				if (shift >= digit_bits) {
					// The digit ends this limb; a straddling one continues in the next.
					shift -= digit_bits;
					++limb;
					if (shift && limb < n) {
						v |= static_cast<digit>(p[limb] << (bits_per_digit - shift));
					}
				}
				out[start + i] = static_cast<value_type>(v & mask);
			}
		} else if (radix_fast && n <= radix_max_limbs && n <= digits_tree_threshold) {
			_digits_radix(out, base, p, n);
		} else if (radix_fast) {
			_digits_tree(out, base, p, n);
		} else {
			// Peel a limb's worth of digits per pass, least significant first. The
			// reserve is only an estimate, so it is skipped when it would not fit.
			const std::size_t per_limb = static_cast<std::size_t>(base_size(base)) * digit_bits;
			if (n <= (std::numeric_limits<std::size_t>::max() - 63 - start) / per_limb) {
				const std::size_t wanted = start + (n * per_limb + 63) / 64;
				if (out.capacity() < wanted) {
					out.reserve(wanted);
				}
			}
			unsigned chunk_digits;
			const small_divisor chunk(base_chunk(base, chunk_digits));
			uinteger_t quotient = *this;
			do {
				auto r = quotient.divmod_small(chunk);
				if (quotient) {
					for (unsigned i = 0; i < chunk_digits; ++i) {
						out.push_back(static_cast<value_type>(r % static_cast<digit>(base)));
						r /= static_cast<digit>(base);
					}
				} else {
					do {
						out.push_back(static_cast<value_type>(r % static_cast<digit>(base)));
						r /= static_cast<digit>(base);
					} while (r);
				}
			} while (quotient);
			std::reverse(out.begin() + start, out.end());
		}
	}

	// The inverse of digits(): `n` digits in `base` (2..256), most significant first,
	// one byte each. Throws std::invalid_argument on a digit >= base.
	template <typename T, typename = std::enable_if_t<std::is_integral<T>::value && sizeof(T) == 1>>
	static uinteger_t from_digits(const T* src, std::size_t n, int base) {
		if (radix_fast && n && base >= 2 && base <= 256 && !base_bits(base)) {
			// Check every digit first so the kernel can trust them.
			const unsigned char* bytes = reinterpret_cast<const unsigned char*>(src);
			unsigned char largest = 0;
			for (std::size_t i = 0; i < n; ++i) {
				largest = bytes[i] > largest ? bytes[i] : largest;
			}
			if (largest >= base) {
				std::size_t i = 0;
				while (bytes[i] < base) {
					++i;
				}
				throw std::invalid_argument("Error: Not a digit in base " + std::to_string(base) + ": " + std::to_string(bytes[i]) + " at " + std::to_string(n - i));
			}
			return _from_digit_bytes(bytes, n, base);
		}
		return _from_digits(n, base, [src, n, base](std::size_t i) {
			const auto d = static_cast<digit>(static_cast<unsigned char>(src[i]));
			if (d >= static_cast<digit>(base)) {
				throw std::invalid_argument("Error: Not a digit in base " + std::to_string(base) + ": " + std::to_string(d) + " at " + std::to_string(n - i));
			}
			return d;
		});
	}

	// Get string representation of value
	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result str(int alphabet_base = 10) const {
		if (alphabet_base < 2 || (alphabet_base > 36 && alphabet_base != 256)) {
			throw std::invalid_argument("Base must be in the range [2, 36] or 256");
		}
		Result result;
		digits(result, alphabet_base);
		if (alphabet_base != 256) {
			for (auto& c : result) {
				c = chr(static_cast<int>(c));
			}
		}
		return result;
	}

	static uinteger_t strtouint(const void* encoded, std::size_t encoded_size, int alphabet_base) {
		uinteger_t result;
		result._parse(encoded, encoded_size, alphabet_base);
		return result;
	}

private:
	// Sets this value, which is zero, to the number in `encoded_size` characters of text (bases 2 to 36) or
	// bytes (base 256, most significant first).
	void _parse(const void* encoded, std::size_t encoded_size, int alphabet_base) {
		const char* data = (const char *)encoded;
		uinteger_t& result = *this;

		if (alphabet_base >= 2 && alphabet_base <= 36) {
			result = _from_digits(encoded_size, alphabet_base, [data, encoded_size, alphabet_base](std::size_t i) {
				const auto d = ord(static_cast<unsigned char>(data[i]));
				if (d < 0 || d >= alphabet_base) {
					throw std::invalid_argument("Error: Not a digit in base " + std::to_string(alphabet_base) + ": '" + std::string(1, data[i]) + "' at " + std::to_string(encoded_size - i));
				}
				return static_cast<digit>(d);
			});
		} else if (encoded_size && alphabet_base == 256) {
			// Leading zero bytes carry no value, so they are skipped: the top limb of the result is
			// nonzero, and zero bytes make zero (no limbs). Each limb is the digit_octets bytes that
			// end i limbs before the end of the text; the top limb takes what is left over.
			const unsigned char* bytes = static_cast<const unsigned char*>(encoded);
			std::size_t count = encoded_size;
			while (count && !*bytes) {
				++bytes;
				--count;
			}
			const std::size_t whole = count / digit_octets;
			const std::size_t left = count % digit_octets;
			result.resize_uninit(whole + (left ? 1 : 0));
			digit* p = result.data();
			for (std::size_t i = 0; i < whole; ++i) {
				p[i] = _load_be(bytes + count - (i + 1) * digit_octets);
			}
			if (left) {
				digit v = 0;
				for (std::size_t j = 0; j < left; ++j) {
					v = static_cast<digit>((v << 8) | bytes[j]);
				}
				p[whole] = v;
			}
		} else {
			throw std::invalid_argument("Error: Cannot convert from base " + std::to_string(alphabet_base));
		}
	}

public:
	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result bin() const {
		return str<Result>(2);
	}

	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result oct() const {
		return str<Result>(8);
	}

	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result hex() const {
		return str<Result>(16);
	}

	template <typename Result = std::string, typename = std::enable_if_t<uinteger_t::is_result<Result>::value>>
	Result raw() const {
		return str<Result>(256);
	}

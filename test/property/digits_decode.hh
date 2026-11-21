// The Horner decoder bounds, digit errors and values, size guards and random digit round trips. Included by property.cc.

// The Horner decoder touches only the chunks = ceil(m / k) limbs it is given (L1): run on a
// buffer of exactly that many limbs between guard limbs, at the digit counts around every
// chunk boundary (m = k c - 1, k c, k c + 1 for c up to 20, across the 16-chunk stack limit of
// _from_digit_horner), against a value built by multiplying and adding. Four digit patterns:
// all base - 1, random, a 1 then zeros, zeros then a 1.
static void check_horner_bounds() {
	for (int base : {3, 5, 7, 10, 36, 58, 62, 100, 255}) {
		const auto& rx = uinteger_t::_radix_for(base);
		for (std::size_t c = 1; c <= 20; ++c) {
			for (std::size_t m : {rx.k * c - 1, rx.k * c, rx.k * c + 1}) {
				if (!m) continue;
				const std::size_t chunks = (m + rx.k - 1) / rx.k;
				for (int pattern = 0; pattern < 4; ++pattern) {
					std::vector<unsigned char> d(m);
					for (std::size_t i = 0; i < m; ++i) {
						switch (pattern) {
							case 0: d[i] = static_cast<unsigned char>(base - 1); break;
							case 1: d[i] = static_cast<unsigned char>(rng() % static_cast<unsigned>(base)); break;
							case 2: d[i] = i == 0 ? 1 : 0; break;
							default: d[i] = i + 1 == m ? 1 : 0; break;
						}
					}
					uinteger_t ref;
					for (auto digit_value : d) {
						ref = ref * uinteger_t(base) + uinteger_t(digit_value);
					}
					const uinteger_t::digit guard = static_cast<uinteger_t::digit>(0x5a5a5a5a5a5a5a5aull);
					std::vector<uinteger_t::digit> buf(chunks + 2, guard);
					const std::size_t n = uinteger_t::_horner_limbs(rx, d.data(), m, buf.data() + 1);
					CHECK(buf.front() == guard && buf.back() == guard, "decoder wrote outside its %zu chunks, base %d, %zu digits, pattern %d", chunks, base, m, pattern);
					CHECK(n <= chunks && n == ref.size() && std::equal(buf.begin() + 1, buf.begin() + 1 + n, ref.data()), "decoder value, %zu limbs (expected %zu, chunks %zu), base %d, %zu digits, pattern %d", n, ref.size(), chunks, base, m, pattern);
					CHECK(uinteger_t::from_digits(d.data(), m, base) == ref, "from_digits at the chunk boundary, base %d, %zu digits, pattern %d", base, m, pattern);
				}
			}
		}
	}
}

// A digit that is not below the base is found wherever it sits in a run of valid ones, and
// the message names the first one counted from the most significant end.
static void check_digit_errors() {
	for (int base : {3, 10, 58, 200}) {
		for (std::size_t n : {1u, 7u, 8u, 9u, 22u, 23u, 40u, 41u, 100u, 257u}) {
			std::string valid(n, '\0');
			for (auto& c : valid) c = static_cast<char>(rng() % static_cast<unsigned>(base));
			CHECK(what_of([&] { uinteger_t::from_digits(valid.data(), n, base); }) == "no throw", "valid run base %d", base);
			for (std::size_t at : {std::size_t(0), n / 2, n - 1}) {
				for (unsigned bad : {static_cast<unsigned>(base), 255u}) {
					std::string s = valid;
					s[at] = static_cast<char>(bad);
					if (at + 1 < n) s[n - 1] = static_cast<char>(bad == 255u ? base : 255);  // a later one is ignored
					const std::string expect = "Error: Not a digit in base " + std::to_string(base) + ": " + std::to_string(bad) + " at " + std::to_string(n - at);
					CHECK(what_of([&] { uinteger_t::from_digits(s.data(), n, base); }) == expect, "bad digit %u at %zu of %zu, base %d", bad, at, n, base);
				}
			}
		}
	}
}

static void check_digit_values() {
	// b^k and b^k - 1 have known digits at every size, across limb and chunk boundaries.
	for (int base = 2; base <= 256; ++base) {
		uinteger_t power(1);
		// Chunks of a limb hold at most 40 digits in a base above 40; low bases and the
		// bit-sliced bases get longer runs.
		const unsigned kmax = (base <= 40 || (base & (base - 1)) == 0) ? 150 : 40;
		for (unsigned k = 0; k <= kmax; ++k) {
			std::string expect(k + 1, '\0');
			expect[0] = 1;
			std::string got;
			power.digits(got, base);
			CHECK(got == expect, "digits of %d^%u", base, k);
			if (k) {
				std::string nines(k, static_cast<char>(base - 1));
				got.clear();
				(power - uinteger_t(1)).digits(got, base);
				CHECK(got == nines, "digits of %d^%u - 1", base, k);
			}
			power *= uinteger_t(base);
		}
		std::string zero;
		uinteger_t(0).digits(zero, base);
		CHECK(zero == std::string(1, '\0'), "digits of zero in base %d", base);
		const unsigned char none = 0;
		CHECK(uinteger_t::from_digits(&none, 0, base) == uinteger_t(0), "from_digits of nothing, base %d", base);
	}

	// Digits that straddle a limb boundary (3 and 5 bits do not divide 32 or 64).
	std::string d;
	(uinteger_t(1) << 63).digits(d, 8);
	CHECK(d == std::string("\x01") + std::string(21, '\0'), "2^63 in base 8");
	d.clear();
	((uinteger_t(1) << 64) - uinteger_t(1)).digits(d, 8);
	CHECK(d == std::string("\x01") + std::string(21, '\x07'), "2^64-1 in base 8");
	d.clear();
	((uinteger_t(1) << 64) - uinteger_t(1)).digits(d, 32);
	CHECK(d == std::string("\x0f") + std::string(12, '\x1f'), "2^64-1 in base 32");
	d.clear();
	uinteger_t(1000).digits(d, 256);
	CHECK(d == std::string("\x03\xe8", 2), "1000 in base 256");

	// Invalid input.
	for (int base = 2; base < 256; ++base) {
		unsigned char bad[3] = {1, static_cast<unsigned char>(base), 1};
		CHECK(what_of([&] { uinteger_t::from_digits(bad, 3, base); }).rfind("Error", 0) == 0, "digit == base %d throws", base);
		bad[1] = 1;
		bad[2] = 255;
		CHECK(what_of([&] { uinteger_t::from_digits(bad, 3, base); }).rfind("Error", 0) == 0, "digit 255 in base %d throws", base);
	}
	unsigned char nine[3] = {1, 0, 9};
	CHECK(what_of([&] { uinteger_t::from_digits(nine, 3, 8); }) == "Error: Not a digit in base 8: 9 at 1", "from_digits message");
	for (int base : {0, 1, 257, -2}) {
		CHECK(what_of([&] { uinteger_t::from_digits(nine, 3, base); }).rfind("Error", 0) == 0, "from_digits base %d", base);
		std::string out;
		CHECK(what_of([&] { uinteger_t(5).digits(out, base); }) == "Base must be in the range [2, 256]", "digits base %d", base);
	}
	// str() takes 2 to 36 and 256, and its message says so.
	for (int base : {-1, 0, 1, 37, 100, 255, 257}) {
		CHECK(what_of([&] { uinteger_t(5).str(base); }) == "Base must be in the range [2, 36] or 256", "str base %d", base);
	}
	CHECK(uinteger_t(5).str(256) == std::string(1, '\x05') && uinteger_t(255).str(36) == "73" && uinteger_t(5).str(2) == "101", "str in the bases at the ends of its range");

	// Parsing a digit that is not below the base, and characters that are not digits.
	CHECK(what_of([] { uinteger_t("19", 8); }) == "Error: Not a digit in base 8: '9' at 1", "parse 9 in base 8");
	CHECK(what_of([] { uinteger_t("12z4", 10); }) == "Error: Not a digit in base 10: 'z' at 2", "parse z in base 10");
	CHECK(what_of([] { uinteger_t("2", 2); }) == "Error: Not a digit in base 2: '2' at 1", "parse 2 in base 2");
	CHECK(what_of([] { uinteger_t("1g", 16); }) == "Error: Not a digit in base 16: 'g' at 1", "parse g in base 16");
	CHECK(what_of([] { uinteger_t("1-9", 16); }) == "Error: Not a digit in base 16: '-' at 2", "parse - in base 16");
	CHECK(uinteger_t("FF", 16) == uinteger_t(255) && uinteger_t("Zz", 36) == uinteger_t(1295), "parse upper case");

	// Bytes >= 0x80 are not digits. (An unsigned char index keeps ord() inside its table;
	// a plain char sign-extends and reads before it.)
	const std::string high_prefix = "Error: Not a digit in base ";
	for (int base : {2, 3, 8, 10, 16, 32, 36}) {
		for (unsigned char byte : {0x80, 0xe9, 0xff}) {
			std::string text = "1";
			text.push_back(static_cast<char>(byte));
			text.push_back('1');
			auto msg = what_of([&] { uinteger_t(text, base); });
			CHECK(msg == high_prefix + std::to_string(base) + ": '" + std::string(1, static_cast<char>(byte)) + "' at 2", "parse byte %u in base %d", byte, base);
			std::vector<char> vec(text.begin(), text.end());
			CHECK(what_of([&] { uinteger_t(vec, base); }) == msg, "parse byte %u from a vector in base %d", byte, base);
			CHECK(what_of([&] { uinteger_t(text.data(), text.size(), base); }) == msg, "parse byte %u from a pointer in base %d", byte, base);
		}
	}
	CHECK(what_of([] { uinteger_t("\xff", 16); }) == "Error: Not a digit in base 16: '\xff' at 1", "parse 0xff in base 16");
	CHECK(what_of([] { uinteger_t("1\x80", 10); }) == "Error: Not a digit in base 10: '\x80' at 1", "parse 0x80 in base 10");
}

// The size arithmetic of the bit-sliced conversions must throw exactly when the exact
// result does not fit the size type, so a 32-bit size_t cannot wrap into a short
// buffer. Checked at 16 and 32 bits against 64-bit arithmetic, and at the sizes a
// 512 MiB base-256 input reaches.
template <typename F>
static bool throws_length(F f) {
	try { f(); } catch (const std::length_error&) { return true; }
	return false;
}

template <typename Size>
static void check_limbs_for_digits(unsigned long long n, unsigned bits) {
	const unsigned long long digit_bits = 8 * sizeof(uinteger_t::digit);
	const unsigned long long need = n * bits + digit_bits - 1;
	Size got = 0;
	const bool threw = throws_length([&] { got = uinteger_t::_limbs_for_digits<Size>(static_cast<Size>(n), bits); });
	CHECK(threw == (need > std::numeric_limits<Size>::max()), "limbs for %llu digits of %u bits (%zu-bit size): %s", n, bits, 8 * sizeof(Size), threw ? "threw" : "did not throw");
	CHECK(threw || got == need / digit_bits, "limbs for %llu digits of %u bits (%zu-bit size): got %llu", n, bits, 8 * sizeof(Size), static_cast<unsigned long long>(got));
}

template <typename Size>
static void check_digits_for_limbs(unsigned long long n, unsigned top, unsigned bits) {
	const unsigned long long digit_bits = 8 * sizeof(uinteger_t::digit);
	const unsigned long long need = (n - 1) * digit_bits + top + bits - 1;
	Size got = 0;
	const bool threw = throws_length([&] { got = uinteger_t::_digits_for_limbs<Size>(static_cast<Size>(n), top, bits); });
	CHECK(threw == (need > std::numeric_limits<Size>::max()), "digits for %llu limbs, top %u, %u bits (%zu-bit size): %s", n, top, bits, 8 * sizeof(Size), threw ? "threw" : "did not throw");
	CHECK(threw || got == need / bits, "digits for %llu limbs, top %u, %u bits (%zu-bit size): got %llu", n, top, bits, 8 * sizeof(Size), static_cast<unsigned long long>(got));
}

template <typename Size>
static void check_size_window(unsigned long long around, unsigned long long span) {
	const unsigned long long digit_bits = 8 * sizeof(uinteger_t::digit);
	for (unsigned bits = 1; bits <= 8; ++bits) {
		const unsigned long long max = std::numeric_limits<Size>::max();
		// n where n * bits + digit_bits - 1 crosses the maximum, and where the digit count does.
		for (unsigned long long centre : {around, (max - (digit_bits - 1)) / bits, (max - digit_bits - bits) / digit_bits + 1}) {
			for (unsigned long long n = centre > span ? centre - span : 0; n <= centre + span && n <= max; ++n) {
				check_limbs_for_digits<Size>(n, bits);
				if (n) {
					for (unsigned top : {1u, static_cast<unsigned>(digit_bits / 2), static_cast<unsigned>(digit_bits)}) check_digits_for_limbs<Size>(n, top, bits);
				}
			}
		}
	}
}

static void check_size_guards() {
	// Everything small, then the neighbourhood of each limit.
	for (unsigned long long n = 0; n <= 300; ++n) {
		for (unsigned bits = 1; bits <= 8; ++bits) {
			check_limbs_for_digits<std::uint16_t>(n, bits);
			check_limbs_for_digits<std::uint32_t>(n, bits);
		}
	}
	check_size_window<std::uint16_t>(65535, 70);
	check_size_window<std::uint32_t>(std::numeric_limits<std::uint32_t>::max(), 70);
	// 2^29 + 5 base-256 digits wrap a 32-bit bit count, so they throw; just under the limit they do not.
	CHECK(throws_length([] { uinteger_t::_limbs_for_digits<std::uint32_t>((1u << 29) + 5, 8); }), "2^29 + 5 base-256 digits fit 32 bits");
	CHECK(!throws_length([] { uinteger_t::_limbs_for_digits<std::uint32_t>((1u << 29) - 8, 8); }), "2^29 - 8 base-256 digits throw");
	// At the real size type, ordinary sizes pass through.
	CHECK(uinteger_t::_limbs_for_digits<std::size_t>(1000, 8) == (8000 + 8 * sizeof(uinteger_t::digit) - 1) / (8 * sizeof(uinteger_t::digit)), "limbs at size_t");
}

static void check_digits_random(std::size_t big) {
	int base = rng() % 3 ? 2 + static_cast<int>(rng() % 255) : (1 << (1 + rng() % 8));
	// Mostly short values, some long enough to cross many limbs and chunks.
	auto v = random_value(rng() % 8 ? 1 + rng() % 40 : 1 + rng() % std::min<std::size_t>(big, 300));
	bool with_ref = v.bits() <= 4000;
	if (rng() & 1) {
		check_digits<std::string>(v, base, with_ref);
	} else {
		check_digits<std::vector<unsigned char>>(v, base, with_ref);
	}
	if (base <= 36) {
		CHECK(uinteger_t(v.str(base), base) == v, "str/parse base %d", base);
	}
}

// The limb container representation, leading-zero bytes and the byte forms. Included by property.cc.

// The representation is the limb vector alone; moving and copying leave no stale state, and an
// in-place right shift does not expose the limbs it dropped (x *= 1 once did).
static void check_repr() {
	static_assert(sizeof(uinteger_t) == sizeof(uinteger_t::container), "a uinteger_t is just its limb vector");
	const std::size_t bits = uinteger_t::digit_bits;

	for (std::size_t n = 0; n <= 9; ++n) {
		for (int rep = 0; rep < 4; ++rep) {
			const auto a = random_value(n * bits / 32);
			const auto b = random_value(rng() % 8 ? n * bits / 32 + rng() % 5 : 0);
			for (std::size_t k : {std::size_t(0), std::size_t(1), bits - 1, bits, bits + 1, 2 * bits - 1, 2 * bits, 2 * bits + 5, 3 * bits, n * bits - 1, n * bits, n * bits + 1, n * bits + 70, std::size_t(5 * bits + 17)}) {
				if (k <= 12 * bits) check_shifts(a, k);
			}
			check_alias(a, b);
			check_alias(b, a);
		}
	}

	// Counts of 2^64 bits or more (more than 64 / digit_bits limbs): a right shift gives zero, a
	// left shift of zero stays zero, and a left shift of anything else cannot be represented.
	// Counts below that are read exactly (test/wide_shift.cc).
	const auto wide = pow2(64);
	CHECK(!(random_value(4) >> wide), "right shift by 2^64");
	CHECK(!(uinteger_t() << wide), "zero shifted left by 2^64");
	bool threw = false;
	try {
		auto x = uinteger_t(1) << wide;
		CHECK(false, "1 << 2^64 returned %zu limbs", x.size());
	} catch (const std::length_error&) {
		threw = true;
	}
	CHECK(threw, "left shift by 2^64 throws std::length_error");
	CHECK(!(random_value(4) >> -1), "right shift by -1 gives zero");

	// A multi-limb factor or divisor whose low limb is 1 (B^k + 1) is not the value 1: the early
	// exits for 1 in multiplication and division check the size as well as the limb (CR2).
	for (std::size_t k = 1; k <= 4; ++k) {
		const auto m = pow2(k * bits) + uinteger_t(1);
		CHECK(m.size() == k + 1 && m.front() == 1, "B^%zu + 1 has %zu limbs", k, m.size());
		for (std::size_t n = 1; n <= 9; ++n) {
			const auto x = random_value(n * bits / 32 + 1);
			const auto prod = x * m;
			CHECK(prod == (x << (k * bits)) + x, "x * (B^%zu + 1) with %zu limbs", k, x.size());
			CHECK(m * x == prod, "(B^%zu + 1) * x with %zu limbs", k, x.size());
			CHECK(prod / m == x && !(prod % m), "x * (B^%zu + 1) / (B^%zu + 1)", k, k);
			const auto qr = x.divmod(m);
			CHECK(qr.first * m + qr.second == x && qr.second < m, "divmod by B^%zu + 1, %zu limbs", k, x.size());
			CHECK(x / m == qr.first && x % m == qr.second, "/ and %% by B^%zu + 1, %zu limbs", k, x.size());
			auto t = x;
			t *= m;
			CHECK(t == prod, "x *= B^%zu + 1", k);
			t = x;
			t /= m;
			CHECK(t == qr.first, "x /= B^%zu + 1", k);
			t = x;
			t %= m;
			CHECK(t == qr.second, "x %%= B^%zu + 1", k);
		}
		CHECK(m * m == pow2(2 * k * bits) + (pow2(k * bits) << 1) + uinteger_t(1), "(B^%zu + 1)^2", k);
	}

	// Carry and borrow chains across limb boundaries.
	for (std::size_t n = 1; n <= 10; ++n) {
		const auto p = pow2(n * bits);
		const auto m = p - uinteger_t(1);
		CHECK(m.size() == n && m + uinteger_t(1) == p, "B^%zu - 1 + 1", n);
		CHECK(m + m == pow2(n * bits + 1) - uinteger_t(2), "(B^%zu - 1) * 2", n);
		CHECK(p - p == uinteger_t(), "B^%zu - B^%zu", n, n);
		CHECK(p - m == uinteger_t(1), "B^%zu - (B^%zu - 1)", n, n);
		auto x = m;
		x += uinteger_t(1);
		CHECK(x == p, "(B^%zu - 1) += 1", n);
		x -= uinteger_t(1);
		CHECK(x == m, "B^%zu -= 1", n);
		auto y = uinteger_t(1);
		y -= p;
		CHECK(y == pow2((n + 1) * bits) - p + uinteger_t(1), "1 - B^%zu wraps within %zu limbs", n, n + 1);
		--x;
		++x;
		CHECK(x == m, "-- then ++");
	}

	// x >>= 128 then x *= 1 gave back the five limbs the shift had dropped.
	auto mk = [&] { return uinteger_t(std::string(5 * bits / 8, '\x7f'), 256); };
	const auto want = mk() >> (2 * bits);
	CHECK(want.size() == 3, "five limbs shifted right by two");
	auto x = mk();
	x >>= 2 * bits;
	x *= uinteger_t(1);
	CHECK(x == want, "x >>= 2 limbs; x *= 1");
	x = mk();
	x >>= 2 * bits;
	x /= uinteger_t(1);
	CHECK(x == want, "x >>= 2 limbs; x /= 1");
	x = mk();
	x >>= 2 * bits;
	x %= uinteger_t(1);
	CHECK(!x, "x >>= 2 limbs; x %%= 1");
	x = mk();
	x >>= 2 * bits;
	const auto& same = x;
	x = same;
	CHECK(x == want, "x >>= 2 limbs; x = x");
	x = mk();
	x >>= 2 * bits;
	uinteger_t y = std::move(x);
	CHECK(y == want, "x >>= 2 limbs; y = std::move(x)");
	x = want;
	CHECK(x == want, "a moved-from value takes assignment");

	// Self copy-assignment, and so `x *= 1`, after an in-place right shift (hunt-1 R1).
	{
		uinteger_t v("100000000000000000000000000000005", 16); // 2^128 + 5
		v >>= 64;
		const auto two64 = pow2(64);
		CHECK(v == two64, "2^128 + 5 >>= 64 gives 2^64");
		v *= 1;
		CHECK(v == two64, "v >>= 64; v *= 1");
		uinteger_t z("100000000000000000000000000000005", 16);
		z >>= 64;
		const auto& zr = z;
		z = zr;
		CHECK(z == two64, "z >>= 64; z = z");
		uinteger_t w("100000000000000000000000000000005", 16);
		w >>= 64;
		w *= w;
		CHECK(w == pow2(128), "w >>= 64; w *= w");
	}

	// A moved-from value is zero, also after an in-place right shift (hunt-1 R2: it kept its
	// window offset, so size() read as 2^64 - 1).
	{
		uinteger_t v("100000000000000000000000000000005", 16);
		v >>= 64;
		uinteger_t moved(std::move(v));
		CHECK(moved == pow2(64), "move construction after >>=");
		CHECK(v.size() == 0 && !v, "moved-from after >>= has size %zu", v.size());
		if (!v.size()) {
			CHECK(v + uinteger_t(7) == uinteger_t(7), "moved-from value used as zero");
		}
		uinteger_t u("100000000000000000000000000000005", 16);
		u >>= 64;
		uinteger_t target;
		target = std::move(u);
		CHECK(target == pow2(64), "move assignment after >>=");
		CHECK(u.size() == 0 && !u, "move-assigned-from after >>= has size %zu", u.size());
		u = uinteger_t(9);
		CHECK(u == uinteger_t(9), "a moved-from value takes assignment after >>=");
	}
}

// 0 to 17 leading zero bytes before a value of a few sizes (and before nothing): the parsed value
// compares, sizes and counts bits as the value does, built by shifting a byte at a time.
static void check_leading_zero_bytes() {
	for (std::size_t zeros = 0; zeros <= 17; ++zeros) {
		for (std::size_t tail : {0, 1, 2, 7, 8, 9, 15, 16, 17}) {
			std::vector<unsigned char> bytes(zeros, 0);
			for (std::size_t i = 0; i < tail; ++i) bytes.push_back(static_cast<unsigned char>(i == 0 ? 1 + rng() % 255 : rng()));
			if (bytes.empty()) continue;
			uinteger_t expect;
			for (auto b : bytes) expect = (expect << 8) + uinteger_t(b);
			for (const auto& u : {uinteger_t(bytes.data(), bytes.size(), 256), uinteger_t(bytes, 256), uinteger_t(std::string(bytes.begin(), bytes.end()), 256)}) {
				CHECK(u == expect && !(u < expect) && !(u > expect), "%zu zero bytes then %zu: comparison", zeros, tail);
				CHECK(u.size() == expect.size() && u.bits() == expect.bits(), "%zu zero bytes then %zu: size %zu bits %zu, expected %zu and %zu", zeros, tail, u.size(), u.bits(), expect.size(), expect.bits());
				CHECK(static_cast<bool>(u) == (tail != 0), "%zu zero bytes then %zu: truth", zeros, tail);
				if (tail == 1) {
					const unsigned value = bytes.back();
					CHECK(u == uinteger_t(value) && (value > 1 ? u > uinteger_t(1) : u == uinteger_t(1)), "%zu zero bytes then one byte %u", zeros, value);
				}
				if (!tail) CHECK(u == uinteger_t(0) && !(u > uinteger_t(0)) && u.bits() == 0 && u.size() == 0, "%zu zero bytes: zero", zeros);
			}
			if (tail == 1 && bytes.back() == 1) {
				const uinteger_t u(bytes, 256);
				CHECK(u == uinteger_t(1) && !(u > uinteger_t(1)) && u.bits() == 1 && u.size() == 1, "%zu zero bytes then 1", zeros);
			}
		}
	}
	// A byte string of just zero bytes, and of a one at every position, for a few lengths.
	for (std::size_t n = 1; n <= 40; ++n) {
		const std::string zeros(n, '\0');
		const uinteger_t zero(zeros, 256);
		CHECK(!zero && zero.size() == 0 && zero.bits() == 0 && zero == uinteger_t(0), "%zu zero bytes are zero", n);
		for (std::size_t at = 0; at < n; ++at) {
			std::string one = zeros;
			one[at] = 1;
			const uinteger_t u(one, 256);
			CHECK(u.bits() == 8 * (n - 1 - at) + 1 && u == (uinteger_t(1) << (8 * (n - 1 - at))), "a one at byte %zu of %zu", at, n);
		}
	}
}

// The base-256 parser and writer: every byte count around the limb size and a few larger, with
// leading zero bytes and a whole zero limb, against limbs built a byte at a time. The value is
// trimmed: it has no zero top limb, and zero bytes make zero.
static void check_bytes() {
	CHECK(what_of([] { uinteger_t(static_cast<const unsigned char*>(nullptr), 0, 256); }) == "Error: Cannot convert from base 256", "empty base-256 input");
	std::vector<std::size_t> sizes;
	for (std::size_t n = 1; n <= 4 * sizeof(digit) + 3; ++n) sizes.push_back(n);
	for (std::size_t n : {63, 64, 65, 127, 128, 129, 255, 256, 257, 300, 1000, 1025}) sizes.push_back(n);
	for (std::size_t n : sizes) {
		for (int shape = 0; shape < 6; ++shape) {
			std::vector<unsigned char> bytes(n);
			for (auto& b : bytes) b = static_cast<unsigned char>(rng());
			switch (shape) {
				case 1: std::fill(bytes.begin(), bytes.end(), 0); break;
				case 2: std::fill(bytes.begin(), bytes.end(), 0xff); break;
				case 3: for (std::size_t i = 0; i < n && i < sizeof(digit); ++i) bytes[i] = 0; break;      // a whole limb of leading zeros
				case 4: for (std::size_t i = 0; i < n && i < 1 + rng() % (2 * sizeof(digit)); ++i) bytes[i] = 0; break;
				case 5: if (n) bytes[0] = 0x80; break;
			}
			// The limbs, least significant first, a byte at a time.
			std::vector<digit> limbs((n + sizeof(digit) - 1) / sizeof(digit), 0);
			for (std::size_t i = 0; i < n; ++i) {
				const std::size_t k = n - 1 - i;  // the byte's place from the least significant end
				limbs[k / sizeof(digit)] |= static_cast<digit>(static_cast<digit>(bytes[i]) << (8 * (k % sizeof(digit))));
			}
			const std::string text(bytes.begin(), bytes.end());
			const uinteger_t forms[] = {uinteger_t(bytes.data(), n, 256), uinteger_t(reinterpret_cast<const char*>(bytes.data()), n, 256),
			                            uinteger_t(bytes, 256), uinteger_t(text, 256)};
			while (!limbs.empty() && !limbs.back()) limbs.pop_back();
			for (const auto& u : forms) {
				bool same = u.size() == limbs.size();
				for (std::size_t i = 0; same && i < limbs.size(); ++i) same = u.data()[i] == limbs[i];
				CHECK(same, "base-256 parse of %zu bytes, shape %d", n, shape);
			}
			// The bytes back: no leading zeros, and one zero byte for zero.
			std::string expect = text.substr(std::min(text.find_first_not_of('\0'), text.size() - 1));
			const auto& u = forms[0];
			CHECK(u.str(256) == expect, "str(256) of %zu bytes, shape %d", n, shape);
			CHECK(u.raw() == expect, "raw() of %zu bytes, shape %d", n, shape);
			std::vector<unsigned char> as_uchar;
			u.digits(as_uchar, 256);
			CHECK(std::string(as_uchar.begin(), as_uchar.end()) == expect, "digits(vector<unsigned char>, 256) of %zu bytes", n);
			std::vector<char> as_char(2, 'x');  // appends after what is there
			as_char.reserve(3);  // (g++ 16 reports the push_back of a zero as out of bounds when it sees capacity 2)
			u.digits(as_char, 256);
			CHECK(std::string(as_char.begin() + 2, as_char.end()) == expect && as_char[0] == 'x' && as_char[1] == 'x', "digits(vector<char>, 256) appends, %zu bytes", n);
			std::vector<int> as_int;
			u.digits(as_int, 256);
			bool ints = as_int.size() == expect.size();
			for (std::size_t i = 0; ints && i < expect.size(); ++i) ints = as_int[i] == static_cast<unsigned char>(expect[i]);
			CHECK(ints, "digits(vector<int>, 256) of %zu bytes", n);
			std::string prefixed = "pre";
			u.digits(prefixed, 256);
			CHECK(prefixed == "pre" + expect, "digits(string, 256) appends, %zu bytes", n);
			CHECK(uinteger_t::from_digits(expect.data(), expect.size(), 256) == u, "from_digits round trip, %zu bytes", n);
			CHECK(uinteger_t(expect, 256) == u, "parse round trip, %zu bytes", n);
		}
	}
}

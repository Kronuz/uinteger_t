// Shifts, the public operator forms and aliasing. Included by property.cc.

// 2^k from bytes, so the shift checks do not rest on the shift operators.
static uinteger_t pow2(std::size_t k) {
	std::string bytes(k / 8 + 1, '\0');
	bytes[0] = static_cast<char>(1u << (k % 8));
	return uinteger_t(bytes, 256);
}

// Shifts by a native count, a one-limb uinteger_t count and in place, against multiplication and
// division by 2^k.
static void check_shifts(const uinteger_t& a, std::size_t k) {
	const auto p = pow2(k);
	const auto left = a * p;
	const auto right = a / p;
	const uinteger_t count(k);
	const unsigned long long kk = k;
	CHECK((a << k) == left, "a << %zu", k);
	CHECK((a << kk) == left, "a << %zu (unsigned long long)", k);
	CHECK((a << count) == left, "a << uinteger_t(%zu)", k);
	CHECK((a >> k) == right, "a >> %zu", k);
	CHECK((a >> kk) == right, "a >> %zu (unsigned long long)", k);
	CHECK((a >> count) == right, "a >> uinteger_t(%zu)", k);
	if (k < 100) {
		const int ki = static_cast<int>(k);
		CHECK((a << ki) == left && (a >> ki) == right, "a << int %d", ki);
	}
	auto t = a;
	t <<= k;
	CHECK(t == left, "a <<= %zu", k);
	t >>= k;
	CHECK(t == a, "a <<= k then >>= %zu", k);
	t = a;
	t <<= count;
	CHECK(t == left, "a <<= uinteger_t(%zu)", k);
	t >>= count;
	CHECK(t == a, "a <<= k then >>= uinteger_t(%zu)", k);
	t = a;
	t >>= k;
	CHECK(t == right, "a >>= %zu", k);
	t = a;
	t >>= count;
	CHECK(t == right, "a >>= uinteger_t(%zu)", k);
	CHECK((right << k) + a % p == a, "(a >> k << k) + a %% 2^k == a for k %zu", k);

	// The static forms with the result aliasing the operand (a right shift once ran the left one).
	t = a;
	uinteger_t::bitwise_rshift(t, t, count);
	CHECK(t == right, "bitwise_rshift in place %zu", k);
	t = a;
	uinteger_t::bitwise_lshift(t, t, count);
	CHECK(t == left, "bitwise_lshift in place %zu", k);
}

// The public output-reusing forms against the operators: a result that is an operand or the same
// value as one, results holding values of other sizes (none, one word, longer than the answer),
// and the quotient and the remainder each taking an operand's place.
static void check_public_forms(const uinteger_t& a, const uinteger_t& b) {
	const auto prod = a * b;
	for (std::size_t dirt : {std::size_t(0), std::size_t(1), a.size() + b.size() + 2}) {
		uinteger_t r = random_value(dirt);
		CHECK(&uinteger_t::mul(r, a, b) == &r && r == prod, "mul(r, a, b) over %zu words", dirt);
		CHECK(&uinteger_t::mul(r, b, a) == &r && r == prod, "mul(r, b, a) over its own product, %zu words", dirt);
	}
	uinteger_t r = a;
	uinteger_t::mul(r, r, b);
	CHECK(r == prod, "mul(r = a, a, b)");
	r = b;
	uinteger_t::mul(r, a, r);
	CHECK(r == prod, "mul(r = b, a, b)");
	r = a;
	uinteger_t::mul(r, r, r);
	CHECK(r == a * a, "mul(r = a, a, a)");
	r = a;
	uinteger_t::mul(r, r, a);
	CHECK(r == a * a, "mul(r = a, r, a)");
	if (!b) {
		return;
	}
	const auto qr = a.divmod(b);
	for (std::size_t dirt : {std::size_t(0), std::size_t(1), a.size() + b.size() + 2}) {
		uinteger_t q = random_value(dirt), rem = random_value(dirt + 1);
		const auto out = uinteger_t::divmod(q, rem, a, b);
		CHECK(&out.first.get() == &q && &out.second.get() == &rem, "divmod(q, r, a, b) returns its outputs");
		CHECK(q == qr.first && rem == qr.second, "divmod(q, r, a, b) over %zu words", dirt);
	}
	// Each output taking an operand's place: the operand is the output object itself.
	uinteger_t q, rem;
	q = a;
	uinteger_t::divmod(q, rem, q, b);
	CHECK(q == qr.first && rem == qr.second, "divmod(q, r, q = a, b)");
	q = b;
	uinteger_t::divmod(q, rem, a, q);
	CHECK(q == qr.first && rem == qr.second, "divmod(q, r, a, q = b)");
	rem = a;
	uinteger_t::divmod(q, rem, rem, b);
	CHECK(q == qr.first && rem == qr.second, "divmod(q, r, r = a, b)");
	rem = b;
	uinteger_t::divmod(q, rem, a, rem);
	CHECK(q == qr.first && rem == qr.second, "divmod(q, r, a, r = b)");
	q = a;
	rem = b;
	uinteger_t::divmod(q, rem, q, rem);
	CHECK(q == qr.first && rem == qr.second, "divmod(q, r, q = a, r = b)");
	q = b;
	rem = a;
	uinteger_t::divmod(q, rem, rem, q);
	CHECK(q == qr.first && rem == qr.second, "divmod(q, r, r = a, q = b)");
	// The same operand twice, as the output of one half.
	q = b;
	uinteger_t::divmod(q, rem, q, q);
	CHECK(q == uinteger_t(1) && !rem, "divmod(q, r, q = b, q)");
	rem = b;
	uinteger_t::divmod(q, rem, rem, rem);
	CHECK(q == uinteger_t(1) && !rem, "divmod(q, r, r = b, r)");
}

// Operands and results that are the same object, and a subtraction that wraps.
static void check_alias(const uinteger_t& a, const uinteger_t& b) {
	check_public_forms(a, b);
	const auto sum = a + b;
	const auto diff = a - b;
	uinteger_t r = a;
	uinteger_t::add(r, r, b);
	CHECK(r == sum, "add(r, r, b)");
	r = b;
	uinteger_t::add(r, a, r);
	CHECK(r == sum, "add(r, a, r)");
	r = a;
	uinteger_t::add(r, r, r);
	CHECK(r == a + a, "add(r, r, r)");
	r = a;
	r += r;
	CHECK(r == a + a, "r += r");
	r = a;
	uinteger_t::sub(r, r, b);
	CHECK(r == diff, "sub(r, r, b)");
	r = b;
	uinteger_t::sub(r, a, r);
	CHECK(r == diff, "sub(r, a, r)");
	r = a;
	r -= r;
	CHECK(!r, "r -= r");
	r = a;
	r *= r;
	CHECK(r == a * a, "r *= r");

	// a - b wraps modulo B^max(size): adding b back gives a, or a + B^n when it wrapped.
	const auto n = std::max(a.size(), b.size()) * uinteger_t::digit_bits;
	CHECK(diff.size() <= std::max(a.size(), b.size()), "a - b wider than its operands");
	CHECK(diff + b == (a >= b ? a : a + pow2(n)), "(a - b) + b");

	if (b) {
		const auto qr = a.divmod(b);
		r = a;
		r /= b;
		CHECK(r == qr.first, "r /= b");
		r = a;
		r %= b;
		CHECK(r == qr.second, "r %%= b");
		r = b;
		r /= r;
		CHECK(r == uinteger_t(1), "r /= r");
		r = b;
		r %= r;
		CHECK(!r, "r %%= r");
	}
}

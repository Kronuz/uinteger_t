// Constructors, conversions and the operators. Class body of uinteger_t, included inside it by uinteger_t.hh; not a header.

UINTEGER_T_PRIVATE:
	// Constructors

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	void _uint_t(const T& value) {
		append(static_cast<digit>(value));
	}

	template <typename T, typename... Args, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	void _uint_t(const T& value, Args... args) {
		_uint_t(args...);
		append(static_cast<digit>(value));
	}

public:
	uinteger_t() = default;

	uinteger_t(const uinteger_t& o) = default;

	uinteger_t(uinteger_t&& o) noexcept = default;

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t(const T& value) {
		set_digit(static_cast<digit>(value));
	}

	template <typename T, typename... Args, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t(const T& value, Args... args) {
		_uint_t(args...);
		append(static_cast<digit>(value));
		trim();
	}

	template <typename T, typename... Args, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t(std::initializer_list<T> list) {
		reserve(list.size());
		for (const auto& value : list) {
			append(static_cast<digit>(value));
		}
		trim();
	}

	template <typename T, std::size_t N>
	explicit uinteger_t(T (&&s)[N], int base=10) :
		uinteger_t(s, N - 1, base) { }

	explicit uinteger_t(const unsigned char* bytes, std::size_t sz, int base) {
		_parse(bytes, sz, base);
	}

	explicit uinteger_t(const char* bytes, std::size_t sz, int base) {
		_parse(bytes, sz, base);
	}

	template <typename T>
	explicit uinteger_t(const std::vector<T>& bytes, int base=10) :
		uinteger_t(bytes.data(), bytes.size(), base) { }

	explicit uinteger_t(const std::string& bytes, int base=10) :
		uinteger_t(bytes.data(), bytes.size(), base) { }

	// Assignment Operator
	uinteger_t& operator=(const uinteger_t& o) = default;
	uinteger_t& operator=(uinteger_t&& o) noexcept = default;

	// Typecast Operators
	explicit operator bool() const {
		return static_cast<bool>(size());
	}
	explicit operator unsigned char() const {
		return static_cast<unsigned char>(size() ? front() : 0);
	}
	explicit operator unsigned short() const {
		return static_cast<unsigned short>(size() ? front() : 0);
	}
	explicit operator unsigned int() const {
		return static_cast<unsigned int>(size() ? front() : 0);
	}
	explicit operator unsigned long() const {
		return static_cast<unsigned long>(size() ? front() : 0);
	}
	explicit operator unsigned long long() const {
		return static_cast<unsigned long long>(size() ? front() : 0);
	}
	explicit operator char() const {
		return static_cast<char>(size() ? front() : 0);
	}
	explicit operator signed char() const {
		return static_cast<signed char>(size() ? front() : 0);
	}
	explicit operator short() const {
		return static_cast<short>(size() ? front() : 0);
	}
	explicit operator int() const {
		return static_cast<int>(size() ? front() : 0);
	}
	explicit operator long() const {
		return static_cast<long>(size() ? front() : 0);
	}
	explicit operator long long() const {
		return static_cast<long long>(size() ? front() : 0);
	}

	// Bitwise Operators
	uinteger_t operator&(const uinteger_t& rhs) const {
		return bitwise_and(*this, rhs);
	}

	uinteger_t& operator&=(const uinteger_t& rhs) {
		return bitwise_and(*this, rhs);
	}

	uinteger_t operator|(const uinteger_t& rhs) const {
		return bitwise_or(*this, rhs);
	}

	uinteger_t& operator|=(const uinteger_t& rhs) {
		return bitwise_or(*this, rhs);
	}

	uinteger_t operator^(const uinteger_t& rhs) const {
		return bitwise_xor(*this, rhs);
	}

	uinteger_t& operator^=(const uinteger_t& rhs) {
		return bitwise_xor(*this, rhs);
	}

	uinteger_t operator~() const {
		return bitwise_inv(*this);
	}

	uinteger_t inv() {
		return bitwise_inv(*this);
	}

	// Bit Shift Operators
	uinteger_t operator<<(const uinteger_t& rhs) const {
		return bitwise_lshift(*this, rhs);
	}

	uinteger_t& operator<<=(const uinteger_t& rhs) {
		return bitwise_lshift(*this, rhs);
	}

	uinteger_t operator>>(const uinteger_t& rhs) const {
		return bitwise_rshift(*this, rhs);
	}

	uinteger_t& operator>>=(const uinteger_t& rhs) {
		return bitwise_rshift(*this, rhs);
	}

	// Shifts by a native integer read the count directly (negative counts wrap to huge ones, as
	// converting the count to a uinteger_t does).
	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t operator<<(T rhs) const {
		uinteger_t result;
		lshift(result, *this, static_cast<digit>(rhs));
		return result;
	}

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t& operator<<=(T rhs) {
		return lshift(*this, *this, static_cast<digit>(rhs));
	}

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t operator>>(T rhs) const {
		uinteger_t result;
		rshift(result, *this, static_cast<digit>(rhs));
		return result;
	}

	template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
	uinteger_t& operator>>=(T rhs) {
		return rshift(*this, *this, static_cast<digit>(rhs));
	}

	// Logical Operators
	bool operator!() const {
		return !static_cast<bool>(*this);
	}

	bool operator&&(const uinteger_t& rhs) const {
		return static_cast<bool>(*this) && rhs;
	}

	bool operator||(const uinteger_t& rhs) const {
		return static_cast<bool>(*this) || rhs;
	}

	// Three-way comparison of two values: -1 when lhs < rhs, 0 when they are equal, 1 when lhs > rhs.
	// The values must be trimmed (no leading zero limb), as every operation leaves them.
	static int compare(const uinteger_t& lhs, const uinteger_t& rhs) {
		auto n = lhs.size();
		if (n != rhs.size()) {
			return n > rhs.size() ? 1 : -1;
		}
		return k_cmp_n(lhs.data(), rhs.data(), n);
	}

	// Comparison Operators
	bool operator==(const uinteger_t& rhs) const {
		return compare(*this, rhs) == 0;
	}

	bool operator!=(const uinteger_t& rhs) const {
		return compare(*this, rhs) != 0;
	}

	bool operator>(const uinteger_t& rhs) const {
		return compare(*this, rhs) > 0;
	}

	bool operator<(const uinteger_t& rhs) const {
		return compare(*this, rhs) < 0;
	}

	bool operator>=(const uinteger_t& rhs) const {
		return compare(*this, rhs) >= 0;
	}

	bool operator<=(const uinteger_t& rhs) const {
		return compare(*this, rhs) <= 0;
	}

	// Arithmetic Operators
	uinteger_t operator+(const uinteger_t& rhs) const {
		return add(*this, rhs);
	}

	uinteger_t& operator+=(const uinteger_t& rhs) {
		return add(*this, rhs);
	}

	uinteger_t operator-(const uinteger_t& rhs) const {
		return sub(*this, rhs);
	}

	uinteger_t& operator-=(const uinteger_t& rhs) {
		return sub(*this, rhs);
	}

	uinteger_t operator*(const uinteger_t& rhs) const {
		return mult(*this, rhs);
	}

	uinteger_t& operator*=(const uinteger_t& rhs) {
		return mult(*this, rhs);
	}

	// result = lhs * rhs. `result` may be `lhs` or `rhs`. A result that is neither keeps its storage,
	// so one reused with room for the product allocates nothing, unless the product's working space
	// exceeds 256 limbs; an operand taken as the result is rebuilt through a temporary. Returns `result`.
	static uinteger_t& mul(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
		return mult(result, lhs, rhs);
	}

	// quotient = lhs / rhs and remainder = lhs % rhs in one division. Either output may be `lhs` or
	// `rhs`; `quotient` and `remainder` must be different objects. An output that is not an operand
	// keeps its storage, so outputs reused with room allocate nothing, unless the division's working
	// space exceeds 256 limbs (a dividend above 73 limbs). Throws `std::domain_error` when `rhs` is
	// zero, before it writes an output. Returns references to `quotient` and `remainder`.
	static std::pair<std::reference_wrapper<uinteger_t>, std::reference_wrapper<uinteger_t>> divmod(uinteger_t& quotient, uinteger_t& remainder, const uinteger_t& lhs, const uinteger_t& rhs) {
		divide(&quotient, &remainder, lhs, rhs);
		return std::make_pair(std::ref(quotient), std::ref(remainder));
	}

	std::pair<uinteger_t, uinteger_t> divmod(const uinteger_t& rhs) const {
		return divmod(*this, rhs);
	}

	uinteger_t operator/(const uinteger_t& rhs) const {
		uinteger_t quotient;
		divide(&quotient, nullptr, *this, rhs);
		return quotient;
	}

	uinteger_t& operator/=(const uinteger_t& rhs) {
		divide(this, nullptr, *this, rhs);
		return *this;
	}

	uinteger_t operator%(const uinteger_t& rhs) const {
		uinteger_t remainder;
		divide(nullptr, &remainder, *this, rhs);
		return remainder;
	}

	uinteger_t& operator%=(const uinteger_t& rhs) {
		divide(nullptr, this, *this, rhs);
		return *this;
	}

	// Increment Operator
	uinteger_t& operator++() {
		return *this += uint_1();
	}
	uinteger_t operator++(int) {
		uinteger_t temp(*this);
		++*this;
		return temp;
	}

	// Decrement Operator
	uinteger_t& operator--() {
		return *this -= uint_1();
	}
	uinteger_t operator--(int) {
		uinteger_t temp(*this);
		--*this;
		return temp;
	}

	// Nothing done since promotion doesn't work here
	uinteger_t operator+() const {
		return *this;
	}

	// two's complement
	uinteger_t operator-() const {
		return uint_0() - *this;
	}

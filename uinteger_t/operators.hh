// Constructors, conversions and the operators, with the free operators taking an integral left operand. Included by uinteger_t.hh.

#pragma once

#include "../uinteger_t.hh"

inline const uinteger_t& uinteger_t::uint_0() {
	static uinteger_t uint_0(0);
	return uint_0;
}

inline const uinteger_t& uinteger_t::uint_1() {
	static uinteger_t uint_1(1);
	return uint_1;
}

template <typename T, typename>
inline void uinteger_t::_uint_t(const T& value) {
	append(static_cast<digit>(value));
}

template <typename T, typename... Args, typename>
inline void uinteger_t::_uint_t(const T& value, Args... args) {
	_uint_t(args...);
	append(static_cast<digit>(value));
}

template <typename T, typename>
inline uinteger_t::uinteger_t(const T& value) {
	set_digit(static_cast<digit>(value));
}

template <typename T, typename... Args, typename>
inline uinteger_t::uinteger_t(const T& value, Args... args) {
	_uint_t(args...);
	append(static_cast<digit>(value));
	trim();
}

template <typename T, typename... Args, typename>
inline uinteger_t::uinteger_t(std::initializer_list<T> list) {
	reserve(list.size());
	for (const auto& value : list) {
		append(static_cast<digit>(value));
	}
	trim();
}

template <typename T, std::size_t N>
inline uinteger_t::uinteger_t(T (&&s)[N], int base) :
	uinteger_t(s, N - 1, base) { }

inline uinteger_t::uinteger_t(const unsigned char* bytes, std::size_t sz, int base) {
	_parse(bytes, sz, base);
}

inline uinteger_t::uinteger_t(const char* bytes, std::size_t sz, int base) {
	_parse(bytes, sz, base);
}

template <typename T>
inline uinteger_t::uinteger_t(const std::vector<T>& bytes, int base) :
	uinteger_t(bytes.data(), bytes.size(), base) { }

inline uinteger_t::uinteger_t(const std::string& bytes, int base) :
	uinteger_t(bytes.data(), bytes.size(), base) { }

inline uinteger_t::operator bool() const {
	return static_cast<bool>(size());
}

inline uinteger_t::operator unsigned char() const {
	return static_cast<unsigned char>(size() ? front() : 0);
}

inline uinteger_t::operator unsigned short() const {
	return static_cast<unsigned short>(size() ? front() : 0);
}

inline uinteger_t::operator unsigned int() const {
	return static_cast<unsigned int>(size() ? front() : 0);
}

inline uinteger_t::operator unsigned long() const {
	return static_cast<unsigned long>(size() ? front() : 0);
}

inline uinteger_t::operator unsigned long long() const {
	return static_cast<unsigned long long>(size() ? front() : 0);
}

inline uinteger_t::operator char() const {
	return static_cast<char>(size() ? front() : 0);
}

inline uinteger_t::operator signed char() const {
	return static_cast<signed char>(size() ? front() : 0);
}

inline uinteger_t::operator short() const {
	return static_cast<short>(size() ? front() : 0);
}

inline uinteger_t::operator int() const {
	return static_cast<int>(size() ? front() : 0);
}

inline uinteger_t::operator long() const {
	return static_cast<long>(size() ? front() : 0);
}

inline uinteger_t::operator long long() const {
	return static_cast<long long>(size() ? front() : 0);
}

inline uinteger_t uinteger_t::operator&(const uinteger_t& rhs) const {
	return bitwise_and(*this, rhs);
}

inline uinteger_t& uinteger_t::operator&=(const uinteger_t& rhs) {
	return bitwise_and(*this, rhs);
}

inline uinteger_t uinteger_t::operator|(const uinteger_t& rhs) const {
	return bitwise_or(*this, rhs);
}

inline uinteger_t& uinteger_t::operator|=(const uinteger_t& rhs) {
	return bitwise_or(*this, rhs);
}

inline uinteger_t uinteger_t::operator^(const uinteger_t& rhs) const {
	return bitwise_xor(*this, rhs);
}

inline uinteger_t& uinteger_t::operator^=(const uinteger_t& rhs) {
	return bitwise_xor(*this, rhs);
}

inline uinteger_t uinteger_t::operator~() const {
	return bitwise_inv(*this);
}

inline uinteger_t uinteger_t::inv() {
	return bitwise_inv(*this);
}

inline uinteger_t uinteger_t::operator<<(const uinteger_t& rhs) const {
	return bitwise_lshift(*this, rhs);
}

inline uinteger_t& uinteger_t::operator<<=(const uinteger_t& rhs) {
	return bitwise_lshift(*this, rhs);
}

inline uinteger_t uinteger_t::operator>>(const uinteger_t& rhs) const {
	return bitwise_rshift(*this, rhs);
}

inline uinteger_t& uinteger_t::operator>>=(const uinteger_t& rhs) {
	return bitwise_rshift(*this, rhs);
}

template <typename T, typename>
inline uinteger_t uinteger_t::operator<<(T rhs) const {
	uinteger_t result;
	lshift(result, *this, static_cast<digit>(rhs));
	return result;
}

template <typename T, typename>
inline uinteger_t& uinteger_t::operator<<=(T rhs) {
	return lshift(*this, *this, static_cast<digit>(rhs));
}

template <typename T, typename>
inline uinteger_t uinteger_t::operator>>(T rhs) const {
	uinteger_t result;
	rshift(result, *this, static_cast<digit>(rhs));
	return result;
}

template <typename T, typename>
inline uinteger_t& uinteger_t::operator>>=(T rhs) {
	return rshift(*this, *this, static_cast<digit>(rhs));
}

inline bool uinteger_t::operator!() const {
	return !static_cast<bool>(*this);
}

inline bool uinteger_t::operator&&(const uinteger_t& rhs) const {
	return static_cast<bool>(*this) && rhs;
}

inline bool uinteger_t::operator||(const uinteger_t& rhs) const {
	return static_cast<bool>(*this) || rhs;
}

inline int uinteger_t::compare(const uinteger_t& lhs, const uinteger_t& rhs) {
	auto n = lhs.size();
	if (n != rhs.size()) {
		return n > rhs.size() ? 1 : -1;
	}
	return k_cmp_n(lhs.data(), rhs.data(), n);
}

inline bool uinteger_t::operator==(const uinteger_t& rhs) const {
	return compare(*this, rhs) == 0;
}

inline bool uinteger_t::operator!=(const uinteger_t& rhs) const {
	return compare(*this, rhs) != 0;
}

inline bool uinteger_t::operator>(const uinteger_t& rhs) const {
	return compare(*this, rhs) > 0;
}

inline bool uinteger_t::operator<(const uinteger_t& rhs) const {
	return compare(*this, rhs) < 0;
}

inline bool uinteger_t::operator>=(const uinteger_t& rhs) const {
	return compare(*this, rhs) >= 0;
}

inline bool uinteger_t::operator<=(const uinteger_t& rhs) const {
	return compare(*this, rhs) <= 0;
}

inline uinteger_t uinteger_t::operator+(const uinteger_t& rhs) const {
	return add(*this, rhs);
}

inline uinteger_t& uinteger_t::operator+=(const uinteger_t& rhs) {
	return add(*this, rhs);
}

inline uinteger_t uinteger_t::operator-(const uinteger_t& rhs) const {
	return sub(*this, rhs);
}

inline uinteger_t& uinteger_t::operator-=(const uinteger_t& rhs) {
	return sub(*this, rhs);
}

inline uinteger_t uinteger_t::operator*(const uinteger_t& rhs) const {
	return mult(*this, rhs);
}

inline uinteger_t& uinteger_t::operator*=(const uinteger_t& rhs) {
	return mult(*this, rhs);
}

inline uinteger_t& uinteger_t::mul(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs) {
	return mult(result, lhs, rhs);
}

inline std::pair<std::reference_wrapper<uinteger_t>, std::reference_wrapper<uinteger_t>> uinteger_t::divmod(uinteger_t& quotient, uinteger_t& remainder, const uinteger_t& lhs, const uinteger_t& rhs) {
	divide(&quotient, &remainder, lhs, rhs);
	return std::make_pair(std::ref(quotient), std::ref(remainder));
}

inline std::pair<uinteger_t, uinteger_t> uinteger_t::divmod(const uinteger_t& rhs) const {
	return divmod(*this, rhs);
}

inline uinteger_t uinteger_t::operator/(const uinteger_t& rhs) const {
	uinteger_t quotient;
	divide(&quotient, nullptr, *this, rhs);
	return quotient;
}

inline uinteger_t& uinteger_t::operator/=(const uinteger_t& rhs) {
	divide(this, nullptr, *this, rhs);
	return *this;
}

inline uinteger_t uinteger_t::operator%(const uinteger_t& rhs) const {
	uinteger_t remainder;
	divide(nullptr, &remainder, *this, rhs);
	return remainder;
}

inline uinteger_t& uinteger_t::operator%=(const uinteger_t& rhs) {
	divide(nullptr, this, *this, rhs);
	return *this;
}

inline uinteger_t& uinteger_t::operator++() {
	return *this += uint_1();
}

inline uinteger_t uinteger_t::operator++(int) {
	uinteger_t temp(*this);
	++*this;
	return temp;
}

inline uinteger_t& uinteger_t::operator--() {
	return *this -= uint_1();
}

inline uinteger_t uinteger_t::operator--(int) {
	uinteger_t temp(*this);
	--*this;
	return temp;
}

inline uinteger_t uinteger_t::operator+() const {
	return *this;
}

inline uinteger_t uinteger_t::operator-() const {
	return uint_0() - *this;
}

namespace std {  // This is probably not a good idea
	// Make it work with std::string()
	inline std::string to_string(uinteger_t& num) {
		return num.str();
	}
	inline const std::string to_string(const uinteger_t& num) {
		return num.str();
	}
}

// lhs type T as first arguemnt
// If the output is not a bool, casts to type T

// Bitwise Operators
template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator&(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) & rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator&=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(rhs & lhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator|(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) | rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator|=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(rhs | lhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator^(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) ^ rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator^=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(rhs ^ lhs);
}

// Bitshift operators
template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
inline uinteger_t operator<<(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) << rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator<<=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(lhs << rhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
inline uinteger_t operator>>(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) >> rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator>>=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(lhs >> rhs);
}

// Comparison Operators
template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
bool operator==(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) == rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
bool operator!=(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) != rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
bool operator>(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) > rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
bool operator<(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) < rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
bool operator>=(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) >= rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
bool operator<=(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) <= rhs;
}

// Arithmetic Operators
template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator+(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) + rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator+=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(rhs + lhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator-(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) - rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator-=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(lhs - rhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator*(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) * rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator*=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(rhs * lhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator/(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) / rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator/=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(lhs / rhs);
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
uinteger_t operator%(const T& lhs, const uinteger_t& rhs) {
	return uinteger_t(lhs) % rhs;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral<T>::value and not std::is_same<T, std::decay_t<uinteger_t>>::value>>
T& operator%=(T& lhs, const uinteger_t& rhs) {
	return lhs = static_cast<T>(lhs % rhs);
}

// IO Operator
inline std::ostream& operator<<(std::ostream& stream, const uinteger_t& rhs) {
	if (stream.flags() & stream.oct) {
		stream << rhs.str(8);
	} else if (stream.flags() & stream.dec) {
		stream << rhs.str(10);
	} else if (stream.flags() & stream.hex) {
		stream << rhs.str(16);
	}
	return stream;
}

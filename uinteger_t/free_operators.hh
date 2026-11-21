// The operators with an integral left operand, the stream operator and the std overloads. Included by uinteger_t.hh.

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

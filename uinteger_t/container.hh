// The limb container and the value's access to its limbs: storage, growth, iteration, trim and bit length. Included by uinteger_t.hh.

#pragma once

#include "../uinteger_t.hh"

inline void uinteger_t::container::copy_limbs(digit* to, const digit* from, size_type n) {
#if defined __GNUC__ && !defined __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
	if (n) {
		std::memmove(to, from, n * sizeof(digit));
	}
#if defined __GNUC__ && !defined __clang__
#pragma GCC diagnostic pop
#endif
}

inline void uinteger_t::container::release() noexcept {
	if (_p != _local) {
		delete[] _p;
	}
}

inline UINTEGER_T_NOINLINE void uinteger_t::container::reallocate(size_type n) {
	if (n > max_size()) {
		throw std::length_error("Error: container too large");
	}
	digit* p = new digit[n];
	copy_limbs(p, _p, _size);
	release();
	_p = p;
	_cap = n;
}

inline UINTEGER_T_NOINLINE void uinteger_t::container::grow(size_type n) {
	reallocate(_cap > inline_capacity && n <= max_size() ? std::min(std::max(n, 2 * _cap), max_size()) : n);
}

inline UINTEGER_T_NOINLINE void uinteger_t::container::replace(size_type n) {
	if (n > max_size()) {
		throw std::length_error("Error: container too large");
	}
	digit* p = new digit[n];
	release();
	_p = p;
	_cap = n;
}

inline UINTEGER_T_NOINLINE void uinteger_t::container::copy_to_heap(const container& o) {
	_p = new digit[o._size];
	_cap = o._size;
	copy_limbs(_p, o._p, o._size);
}

inline uinteger_t::container::container() noexcept : _p(_local), _size(0), _cap(inline_capacity) { }

inline uinteger_t::container::container(const container& o) : _p(_local), _size(o._size), _cap(inline_capacity) {
	if (_size > inline_capacity) {
		copy_to_heap(o);
	} else {
		copy_limbs(_local, o._p, _size);
	}
}

inline uinteger_t::container::container(container&& o) noexcept : _p(_local), _size(o._size), _cap(inline_capacity) {
	if (o._cap > inline_capacity) {
		_p = o._p;
		_cap = o._cap;
		o._p = o._local;
		o._cap = inline_capacity;
	} else {
		copy_limbs(_local, o._local, _size);
	}
	o._size = 0;
}

inline uinteger_t::container::~container() {
	release();
}

inline uinteger_t::container& uinteger_t::container::operator=(const container& o) {
	if (this != &o) {
		assign(o._p, o._p + o._size);
	}
	return *this;
}

inline uinteger_t::container& uinteger_t::container::operator=(container&& o) noexcept {
	if (this != &o) {
		if (o._cap > inline_capacity) {
			release();
			_p = o._p;
			_cap = o._cap;
			o._p = o._local;
			o._cap = inline_capacity;
		} else {
			copy_limbs(_p, o._local, o._size);
		}
		_size = o._size;
		o._size = 0;
	}
	return *this;
}

inline void uinteger_t::container::assign(const digit* first, const digit* last) {
	const size_type n = static_cast<size_type>(last - first);
	if (n > _cap) {
		replace(n);
	}
	copy_limbs(_p, first, n);
	_size = n;
}

inline void uinteger_t::container::assign(size_type n, digit v) {
	if (n > _cap) {
		replace(n);
	}
	std::fill_n(_p, n, v);
	_size = n;
}

inline uinteger_t::container::size_type uinteger_t::container::size() const noexcept {
	return _size;
}

inline uinteger_t::container::size_type uinteger_t::container::capacity() const noexcept {
	return _cap;
}

inline bool uinteger_t::container::empty() const noexcept {
	return !_size;
}

inline constexpr uinteger_t::container::size_type uinteger_t::container::max_size() noexcept {
	return static_cast<size_type>(std::numeric_limits<difference_type>::max()) / sizeof(digit);
}

inline uinteger_t::digit* uinteger_t::container::data() noexcept {
	return _p;
}

inline const uinteger_t::digit* uinteger_t::container::data() const noexcept {
	return _p;
}

inline uinteger_t::container::iterator uinteger_t::container::begin() noexcept { return data(); }

inline uinteger_t::container::const_iterator uinteger_t::container::begin() const noexcept { return data(); }

inline uinteger_t::container::const_iterator uinteger_t::container::cbegin() const noexcept { return data(); }

inline uinteger_t::container::iterator uinteger_t::container::end() noexcept { return data() + _size; }

inline uinteger_t::container::const_iterator uinteger_t::container::end() const noexcept { return data() + _size; }

inline uinteger_t::container::const_iterator uinteger_t::container::cend() const noexcept { return data() + _size; }

inline uinteger_t::container::reverse_iterator uinteger_t::container::rbegin() noexcept { return reverse_iterator(end()); }

inline uinteger_t::container::const_reverse_iterator uinteger_t::container::rbegin() const noexcept { return const_reverse_iterator(end()); }

inline uinteger_t::container::const_reverse_iterator uinteger_t::container::crbegin() const noexcept { return const_reverse_iterator(end()); }

inline uinteger_t::container::reverse_iterator uinteger_t::container::rend() noexcept { return reverse_iterator(begin()); }

inline uinteger_t::container::const_reverse_iterator uinteger_t::container::rend() const noexcept { return const_reverse_iterator(begin()); }

inline uinteger_t::container::const_reverse_iterator uinteger_t::container::crend() const noexcept { return const_reverse_iterator(begin()); }

inline uinteger_t::container::reference uinteger_t::container::operator[](size_type i) noexcept { return data()[i]; }

inline uinteger_t::container::const_reference uinteger_t::container::operator[](size_type i) const noexcept { return data()[i]; }

inline uinteger_t::container::reference uinteger_t::container::front() noexcept { return data()[0]; }

inline uinteger_t::container::const_reference uinteger_t::container::front() const noexcept { return data()[0]; }

inline uinteger_t::container::reference uinteger_t::container::back() noexcept { return data()[_size - 1]; }

inline uinteger_t::container::const_reference uinteger_t::container::back() const noexcept { return data()[_size - 1]; }

inline void uinteger_t::container::reserve(size_type n) {
	if (n > _cap) {
		reallocate(n);
	}
}

inline void uinteger_t::container::clear() noexcept {
	_size = 0;
}

inline void uinteger_t::container::resize(size_type n) {
	resize(n, 0);
}

inline void uinteger_t::container::resize(size_type n, digit c) {
	if (n > _size) {
		if (n > _cap) {
			grow(n);
		}
		std::fill(data() + _size, data() + n, c);
	}
	_size = n;
}

inline void uinteger_t::container::resize_uninit(size_type n) {
	if (n > _cap) {
		grow(n);
	}
#ifdef UINTEGER_T_POISON_LIMBS
	if (n > _size) {
		std::fill(data() + _size, data() + n, static_cast<digit>(0xa5a5a5a5a5a5a5a5ull));
	}
#endif
	_size = n;
}

inline void uinteger_t::container::push_back(digit v) {
	if (_size == _cap) {
		grow(_size + 1);
	}
	data()[_size++] = v;
}

inline UINTEGER_T_NOINLINE uinteger_t::container::iterator uinteger_t::container::insert(const_iterator pos, size_type n, digit c) {
	const size_type at = static_cast<size_type>(pos - begin());
	if (n > max_size() - _size) {
		throw std::length_error("Error: container too large");
	}
	const size_type total = _size + n;
	if (total > _cap) {
		const size_type cap = _cap > inline_capacity ? std::min(std::max(total, 2 * _cap), max_size()) : total;
		digit* p = new digit[cap];
		copy_limbs(p, _p, at);
		std::fill_n(p + at, n, c);
		copy_limbs(p + at + n, _p + at, _size - at);
		release();
		_p = p;
		_cap = cap;
	} else if (n) {
		digit* d = _p;
		std::copy_backward(d + at, d + _size, d + total);
		std::fill_n(d + at, n, c);
	}
	_size = total;
	return begin() + at;
}

inline void uinteger_t::reserve(std::size_t sz) {
	_value.reserve(sz);
}

inline std::size_t uinteger_t::grow(std::size_t n) {
	// expands the vector using a growth factor
	// and returns the new capacity.
	auto cc = _value.capacity();
	if (n > cc) {
		cc = n * growth_factor;
		_value.reserve(cc);
	}
	return cc;
}

inline void uinteger_t::resize(std::size_t sz) {
	_value.resize(sz);
}

inline void uinteger_t::resize(std::size_t sz, const digit& c) {
	_value.resize(sz, c);
}

inline void uinteger_t::clear() {
	_value.clear();
}

inline uinteger_t::digit* uinteger_t::data() noexcept {
	return _value.data();
}

inline const uinteger_t::digit* uinteger_t::data() const noexcept {
	return _value.data();
}

inline std::size_t uinteger_t::size() const noexcept {
	return _value.size();
}

inline void uinteger_t::prepend(std::size_t sz, const digit& c) {
	_value.insert(_value.begin(), sz, c);
}

inline void uinteger_t::prepend(const digit& c) {
	prepend(1, c);
}

inline void uinteger_t::prepend(const uinteger_t& num) {
	prepend(num.size(), 0);
	std::copy(num.begin(), num.end(), begin());
}

inline void uinteger_t::append(std::size_t sz, const digit& c) {
	// Efficiently append by growing by growth factor
	auto nsz = _value.size() + sz;
	grow(nsz);
	_value.resize(nsz, c);
}

inline void uinteger_t::append(const digit& c) {
	append(1, c);
}

inline void uinteger_t::append(const uinteger_t& num) {
	auto sz = num.size();
	append(sz, 0);
	std::copy(num.begin(), num.end(), end() - sz);
}

inline uinteger_t::container::iterator uinteger_t::begin() noexcept {
	return _value.begin();
}

inline uinteger_t::container::const_iterator uinteger_t::begin() const noexcept {
	return _value.cbegin();
}

inline uinteger_t::container::iterator uinteger_t::end() noexcept {
	return _value.end();
}

inline uinteger_t::container::const_iterator uinteger_t::end() const noexcept {
	return _value.cend();
}

inline uinteger_t::container::reverse_iterator uinteger_t::rbegin() noexcept {
	return _value.rbegin();
}

inline uinteger_t::container::const_reverse_iterator uinteger_t::rbegin() const noexcept {
	return _value.crbegin();
}

inline uinteger_t::container::reverse_iterator uinteger_t::rend() noexcept {
	return _value.rend();
}

inline uinteger_t::container::const_reverse_iterator uinteger_t::rend() const noexcept {
	return _value.crend();
}

inline uinteger_t::container::reference uinteger_t::front() {
	return _value.front();
}

inline uinteger_t::container::const_reference uinteger_t::front() const {
	return _value.front();
}

inline uinteger_t::container::reference uinteger_t::back() {
	return _value.back();
}

inline uinteger_t::container::const_reference uinteger_t::back() const {
	return _value.back();
}

inline void uinteger_t::trim(digit mask) {
	auto n = _value.size();
	digit* p = _value.data();

	// Masks the last value of internal vector
	mask &= (digit_bits - 1);
	if (mask && n) {
		p[n - 1] &= (static_cast<digit>(1) << mask) - 1;
	}

	// Removes all unused zeros from the internal vector
	auto m = n;
	while (m && !p[m - 1]) {
		--m;
	}
	if (m != n) {
		_value.resize(m); // shrink
	}
}

inline void uinteger_t::resize_uninit(std::size_t n) {
	_value.resize_uninit(n);
}

inline const uinteger_t::digit& uinteger_t::value(std::size_t idx) const {
	static const digit zero = 0;
	return idx < size() ? *(begin() + idx) : zero;
}

inline bool uinteger_t::operator[](std::size_t n) const {
	auto nd = n / digit_bits;
	auto nm = n % digit_bits;
	return nd < size() ? (*(begin() + nd) >> nm) & 1 : 0;
}

inline std::size_t uinteger_t::bits() const {
	auto sz = size();
	if (sz) {
		return _bits(back()) + (sz - 1) * digit_bits;
	}
	return 0;
}

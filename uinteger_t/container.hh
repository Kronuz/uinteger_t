// The limb container, the value's inline-or-heap vector of limbs, and the value's access to it. Included inside class uinteger_t by uinteger_t.hh.

	// The limbs of a value: a vector of `digit` that keeps up to `inline_capacity` limbs inside the
	// object and uses a heap block beyond that. `data()` is a pointer member, to the buffer or to
	// the block (the block is in use exactly when `capacity() > inline_capacity`), so reaching the
	// limbs reads nothing the limbs' own stores could delay. Growing zero-fills the new limbs;
	// shrinking keeps the storage. Growing past the capacity moves the limbs, which invalidates
	// pointers into them. A move of an inline value copies its limbs, of a heap value takes the
	// block; the source is left empty. The paths that allocate are out of line, so the rest
	// inlines into the arithmetic.
	class container {
	public:
		using value_type = digit;
		using size_type = std::size_t;
		using difference_type = std::ptrdiff_t;
		using reference = digit&;
		using const_reference = const digit&;
		using pointer = digit*;
		using const_pointer = const digit*;
		using iterator = digit*;
		using const_iterator = const digit*;
		using reverse_iterator = std::reverse_iterator<iterator>;
		using const_reverse_iterator = std::reverse_iterator<const_iterator>;

		// Read it by value (`constexpr size_type n = container::inline_capacity;`): the header
		// gives it no out-of-class definition, so a reference to it would not link before C++17.
		static constexpr size_type inline_capacity = UINTEGER_T_INLINE_LIMBS;
		static_assert(inline_capacity >= 1, "UINTEGER_T_INLINE_LIMBS must be at least 1");

	private:
		digit* _p;       // _local, or the heap block
		size_type _size;
		size_type _cap;  // inline_capacity while the limbs are inline
		digit _local[inline_capacity];

		// Copies `n` limbs; the ranges may overlap. g++ cannot see that a value's size bounds the
		// buffer it copies from and to, so it reports out-of-bounds and uninitialized reads that
		// the invariants rule out.
		static void copy_limbs(digit* to, const digit* from, size_type n) {
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

		// Frees the heap block, if there is one. The caller sets `_p` and `_cap` next.
		void release() noexcept {
			if (_p != _local) {
				delete[] _p;
			}
		}

		// Moves the limbs to a new heap block of exactly `n` limbs (`n > _cap`). Throws before
		// changing anything.
		UINTEGER_T_NOINLINE void reallocate(size_type n) {
			if (n > max_size()) {
				throw std::length_error("Error: container too large");
			}
			digit* p = new digit[n];
			copy_limbs(p, _p, _size);
			release();
			_p = p;
			_cap = n;
		}

		// Room for `n` limbs (`n > _cap`): exact from the inline buffer, at least doubled from a
		// heap block, so repeated growth is linear overall.
		UINTEGER_T_NOINLINE void grow(size_type n) {
			reallocate(_cap > inline_capacity && n <= max_size() ? std::min(std::max(n, 2 * _cap), max_size()) : n);
		}

		// A heap block of exactly `n` limbs for a value about to be overwritten (`n > _cap`).
		UINTEGER_T_NOINLINE void replace(size_type n) {
			if (n > max_size()) {
				throw std::length_error("Error: container too large");
			}
			digit* p = new digit[n];
			release();
			_p = p;
			_cap = n;
		}

		// Copy construction of a value too long to be inline (`_cap` is still `inline_capacity`).
		UINTEGER_T_NOINLINE void copy_to_heap(const container& o) {
			_p = new digit[o._size];
			_cap = o._size;
			copy_limbs(_p, o._p, o._size);
		}

	public:
		container() noexcept : _p(_local), _size(0), _cap(inline_capacity) { }

		container(const container& o) : _p(_local), _size(o._size), _cap(inline_capacity) {
			if (_size > inline_capacity) {
				copy_to_heap(o);
			} else {
				copy_limbs(_local, o._p, _size);
			}
		}

		container(container&& o) noexcept : _p(_local), _size(o._size), _cap(inline_capacity) {
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

		~container() {
			release();
		}

		// Copy assignment reuses the storage when the limbs fit; otherwise it allocates before
		// it releases, so a failure leaves `*this` unchanged.
		container& operator=(const container& o) {
			if (this != &o) {
				assign(o._p, o._p + o._size);
			}
			return *this;
		}

		container& operator=(container&& o) noexcept {
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

		// Replaces the limbs with a copy of [first, last), which must not lie inside this container.
		void assign(const digit* first, const digit* last) {
			const size_type n = static_cast<size_type>(last - first);
			if (n > _cap) {
				replace(n);
			}
			copy_limbs(_p, first, n);
			_size = n;
		}

		// Replaces the limbs with `n` copies of `v`.
		void assign(size_type n, digit v) {
			if (n > _cap) {
				replace(n);
			}
			std::fill_n(_p, n, v);
			_size = n;
		}

		size_type size() const noexcept {
			return _size;
		}

		size_type capacity() const noexcept {
			return _cap;
		}

		bool empty() const noexcept {
			return !_size;
		}

		static constexpr size_type max_size() noexcept {
			return static_cast<size_type>(std::numeric_limits<difference_type>::max()) / sizeof(digit);
		}

		digit* data() noexcept {
			return _p;
		}

		const digit* data() const noexcept {
			return _p;
		}

		iterator begin() noexcept { return data(); }
		const_iterator begin() const noexcept { return data(); }
		const_iterator cbegin() const noexcept { return data(); }
		iterator end() noexcept { return data() + _size; }
		const_iterator end() const noexcept { return data() + _size; }
		const_iterator cend() const noexcept { return data() + _size; }
		reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
		const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
		const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }
		reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
		const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
		const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

		reference operator[](size_type i) noexcept { return data()[i]; }
		const_reference operator[](size_type i) const noexcept { return data()[i]; }
		reference front() noexcept { return data()[0]; }
		const_reference front() const noexcept { return data()[0]; }
		reference back() noexcept { return data()[_size - 1]; }
		const_reference back() const noexcept { return data()[_size - 1]; }

		void reserve(size_type n) {
			if (n > _cap) {
				reallocate(n);
			}
		}

		void clear() noexcept {
			_size = 0;
		}

		void resize(size_type n) {
			resize(n, 0);
		}

		void resize(size_type n, digit c) {
			if (n > _size) {
				if (n > _cap) {
					grow(n);
				}
				std::fill(data() + _size, data() + n, c);
			}
			_size = n;
		}

		// Resizes without initializing new limbs, for a caller that writes them before it reads
		// them. UINTEGER_T_POISON_LIMBS fills them with a pattern, so a test can catch a limb left unwritten.
		void resize_uninit(size_type n) {
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

		void push_back(digit v) {
			if (_size == _cap) {
				grow(_size + 1);
			}
			data()[_size++] = v;
		}

		// Inserts `n` copies of `c` before `pos`; returns the position of the first.
		UINTEGER_T_NOINLINE iterator insert(const_iterator pos, size_type n, digit c) {
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
	};

	// Whether `T` can receive digits from `str()`, `digits()` and `raw()`: a `std::basic_string` or a
	// `std::vector`. base-x uses it to constrain its own result types the same way.
	template <typename T>
	struct is_result {
		static const bool value = false;
	};

	template <typename T, typename Alloc>
	struct is_result<std::vector<T, Alloc>> {
		static const bool value = true;
	};

	template <typename charT, typename traits, typename Alloc>
	struct is_result<std::basic_string<charT, traits, Alloc>> {
		static const bool value = true;
	};

UINTEGER_T_PRIVATE:
	static_assert(digit_octets == half_digit_octets * 2, "half_digit must be exactly half the size of digit");

	static constexpr double growth_factor = 1.5;

	// The limbs, least significant first, with no leading zero limb (zero is empty).
	container _value;

public:
	// Limb storage

	void reserve(std::size_t sz) {
		_value.reserve(sz);
	}

	std::size_t grow(std::size_t n) {
		// expands the vector using a growth factor
		// and returns the new capacity.
		auto cc = _value.capacity();
		if (n > cc) {
			cc = n * growth_factor;
			_value.reserve(cc);
		}
		return cc;
	}

	void resize(std::size_t sz) {
		_value.resize(sz);
	}

	void resize(std::size_t sz, const digit& c) {
		_value.resize(sz, c);
	}

	void clear() {
		_value.clear();
	}

	digit* data() noexcept {
		return _value.data();
	}

	const digit* data() const noexcept {
		return _value.data();
	}

	std::size_t size() const noexcept {
		return _value.size();
	}

	void prepend(std::size_t sz, const digit& c) {
		_value.insert(_value.begin(), sz, c);
	}

	void prepend(const digit& c) {
		prepend(1, c);
	}

	void prepend(const uinteger_t& num) {
		prepend(num.size(), 0);
		std::copy(num.begin(), num.end(), begin());
	}

	void append(std::size_t sz, const digit& c) {
		// Efficiently append by growing by growth factor
		auto nsz = _value.size() + sz;
		grow(nsz);
		_value.resize(nsz, c);
	}

	void append(const digit& c) {
		append(1, c);
	}

	void append(const uinteger_t& num) {
		auto sz = num.size();
		append(sz, 0);
		std::copy(num.begin(), num.end(), end() - sz);
	}

	container::iterator begin() noexcept {
		return _value.begin();
	}

	container::const_iterator begin() const noexcept {
		return _value.cbegin();
	}

	container::iterator end() noexcept {
		return _value.end();
	}

	container::const_iterator end() const noexcept {
		return _value.cend();
	}

	container::reverse_iterator rbegin() noexcept {
		return _value.rbegin();
	}

	container::const_reverse_iterator rbegin() const noexcept {
		return _value.crbegin();
	}

	container::reverse_iterator rend() noexcept {
		return _value.rend();
	}

	container::const_reverse_iterator rend() const noexcept {
		return _value.crend();
	}

	container::reference front() {
		return _value.front();
	}

	container::const_reference front() const {
		return _value.front();
	}

	container::reference back() {
		return _value.back();
	}

	container::const_reference back() const {
		return _value.back();
	}

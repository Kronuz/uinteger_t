# uinteger_t API reference

```cpp
class uinteger_t;
```

A value type. The default constructor makes zero. Copy and move are supported,
and it deliberately specializes `std::is_arithmetic`, `std::is_integral`, and
`std::is_unsigned` so it slots into generic code that probes those traits. That
is a knowing exception to the rule against specializing standard traits (a recent
libc++ flags it); the specialization is wrapped in a guarded diagnostic pragma in
`uinteger_t.hh` so it stays clean across toolchains.

## Constructing

```cpp
uinteger_t();                                   // zero
uinteger_t(const T& value);                     // from any integral type
uinteger_t(const std::string& bytes, int base = 10);
uinteger_t(const char* bytes, std::size_t sz, int base);
uinteger_t(T (&&s)[N], int base = 10);          // string literal, e.g. "beee"
uinteger_t(const std::vector<T>& bytes, int base = 10);
```

String constructors parse digits in the given `base` (2 to 36, or 256 for raw
big-endian bytes) with no base prefix: pass `16` for hex, do not write `"0x..."`.
A character that is not a digit below the base throws `std::invalid_argument`
(`Error: Not a digit in base N: 'c' at K`, with K counting from the end of the
string). There is also a multi-limb
constructor `uinteger_t(value, args...)` and an `initializer_list` form that take
raw 64-bit limbs in most-significant-first order, for building a value directly
from its limbs.

## Arithmetic, bitwise, shift

```cpp
uinteger_t operator+(const uinteger_t&) const;   // + - * / %  and their op= forms
uinteger_t operator&(const uinteger_t&) const;   // & | ^ ~    and their op= forms
uinteger_t operator<<(const uinteger_t&) const;  // << >>      and their op= forms
uinteger_t operator++();  uinteger_t operator--();
```

Division by zero throws `std::domain_error`. Free-function overloads let a native
integer appear on the left-hand side (`1 + big`, `2 * big`, and so on). A shift
count may be a native integer (`big << 3`, `big >>= 64 * k + s`; the count is truncated
to one limb) or a `uinteger_t`, read directly when it fits 64 bits. A left shift of a
nonzero value by more than memory can hold throws (`std::length_error`, or
`std::bad_alloc` from the allocator), and so does one by a count of 2^64 bits or more; a
right shift by that much gives zero. A negative native count wraps to a very large one.

## Limb storage

```cpp
class container;                                  // the limbs: a vector of digit with a small buffer
static constexpr std::size_t container::inline_capacity;  // UINTEGER_T_INLINE_LIMBS
digit* data();  const digit* data() const;        // limb 0 is the least significant
std::size_t size() const;                         // limb count; zero is the empty value
container::iterator begin();  end();  rbegin();  rend();  // and the const forms, front(), back()
void reserve(std::size_t);  void resize(std::size_t);  void clear();
void append(const digit&);  void prepend(const digit&);   // at the most / least significant end
```

The limbs live in `uinteger_t::container`, a small vector: a value of up to
`container::inline_capacity` limbs is stored inside the object, with no allocation, and a
longer one in one heap block. The capacity is `UINTEGER_T_INLINE_LIMBS` limbs, by default 64
bytes' worth (8 limbs at 64-bit limbs, 16 at 32-bit, 32 at 16-bit); define the macro to
change it (at least 1), to the same value in every translation unit of a program: it changes
the layout of the class, so two units that disagree are an ODR violation. `sizeof(uinteger_t)`
is three words (a data pointer, a size and a capacity) plus the buffer: 88 bytes by default
on LP64.

`container` is not a `std::vector<digit>`: its iterators are plain pointers
(`container::iterator` is `digit*`, `const_iterator` is `const digit*`, the reverse ones wrap
them) and it offers only the members above and the ones the class uses. Read
`inline_capacity` by value (`constexpr std::size_t n = uinteger_t::container::inline_capacity;`);
binding a reference to it does not link before C++17.

A `uinteger_t` holds a pointer to its own buffer while its limbs are inline, so it is not
trivially relocatable: copying the bytes of one (`memcpy`, `realloc` of an array of them)
leaves the pointer on the old storage. Move or copy through the constructors, as `std::vector`
and the standard algorithms do.

Pointers and iterators into the limbs stay valid until the value grows past its capacity or
is moved. A move of a value that is inline copies its limbs into the new value; a move of a
longer one hands over the heap block, so pointers into it follow the new owner. Either way the
source is left as zero and can be used again. Shrinking (a right shift, a subtraction that
trims) keeps the block, and a copy of a value that fits is stored inline whatever the
original uses. Copy assignment reuses the target's storage when the value fits; when it must
allocate it does so before releasing the old storage, so a failed assignment leaves the target
unchanged.

## Comparison and conversion

```cpp
bool operator==/!=/</<=/>/>=(const uinteger_t&) const;
static int compare(const uinteger_t& lhs, const uinteger_t& rhs);  // -1 / 0 / 1

explicit operator bool() const;                  // false only for zero
explicit operator unsigned long long() const;    // and the other native widths
```

The native-integer conversions are explicit and return the low limb (truncating
larger values), so cast deliberately. `compare` returns the three-way result the
relational operators are built on.

## Rendering and inspection

```cpp
template <typename Result = std::string>
Result str(int alphabet_base = 10) const;        // render in base 2..36, or 256 (raw bytes)

std::size_t bits() const;                         // significant bit count
std::pair<uinteger_t, uinteger_t> divmod(const uinteger_t& rhs) const;
std::ostream& operator<<(std::ostream&, const uinteger_t&);  // honors oct/dec/hex
```

`str` produces lowercase digits for bases above 10. `divmod` returns quotient and
remainder together, which is cheaper than computing `/` and `%` separately.
Streaming with `operator<<` respects the stream's `std::oct` / `std::dec` /
`std::hex` flag.

## Digit-level conversion

```cpp
template <typename Result>
void digits(Result& out, int base) const;                         // base 2..256

template <typename T>                                             // T is a one-byte integral type
static uinteger_t from_digits(const T* src, std::size_t n, int base);
```

`digits` appends the value's digits in `base` to `out` as byte values `0..base-1`,
most significant first, with no leading zeros; zero gives a single `0` digit.
`out` is a `std::basic_string` or `std::vector` of a byte-sized type, and what it
already holds stays in place. Bases outside `[2, 256]` throw
`std::invalid_argument`.

`from_digits` is the inverse: `n` digits, most significant first, one byte each.
Leading zero digits are fine and `n == 0` gives zero. A digit that is not below
`base` throws `std::invalid_argument` (`Error: Not a digit in base N: D at K`),
as does a base outside `[2, 256]`.

Power-of-two bases (2, 4, 8, 16, 32, 64, 128, 256) slice and pack bits directly,
so both directions are linear in the number of digits. Other bases work a limb of
digits at a time and are quadratic in the length up to 8192 bits; above that
(at 32- and 64-bit limbs) they go through a tree of divisions and products, which
takes O(M(n) log n) with M the multiplication, about O(n^1.465 log n). A bit count or digit count that does not fit in
`std::size_t` throws `std::length_error` before anything is allocated (a 32-bit
`size_t` reaches it at 512 MiB of base-256 digits). If appending fails part way
(allocation failure), `out` may hold a partial digit sequence; `str` and base-x use
a fresh `Result`, so they never expose it. `str` and the string
constructors are these two functions with the ASCII alphabet `0-9a-z` mapped on
top; [base-x](https://github.com/Kronuz/base-x) maps its own alphabets the same way.

## Notes & caveats

- Unsigned only. `small - large` wraps modulo `2^(8*number_of_bytes)` for the
  current width of the operands rather than producing a negative value.
- Native-integer conversions are explicit and truncate to the low 64 bits. Use
  `str()` when you need the full value as text.
- String constructors take no base prefix. Pass the base as the second argument.
- `str` and the string constructors accept bases 2 to 36 and 256; `digits` and
  `from_digits` accept 2 to 256.
- Division and modulo by zero throw `std::domain_error`.
- Values of up to `UINTEGER_T_INLINE_LIMBS` limbs (8 by default, 64 bytes at 64-bit limbs) are
  stored in the object, so `sizeof(uinteger_t)` is 88 bytes by default rather than the
  24 of a vector; see Limb storage above for what that changes about iterators and moves.
- The `namespace std` trait specializations are a deliberate convenience so the
  type reports as arithmetic/integral/unsigned; it is technically undefined
  behavior to add to `namespace std`, and the header notes as much.

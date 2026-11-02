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

## Reusing outputs

```cpp
static uinteger_t& mul(uinteger_t& result, const uinteger_t& lhs, const uinteger_t& rhs);
static std::pair<std::reference_wrapper<uinteger_t>, std::reference_wrapper<uinteger_t>>
divmod(uinteger_t& quotient, uinteger_t& remainder, const uinteger_t& lhs, const uinteger_t& rhs);
```

`mul` stores `lhs * rhs` in `result` and returns it. `divmod` stores the quotient and the
remainder of `lhs / rhs` in `quotient` and `remainder` and returns references to them.
Each output may be an operand (`mul(x, x, y)`, `divmod(q, r, q, d)`); the one exception is
that `quotient` and `remainder` must be different objects. A zero divisor throws
`std::domain_error` before either output is written. `divmod` writes `quotient` before it
allocates for `remainder`, so a failed allocation can leave `quotient` written and `remainder`
as it was.

An output that is not an operand keeps its limb storage, so a loop that passes the same
objects each time allocates only until they have grown to the largest values, and not at
all for results that fit the inline buffer. The working space is on the stack for products
of operands up to about 85 limbs and for divisions by more than one limb of a dividend up to
73 limbs (the same counts at every limb width), and on the heap above that. An operand used as
the `mul` result is rebuilt through a temporary, which allocates for a product longer than the
inline buffer. `a * b`, `a / b`, `a % b` and `a.divmod(b)` build a new result each time.

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

`data()`, `size()` and the iterators read the limbs. The members that write them, `append`,
`prepend`, `resize` and the non-const `data()`, are internal: they let a caller leave a leading
zero limb, and every operation assumes none (the comparisons, `bits()`, `bool` and
`divmod` read the size as the value's length). A caller that uses them must keep the value
trimmed, with a nonzero top limb or no limbs for zero. `clear()` and `reserve()` always leave
a valid value.

`container`'s iterators are plain pointers
(`container::iterator` is `digit*`, `const_iterator` is `const digit*`, the reverse ones wrap
them), and it offers only the members above and the ones the class uses. Read
`inline_capacity` by value (`constexpr std::size_t n = uinteger_t::container::inline_capacity;`);
binding a reference to it does not link before C++17.

A `uinteger_t` holds a pointer to its own buffer while its limbs are inline, so it is not
trivially relocatable: copying the bytes of one (`memcpy`, `realloc` of an array of them)
leaves the pointer on the old storage. Move or copy through the constructors, as `std::vector`
and the standard algorithms do.

Pointers and iterators into the limbs stay valid until the value grows past its capacity or
is moved. A move of a value that is inline copies its limbs into the new value; a move of a
longer one hands over the heap block, so pointers into it follow the new owner. Either way the
source is left as zero and can be used again; a self-move (`x = std::move(x)`) keeps the value. Shrinking (a right shift, a subtraction that
trims) keeps the block, and a copy of a value that fits is stored inline whatever the
original uses. Copy assignment reuses the target's storage when the value fits; when it must
allocate it does so before releasing the old storage, so a failed assignment leaves the target
unchanged.

## Comparison and conversion

```cpp
bool operator==/!=/</<=/>/>=(const uinteger_t&) const;
static int compare(const uinteger_t& lhs, const uinteger_t& rhs);  // -1 / 0 / 1, public and static

explicit operator bool() const;                  // false only for zero
explicit operator unsigned long long() const;    // and the other native widths
```

The native-integer conversions are explicit and return the low limb (truncating
larger values), so cast deliberately. `compare` returns -1, 0 or 1 as `lhs` is less than,
equal to or greater than `rhs`; the relational operators are built on it. It compares sizes
first, so both values must be trimmed.

## Rendering and inspection

```cpp
template <typename Result = std::string>
Result str(int alphabet_base = 10) const;        // render in base 2..36, or 256 (raw bytes)

std::size_t bits() const;                         // significant bit count
std::pair<uinteger_t, uinteger_t> divmod(const uinteger_t& rhs) const;
std::ostream& operator<<(std::ostream&, const uinteger_t&);  // honors oct/dec/hex
```

`str` produces lowercase digits for bases above 10. A base outside 2 to 36 and 256 throws
`std::invalid_argument` ("Base must be in the range [2, 36] or 256"). `divmod` returns quotient and
remainder together, which is cheaper than computing `/` and `%` separately.
Streaming with `operator<<` respects the stream's `std::oct` / `std::dec` /
`std::hex` flag and no other: `showbase`, `uppercase` and `showpos` are ignored, and a stream
with no base flag set prints nothing.

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
digits at a time and are quadratic in the length up to 8192 bits (printing to
14336 bits on Apple's arm64 cores, parsing to 12288 at 64-bit limbs on x86-64); printing's cutoff is
`UINTEGER_T_RADIX_MAX_BITS`, the encoding kernel's limit, unless `UINTEGER_T_DIGITS_TREE_THRESHOLD`
sets it; above that
(at 32- and 64-bit limbs) they go through a tree of divisions and products, which
takes O(M(n) log n) with M the multiplication, about O(n^1.404 log n) below the transform's cutoff. A bit count or digit count that does not fit in
`std::size_t` throws `std::length_error` before anything is allocated (a 32-bit
`size_t` reaches it at 512 MiB of base-256 digits). If appending fails part way
(allocation failure), `out` may hold a partial digit sequence; `str` and base-x use
a fresh `Result`, so they never expose it. `str` and the string
constructors are these two functions with the ASCII alphabet `0-9a-z` mapped on
top; [base-x](https://github.com/Kronuz/base-x) maps its own alphabets the same way.

## Helpers for other libraries

```cpp
template <typename T> struct is_result;                  // is_result<T>::value
static constexpr unsigned base_bits(int base);           // base in [1, 256]
static constexpr unsigned base_size(int base);           // base in [1, 256]
```

`is_result<T>::value` is true for the types `str`, `digits` and `raw` can fill: a
`std::basic_string` or a `std::vector`. [base-x](https://github.com/Kronuz/base-x) constrains its
own result types with it. `base_bits(base)` is the number of bits in a digit when `base` is a
power of two from 2 to 256 (1 to 8) and 0 for every other base; base-x sizes the blocks of its padded
alphabets with it. `base_size(base)` is the number of digits that write the largest 64-bit value in
`base` (64 for base 2, 20 for base 10, 8 for base 256); `digits` sizes its output with it. Both are
constant expressions.

## Internal members

These are public and not part of the supported API; a caller that uses one owns the invariants.

- `append`, `prepend`, `resize`, the non-const `data()` and `grow`: write the limbs or the
  capacity directly (see Limb storage). The value must be left trimmed.
- `strtouint(bytes, size, base)`: the parser behind the string constructors; use the constructors.
- `uint_0()` and `uint_1()`: shared constants behind `++`, `--` and unary `-`.

`value(i)` returns limb `i` (zero past the top), `operator[](n)` tests bit `n`, and `bin()`, `oct()`,
`hex()` and `raw()` are `str` in base 2, 8, 16 and 256.

## Notes & caveats

- Unsigned only. `small - large` wraps modulo `2^(8*number_of_bytes)` for the
  current width of the operands rather than producing a negative value.
- Native-integer conversions are explicit and truncate to the low 64 bits. Use
  `str()` when you need the full value as text. The constructor from a native integer keeps
  `static_cast<digit>(value)`, one limb: a type wider than a limb loses its high bits, and a
  negative value becomes `2^digit_bits` plus the value.
- String constructors take no base prefix. Pass the base as the second argument.
- `str` and the string constructors accept bases 2 to 36 and 256; `digits` and
  `from_digits` accept 2 to 256.
- Division and modulo by zero throw `std::domain_error`.
- Values of up to `UINTEGER_T_INLINE_LIMBS` limbs (8 by default, 64 bytes at 64-bit limbs) are
  stored in the object, so `sizeof(uinteger_t)` is 88 bytes by default, where a `std::vector`
  takes 24; see Limb storage above for what that changes about iterators and moves.
- Operations use scratch memory beyond their result, up to 8 times a product above 1400 limbs;
  see ARCHITECTURE.md, Memory and compile cost.
- The tuning macros (the algorithm cutoffs, `UINTEGER_T_RADIX_UNROLL`, `UINTEGER_T_RADIX_MAX_BITS`, the
  conversion tree's cutoffs) set `static constexpr` members of the class: like `UINTEGER_T_INLINE_LIMBS`, each must have the
  same value in every translation unit of a program, or it is an ODR violation.
- The `namespace std` trait specializations are a deliberate convenience so the
  type reports as arithmetic/integral/unsigned; it is technically undefined
  behavior to add to `namespace std`, and the header notes as much.

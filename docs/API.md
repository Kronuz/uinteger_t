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
integer appear on the left-hand side (`1 + big`, `2 * big`, and so on).

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
digits at a time and are quadratic in the length. `str` and the string
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
- The `namespace std` trait specializations are a deliberate convenience so the
  type reports as arithmetic/integral/unsigned; it is technically undefined
  behavior to add to `namespace std`, and the header notes as much.

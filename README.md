# uinteger_t

![constexpr](https://img.shields.io/badge/constexpr-compile--time-blue)

An arbitrary-precision unsigned integer type for C++17.

## What it is

`uinteger_t` is a single-header arbitrary-precision unsigned integer. It behaves
like a built-in unsigned integer, except the values can be as large as memory
allows. You construct it from native integers or from strings in any base from 2
to 36, do the usual arithmetic, bitwise, shift, and comparison operations, and
render the result back to a string in the base of your choice.

Digits are stored in a `std::vector<uint64_t>` in little-endian order, so
`_value[0]` is the least significant limb. The arithmetic is tuned rather than
naive: addition and subtraction use 64-bit limbs with carry/borrow,
multiplication switches from long multiplication to Karatsuba for large numbers,
and division uses Knuth's Algorithm D.

## When to use it / when not

Use it when you need exact integer math that overflows 64 bits (big hashes,
cryptographic-sized values, base conversions, counters that must never wrap) and
you want a small, header-only dependency that reads like ordinary integer code.

Skip it if a fixed-width type (`uint64_t`, `__int128`, or a SIMD bignum library)
already covers your range, or if you need signed values, rationals, or modular
arithmetic primitives. This type is unsigned only: subtracting a larger value
from a smaller one wraps the way unsigned arithmetic does, it does not go
negative. There is also no built-in overflow trap, since there is no fixed width
to overflow.

## Install

Header-only. Copy `uinteger_t.hh` somewhere on your include path and include it:

```cpp
#include "uinteger_t.hh"
```

With CMake there's an `INTERFACE` target (`uinteger_t`) you can link against. To
pull it in via `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(
  uinteger_t
  GIT_REPOSITORY https://github.com/Kronuz/uinteger_t.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(uinteger_t)

target_link_libraries(your_target PRIVATE uinteger_t)
```

The `uinteger_t` target adds the include directory and requests `cxx_std_17`.

## Usage

```cpp
// example.cc
// c++ -std=c++17 -o example example.cc

#include <iostream>
#include "uinteger_t.hh"

int main() {
    uinteger_t a = 3735879680;
    uinteger_t b("beee", 16);  // string input, no base prefix ("0b" / "0x")
    uinteger_t num = a + b + 1;

    std::cout << num         << std::endl;  // print directly
    // => 3735928559

    std::cout << num.str(2)  << std::endl;  // render in a specific base
    // => 11011110101011011011111011101111

    std::cout << std::hex << num << std::endl;  // std::oct, std::dec, std::hex work
    // => deadbeef

    return 0;
}
```

Numbers beyond 64 bits work the same way:

```cpp
uinteger_t two_64("18446744073709551616", 10);  // 2^64
uinteger_t two_128 = two_64 * two_64;            // 2^128
// two_128.str() == "340282366920938463463374607431768211456"
// (uinteger_t(1) << uinteger_t(128)) == two_128
```

## API reference

```cpp
class uinteger_t;
```

A value type. The default constructor makes zero. Copy and move are supported,
and it deliberately specializes `std::is_arithmetic`, `std::is_integral`, and
`std::is_unsigned` so it slots into generic code that probes those traits. That
is a knowing exception to the rule against specializing standard traits (a recent
libc++ flags it); the specialization is wrapped in a guarded diagnostic pragma in
`uinteger_t.hh` so it stays clean across toolchains.

### Constructing

```cpp
uinteger_t();                                   // zero
uinteger_t(const T& value);                     // from any integral type
uinteger_t(const std::string& bytes, int base = 10);
uinteger_t(const char* bytes, std::size_t sz, int base);
uinteger_t(T (&&s)[N], int base = 10);          // string literal, e.g. "beee"
uinteger_t(const std::vector<T>& bytes, int base = 10);
```

String constructors parse digits in the given `base` (2 to 36) with no base
prefix: pass `16` for hex, do not write `"0x..."`. There is also a multi-limb
constructor `uinteger_t(value, args...)` and an `initializer_list` form that take
raw 64-bit limbs in most-significant-first order, for building a value directly
from its limbs.

### Arithmetic, bitwise, shift

```cpp
uinteger_t operator+(const uinteger_t&) const;   // + - * / %  and their op= forms
uinteger_t operator&(const uinteger_t&) const;   // & | ^ ~    and their op= forms
uinteger_t operator<<(const uinteger_t&) const;  // << >>      and their op= forms
uinteger_t operator++();  uinteger_t operator--();
```

Division by zero throws `std::domain_error`. Free-function overloads let a native
integer appear on the left-hand side (`1 + big`, `2 * big`, and so on).

### Comparison and conversion

```cpp
bool operator==/!=/</<=/>/>=(const uinteger_t&) const;
static int compare(const uinteger_t& lhs, const uinteger_t& rhs);  // -1 / 0 / 1

explicit operator bool() const;                  // false only for zero
explicit operator unsigned long long() const;    // and the other native widths
```

The native-integer conversions are explicit and return the low limb (truncating
larger values), so cast deliberately. `compare` returns the three-way result the
relational operators are built on.

### Rendering and inspection

```cpp
template <typename Result = std::string>
Result str(int alphabet_base = 10) const;        // render in base 2..36

std::size_t bits() const;                         // significant bit count
std::pair<uinteger_t, uinteger_t> divmod(const uinteger_t& rhs) const;
std::ostream& operator<<(std::ostream&, const uinteger_t&);  // honors oct/dec/hex
```

`str` produces lowercase digits for bases above 10. `divmod` returns quotient and
remainder together, which is cheaper than computing `/` and `%` separately.
Streaming with `operator<<` respects the stream's `std::oct` / `std::dec` /
`std::hex` flag.

## Build & test

Header-only, so there's nothing to compile for use. To run the smoke test:

```sh
c++ -std=c++17 -I. test/test.cc -o test/test && ./test/test
# or with CMake:
cmake -B build && cmake --build build && ctest --test-dir build
```

Requires a C++17 compiler; g++ and clang++ are supported. The header also
compiles under C++14, but the build and tooling here target C++17 to match the
sibling libraries.

The repo also ships a fuller GoogleTest suite under `tests/` (the original
upstream tests). It needs GoogleTest on the include path; see `tests/Makefile`.
The `test/test.cc` smoke test has no external dependencies and is the one wired
into CMake/CTest.

## Notes & caveats

- Unsigned only. `small - large` wraps modulo `2^(8*number_of_bytes)` for the
  current width of the operands rather than producing a negative value.
- Native-integer conversions are explicit and truncate to the low 64 bits. Use
  `str()` when you need the full value as text.
- String constructors take no base prefix. Pass the base as the second argument.
- Division and modulo by zero throw `std::domain_error`.
- The `namespace std` trait specializations are a deliberate convenience so the
  type reports as arithmetic/integral/unsigned; it is technically undefined
  behavior to add to `namespace std`, and the header notes as much.

## Examples

[`examples/demo.cc`](examples/demo.cc) is a runnable tour. A top-level CMake build
produces it next to the test:

```sh
cmake -B build && cmake --build build && ./build/uinteger_t_demo
```

It works through values that overflow a 64-bit register (`uint64` max plus one,
`2^64 * 2^64 == 2^128`, and `1 << 128` arriving at the same value), computes `40!`
with a plain `*=` loop to show the value just grows as needed, renders one number
across bases 2/10/16/36 and through `std::hex` / `std::oct`, round-trips strings in
non-trivial bases (base-36 `"zz"`, a 24-digit hex value), compares values of wildly
different magnitudes, and uses `divmod()` to get quotient and remainder of `40!`
by `10^12` in a single call (then checks `q * d + r == 40!`).

## Provenance

Based on Jason Lee's `uint128_t`, rewritten by Germán Méndez Bravo (Kronuz) to be
header-only and to support an arbitrary number of bits. The same header is
vendored inside [Xapiand](https://github.com/Kronuz/Xapiand) at
`src/uinteger_t.hh`; this repository is the standalone home for it, and the two
copies are kept in sync.

## License

MIT. Copyright (c) 2017, 2019 Germán Méndez Bravo (Kronuz) and (c) 2013-2017
Jason Lee. See [LICENSE](LICENSE).

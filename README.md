# uinteger_t

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/uinteger_t/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/uinteger_t/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

Header-only arbitrary-precision unsigned integer for C++17: built-in operators, parsing and
printing in bases 2 to 36. Construct one from numbers, strings or raw bytes, do the usual
arithmetic, and print it in any base; multiplication and division switch algorithms as values
grow. Reach for it for exact unsigned math past 64 bits.

![uinteger_t: little-endian 64-bit limbs, with schoolbook, Karatsuba, Toom-3, Toom-4 or NTT multiplication and Algorithm D, recursive or reciprocal division](docs/uinteger_t.png)

## ✨ Highlights

- **Feels native.** `+ - * / %`, bitwise and shift operators, comparisons, and
  `std::cout << std::hex`.
- **Grows as needed.** Values are as wide as memory allows.
- **Tuned arithmetic.** 64-bit limbs with hardware carry where available,
  Karatsuba, Toom-3, Toom-4 and number-theoretic transform multiplication with dedicated
  squaring, and division by Knuth's Algorithm D with reciprocal-based quotient
  estimates, recursive (Burnikel-Ziegler) from 2,048-bit divisors, and by an
  approximate reciprocal (Newton iteration) for divisors of 1,400 limbs and quotients of 5,000.
- **Fast.** Decimal printing beats GMP up to 4,096 bits, and most other operations take 1.2 to
  2.3 times GMP's time ([Performance](#-performance)).
- **Any base.** Parse and render bases 2 to 36 and 256 (raw bytes); `digits()` and
  `from_digits()` convert to and from digit arrays in bases 2 to 256, which is how
  [base-x](https://github.com/Kronuz/base-x) maps its alphabets; `divmod` returns
  quotient and remainder in one call.
- **One header, no dependencies.** [base-x](https://github.com/Kronuz/base-x)
  builds its encoders on it.

## 🚀 Quick start

```cpp
#include "uinteger_t.hh"

#include <iostream>

int main() {
	uinteger_t a = 3735879680;
	uinteger_t b("beee", 16);                         // base as the second argument
	uinteger_t n = a + b + 1;
	std::cout << n << '\n';                           // 3735928559
	std::cout << std::hex << n << '\n';               // deadbeef

	uinteger_t two_64("18446744073709551616", 10);
	std::cout << std::dec << two_64 * two_64 << '\n'; // 340282366920938463463374607431768211456
}
```

[`examples/demo.cc`](examples/demo.cc) computes `40!`, round-trips bases, and uses
`divmod`.

## 📦 Installation

Requires a C++17 compiler and CMake 3.14 or newer, and nothing else. CI builds it on Linux
(x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan, UBSan, and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(uinteger_t
  GIT_REPOSITORY https://github.com/Kronuz/uinteger_t.git
  GIT_TAG        a383e370c3d5725b83a0e824ffa9ce3a33a009aa)  # master tracks the tip
FetchContent_MakeAvailable(uinteger_t)

target_link_libraries(my_app PRIVATE uinteger_t::uinteger_t)
```

Then `#include "uinteger_t.hh"`, or copy that one header onto your include path.

## 🎯 When to use it

Use it for exact unsigned integers wider than 64 bits: big hashes, IDs, base conversion,
factorials.

Not for:

- Signed values, rationals or modular-arithmetic primitives: it is unsigned only, and
  `small - large` wraps.
- A fixed width you already have: `uint64_t` or `unsigned __int128` is faster.
- Secrets such as private keys or exponents: an operation's time depends on its values, so it
  leaks them through timing (see [docs/OPEN-WORK.md](docs/OPEN-WORK.md)).
- Builds where compile time matters most: a translation unit that prints or parses numbers
  costs the compiler about twice the work at `-O2` with the default conversion kernels as with
  `UINTEGER_T_RADIX_UNROLL=0` ([ARCHITECTURE.md](ARCHITECTURE.md), Memory and compile cost).
- Large arrays of small values: up to 8 limbs (64 bytes) live inside the object, so
  `sizeof(uinteger_t)` is 88 bytes at the defaults ([docs/API.md](docs/API.md), Limb storage).

## ⚡ Performance

Microseconds per call on an Apple M1 Pro (Apple clang 21, `-O2`) for random operands, against
GMP 6.3.0 and Boost.Multiprecision 1.92's `cpp_int`; the best of two runs of
[`bench/bench.cc`](bench/bench.cc). GMP writes each result into an existing `mpz_t`, while
uinteger_t's operators and Boost's return a new value (uinteger_t allocates one above 512 bits);
`uinteger_t::mul` and `uinteger_t::divmod` write into existing values instead.

| operation | bits | uinteger_t | GMP | Boost `cpp_int` |
|---|---:|---:|---:|---:|
| `a + b` | 256 | 0.007 | 0.004 | 0.026 |
|  | 4,096 | 0.058 | 0.017 | 0.088 |
|  | 65,536 | 0.479 | 0.317 | 1.06 |
|  | 1,048,576 | 7.25 | 5.12 | 21.5 |
| `a * b` | 256 | 0.021 | 0.015 | 0.046 |
|  | 4,096 | 1.61 | 1.19 | 3.00 |
|  | 65,536 | 123 | 80.0 | 283 |
|  | 1,048,576 | 2,818 | 2,120 | 23,571 |
| `a / b`, b half of a | 256 | 0.032 | 0.026 | 0.093 |
|  | 4,096 | 1.02 | 0.499 | 3.39 |
|  | 65,536 | 92.9 | 51.9 | 830 |
|  | 1,048,576 | 4,653 | 2,062 | 210,137 |
| `a.str(10)` | 256 | 0.102 | 0.135 | 0.310 |
|  | 4,096 | 2.83 | 4.05 | 33.5 |
|  | 65,536 | 291 | 207 | 9,544 |
|  | 1,048,576 | 20,765 | 11,117 | 2,455,933 |
| `uinteger_t(s, 10)` | 256 | 0.138 | 0.106 | 0.109 |
|  | 4,096 | 3.08 | 1.97 | 2.78 |
|  | 65,536 | 177 | 102 | 372 |
|  | 1,048,576 | 8,994 | 4,920 | 90,578 |

Decimal printing is faster than GMP's up to 4,096 bits. Elsewhere uinteger_t takes 1.2 to 2.3
times GMP's time, and a new 4,096-bit sum 3.4 times, where allocating the result costs more
than the addition. Boost takes 2 to 4 times uinteger_t's time at 256 bits and 3 (sums) to 118
(decimal printing) times at a million bits, but parses decimal faster up to 4,096 bits. An Intel
Core i9-9980HK (Apple clang 17) shows the same pattern. The table comes from:

```sh
bench/run.sh -p /opt/homebrew -o out/readme 4 64 1024 16384 add mul div str10 parse10
python3 bench/summarize.py --readme out/readme
```

[docs/BENCHMARKING.md](docs/BENCHMARKING.md) describes the runs and how to measure a change.

## 📚 Documentation

- [docs/API.md](docs/API.md): constructors, operators, conversions, rendering and caveats.
- [ARCHITECTURE.md](ARCHITECTURE.md): the limb layout and the arithmetic algorithms;
  [docs/design/](docs/design/) holds the proofs, bounds and measurements behind each kernel.
- [docs/BENCHMARKING.md](docs/BENCHMARKING.md): reproducing the performance table and judging a
  change.
- [docs/OPEN-WORK.md](docs/OPEN-WORK.md): what is missing, where it trails GMP and Boost, and
  what the tests do not reach.
- [CHANGELOG.md](CHANGELOG.md): changes by date.
- [AGENTS.md](AGENTS.md): repository map and traps.
- [Catalog entry](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#uinteger_t):
  include line, target and key API.

## 🛠️ Building and testing

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

`build/uinteger_t_demo` runs the tour. The benchmark builds outside CMake, through
`bench/run.sh` ([docs/BENCHMARKING.md](docs/BENCHMARKING.md)).

## 🤝 Contributing

Read [AGENTS.md](AGENTS.md) and the catalog's
[rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
before opening a pull request; CI must pass on every platform.

## 📄 License

MIT, Copyright (c) 2017-2026 Germán Méndez Bravo and (c) 2013-2017 Jason Lee, whose
`uint128_t` it grew from. See [LICENSE](LICENSE).

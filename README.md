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
- **Fast.** Decimal printing beats GMP from 256 bits up to 14,336 on an Apple M4 Pro (6,144 on an
  Intel i9), and most other operations take 1.0 to 2.6 times GMP's time ([Performance](#-performance)).
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

Time in microseconds for one call on random operands of `bits` bits: `a + b`, `a * b`, `a / b`
(the quotient, with `b` half the bits of `a`), `a.str(10)` (print `a` in decimal) and
`uinteger_t(s, 10)` (parse that string back). Each cell holds the Apple M4 Pro value, then the
Intel Core i9-9980HK's. The two ratio columns divide uinteger_t's time by that of
[GMP](https://gmplib.org/) 6.3.0 and by that of `cpp_int` from
[Boost.Multiprecision](https://github.com/boostorg/multiprecision/tree/boost-1.92.0) 1.92.0, so
below 1 uinteger_t is faster. GMP and Boost return the same results as uinteger_t on every operand
of the benchmark: [`bench/bench.cc`](bench/bench.cc) compares them before it times a row and stops
on a difference.

| operation | bits | uinteger_t, µs (M4 Pro / i9) | × GMP's time (M4 Pro / i9) | × Boost's time (M4 Pro / i9) |
|---|---:|---:|---:|---:|
| `a + b` | 256 | 0.004 / 0.008 | 1.33 / 1.14 | 0.24 / 0.15 |
|  | 4,096 | 0.037 / 0.139 | 2.85 / 4.96 | 0.61 / 0.95 |
|  | 1,048,576 | 5.96 / 9.33 | 1.40 / 1.07 | 0.40 / 0.75 |
| `a * b` | 256 | 0.014 / 0.034 | 1.27 / 1.55 | 0.54 / 0.44 |
|  | 4,096 | 0.998 / 2.37 | 1.24 / 1.46 | 0.44 / 0.66 |
|  | 1,048,576 | 2,382 / 5,451 | 1.55 / 1.70 | 0.13 / 0.22 |
| `a / b`, b half of a | 256 | 0.020 / 0.060 | 1.00 / 1.33 | 0.33 / 0.29 |
|  | 4,096 | 0.822 / 0.767 | 1.55 / 1.25 | 0.32 / 0.23 |
|  | 1,048,576 | 3,510 / 8,269 | 2.32 / 2.56 | 0.021 / 0.077 |
| `a.str(10)` | 256 | 0.071 / 0.175 | 0.72 / 0.94 | 0.31 / 0.21 |
|  | 4,096 | 1.85 / 4.24 | 0.57 / 0.76 | 0.080 / 0.061 |
|  | 1,048,576 | 15,038 / 31,827 | 1.81 / 2.08 | 0.008 / 0.008 |
| `uinteger_t(s, 10)` | 256 | 0.105 / 0.241 | 1.33 / 1.58 | 1.40 / 1.28 |
|  | 4,096 | 2.25 / 4.67 | 1.35 / 1.32 | 1.27 / 1.19 |
|  | 1,048,576 | 6,681 / 15,551 | 1.82 / 2.23 | 0.088 / 0.15 |

Decimal printing is faster than GMP's from 256 bits up to 14,336 on the M4 Pro and up to 6,144 on
the i9, which ties GMP to 7,680 bits ([runs](bench/results/2026-11-01-str10-crossover/README.md)).
Elsewhere in the table uinteger_t takes 1.0 to 2.3 times GMP's time on the M4 Pro and 1.1 to 2.6 on
the i9, and a 4,096-bit sum 2.9 and 5.0 times, where allocating the result costs more than the
addition. GMP writes each result into an existing `mpz_t`, while uinteger_t's operators and Boost's return a new
value (uinteger_t allocates one above 512 bits); `uinteger_t::mul` and `uinteger_t::divmod` write
into existing values instead. Boost takes 1.05 to about 130 times uinteger_t's time, except for
decimal parsing up to 4,096 bits, where uinteger_t takes 1.2 to 1.4 times Boost's. GMP and Boost
are the two arbitrary-precision libraries C++ programs choose between, so no other is timed;
for a fixed width see [When to use it](#-when-to-use-it).

Apple M4 Pro (macOS 26, arm64) and Intel Core i9-9980HK (macOS 15, x86-64), both with Apple clang
17.0.0 at `-O2` and with GMP 6.3.0 and Boost 1.92.0 from Homebrew; each cell is the best of two runs
of the benchmark at `uinteger_t:7b600e8`. To rebuild and rerun the table, run the first command on
each machine (with `-p /usr/local -o out/i9` on the Intel Mac) and the second where both outputs
are:

```sh
bench/run.sh -p /opt/homebrew -n 2 -o out/m4 2 4 8 16 64 256 1024 4096 16384
python3 bench/summarize.py --readme 'M4 Pro=out/m4' 'i9=out/i9'
```

[docs/perf/gmp-boost.md](docs/perf/gmp-boost.md) has the method, the compilers and every size and
operation on both machines, with the rows where uinteger_t loses;
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

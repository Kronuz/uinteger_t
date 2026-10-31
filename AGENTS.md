# AGENTS

Notes for agents and contributors working in this repo.

## Repo map

- `uinteger_t.hh` — the entire library. One class, `uinteger_t`, header-only.
  Arbitrary-precision unsigned integer over a `std::vector<uint64_t>`.
- `test/test.cc` — dependency-free smoke test (construct, add/multiply/shift,
  compare, `str` in several bases). This is the one wired into CMake/CTest.
- `test/property.cc` — dependency-free kernel properties (multiply against a
  reference schoolbook, division identities, shift and base round trips, and
  `digits()`/`from_digits()` against repeated division), built
  at 64-bit and 32-bit limbs and run by CTest, so CI's sanitizer jobs cover them.
- `test/dc_kernel.cc` — recursive division kernel against schoolbook division on
  raw limbs, with guard limbs around an exactly sized scratch buffer (opens up
  `private`; run by CTest). `DIV_DC_THRESHOLD` and `DIV_DC_STEP_THRESHOLD` (limbs)
  can be overridden to run the recursion on small operands.
- `test/fuzz_gmp.cc` — differential fuzz against GMP (needs `-lgmp`; not in CTest):
  `c++ -std=c++17 -O2 -I. test/fuzz_gmp.cc -lgmp -o fuzz_gmp && ./fuzz_gmp 3000`.
- `bench/bench.cc` — add, mul, div, `str` and parse timings against GMP and
  Boost.Multiprecision when available (`-DHAVE_GMP -lgmp`, `-DHAVE_BOOST`).
- `tests/` — the original upstream GoogleTest suite (`tests/test.cc`,
  `tests/Makefile`, `tests/testcases/*`). Needs GoogleTest on the include path;
  not built by CMake.
- `CMakeLists.txt` — defines the `uinteger_t` `INTERFACE` target and a `ctest`
  test.
- `LICENSE` — MIT, Copyright (c) 2017, 2019 Germán Méndez Bravo (Kronuz) and
  (c) 2013-2017 Jason Lee.
- `README.md` — what it is, install, quick start.
- `docs/API.md` — the API reference and caveats; `docs/uinteger_t.d2` is the
  README diagram's source.
- `ARCHITECTURE.md` — internal representation and the arithmetic algorithms.

## Build & run the test

```sh
c++ -std=c++17 -I. test/test.cc -o test/test && ./test/test
# or:
cmake -B build && cmake --build build && ctest --test-dir build
```

The smoke test prints `uinteger_t OK: ...` and exits 0 on success; it uses
`assert`, so build without `NDEBUG`.

## Conventions

- C++17 (the header also compiles under C++14, but tooling targets C++17 to
  match the sibling libraries). g++ and clang++ are supported.
- Header-only. Keep everything in `uinteger_t.hh`; there is no `.cc` for the
  library itself.
- Indentation is tabs, matching the existing source.
- Use double quotes in code blocks per the docs' style.
- Limb width is configurable via the `DIGIT_T` / `HALF_DIGIT_T` macros, defaults
  64/32. A `static_assert` requires the half digit to be exactly half the digit.

## Invariants

- Limbs live in a `std::vector<uint64_t>`, little-endian: index 0 is least
  significant. Leading-zero limbs are trimmed, so the value zero is the empty
  limb sequence and `operator bool()` is `size() != 0`.
- `_begin` offsets the value's window into its vector: in-place right shifts and
  `prepend` move it, so `data()` and the iterators start at `_begin`. The window
  view constructor (`[_begin, _end)` over another value's container) and
  `_carry` are vestigial: no operation builds a view, and nothing reads `_carry`.
- Division/modulo by zero throws `std::domain_error`.
- Native-integer conversion operators are explicit and truncate to the low limb.

## How to extend

- New operators go on the class; add the matching free-function overload (in the
  block near the bottom of the header) if a native integer should be valid on the
  left-hand side, so `1 + big` works alongside `big + 1`.
- New arithmetic goes in a raw-limb kernel (`k_*`, a pointer plus a length) that
  the operator calls once after sizing the result; recursion slices operands by
  pointer and carves temporaries from one scratch buffer, as `k_mul` does.
- `divmod` is the division primitive; build new division-derived operations on it
  rather than on `/` and `%`.
- Base conversion lives in `digits()` and `from_digits()`; `str()` and the string
  constructors only map characters around them. Put conversion speedups there so
  base-x gets them too. `divmod_small`, `mul_add_small` and `base_chunk` are
  private; a test reaches them with `UINT_T_PUBLIC_IMPLEMENTATION`.
- Add a matching assertion block to `test/test.cc` for any new behavior, and a
  GoogleTest case under `tests/testcases/` if you want it in the full suite.

## Traps

- Subtraction is unsigned and wraps; `small - large` does not go negative. Don't
  treat the result as signed.
- The native-integer casts are explicit and lossy (low limb only). Use `str()`
  for the full value.
- String constructors take no base prefix: `uinteger_t("ff", 16)`, never
  `"0xff"`. The base is the second argument. A character that is a digit but not
  below the base (`"9"` in base 8) throws, like a non-digit does.
- `str()` and the string constructors take bases 2 to 36 and 256; `digits()` and
  `from_digits()` take 2 to 256.
- The Karatsuba threshold is `2048 / digit_bits` limbs, not a literal limb count;
  it scales with `DIGIT_T`. Changing the limb width changes the threshold.
- `uinteger_t(std::uint64_t)` keeps only the low limb when `DIGIT_T` is narrower
  than 64 bits. Tests build values from bytes (`uinteger_t(bytes, 256)`).
- The header adds trait specializations to `namespace std`
  (`is_arithmetic`, `is_integral`, `is_unsigned`). That is technically undefined
  behavior; it is intentional and the header comments say so. Leave it unless you
  have a reason and understand the consequence.

## Provenance and syncing

[Xapiand](https://github.com/Kronuz/Xapiand) and
[base-x](https://github.com/Kronuz/base-x) pin this repository to a commit in their
`CMakeLists.txt`, so a change here reaches them only when the pin moves. base-x
calls `digits()` and `from_digits()`; bump its pin with any change to those. Keep
the MIT header
intact when editing `uinteger_t.hh`.

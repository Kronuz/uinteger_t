# AGENTS

Notes for agents and contributors working in this repo.

## Repo map

- `uinteger_t.hh` — the entire library. One class, `uinteger_t`, header-only.
  Arbitrary-precision unsigned integer over a `std::vector<uint64_t>`.
- `test/test.cc` — dependency-free smoke test (construct, add/multiply/shift,
  compare, `str` in several bases). This is the one wired into CMake/CTest.
- `tests/` — the original upstream GoogleTest suite (`tests/test.cc`,
  `tests/Makefile`, `tests/testcases/*`). Needs GoogleTest on the include path;
  not built by CMake.
- `CMakeLists.txt` — defines the `uinteger_t` `INTERFACE` target and a `ctest`
  test.
- `LICENSE` — MIT, Copyright (c) 2017, 2019 Germán Méndez Bravo (Kronuz) and
  (c) 2013-2017 Jason Lee.
- `README.md` — usage and API reference.
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
- A `uinteger_t` may be an owned value or a window view. Owned values have
  `_value` aliasing their own `_value_instance`; a view has `[_begin, _end)`
  narrowing a reference into another value's container. Multiplication builds
  views to avoid copying.
- `_carry` holds a deferred most-significant carry limb; some operations leave it
  set rather than materializing the extra limb immediately. Keep it consistent
  with the limb contents.
- Division/modulo by zero throws `std::domain_error`.
- Native-integer conversion operators are explicit and truncate to the low limb.

## How to extend

- New operators go on the class; add the matching free-function overload (in the
  block near the bottom of the header) if a native integer should be valid on the
  left-hand side, so `1 + big` works alongside `big + 1`.
- New arithmetic paths should respect the window-view representation: prefer
  slicing (`[_begin, _end)`) over copying limbs, the way Karatsuba does.
- `divmod` is the division primitive; build new division-derived operations on it
  rather than on `/` and `%`.
- Add a matching assertion block to `test/test.cc` for any new behavior, and a
  GoogleTest case under `tests/testcases/` if you want it in the full suite.

## Traps

- Subtraction is unsigned and wraps; `small - large` does not go negative. Don't
  treat the result as signed.
- The native-integer casts are explicit and lossy (low limb only). Use `str()`
  for the full value.
- String constructors take no base prefix: `uinteger_t("ff", 16)`, never
  `"0xff"`. The base is the second argument.
- The Karatsuba cutoff is `1024 / digit_bits` limbs, not a literal limb count; it
  scales with `DIGIT_T`. Changing the limb width changes the threshold.
- The header adds trait specializations to `namespace std`
  (`is_arithmetic`, `is_integral`, `is_unsigned`). That is technically undefined
  behavior; it is intentional and the header comments say so. Leave it unless you
  have a reason and understand the consequence.

## Provenance and syncing

This header is also vendored inside
[Xapiand](https://github.com/Kronuz/Xapiand) at `src/uinteger_t.hh`. The two
copies are kept byte-for-byte in sync; if you change one, port the change to the
other. Keep the MIT header intact when editing `uinteger_t.hh`.

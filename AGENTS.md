# AGENTS

Notes for agents and contributors working in this repo.

## Repo map

- `uinteger_t.hh` — the entire library. One class, `uinteger_t`, header-only.
  Arbitrary-precision unsigned integer over a small vector of `uint64_t` limbs.
- `test/test.cc` — dependency-free smoke test (construct, add/multiply/shift,
  compare, `str` in several bases). This is the one wired into CMake/CTest.
- `test/property.cc` — dependency-free kernel properties (multiply against a
  reference schoolbook, division identities, shift and base round trips, and
  `digits()`/`from_digits()` against repeated division), built
  at 64-bit and 32-bit limbs and run by CTest, so CI's sanitizer jobs cover them.
  It also builds with tiny multiplication cutoffs (`UINTEGER_T_MUL_KARATSUBA_THRESHOLD=2`
  and the other four) so Karatsuba, Toom-3, Toom-4 and the squaring kernels run at every
  size, and checks the raw kernels on dirty buffers with guard limbs (it opens up `private`).
- `bench/tune.cc` — times each multiplication and squaring algorithm over a size sweep
  to find the cutoffs (`./tune 8 400 8`, `./tune -r 300` for unbalanced shapes, `./tune -t 400 1400 2000`
  for Toom-3, Toom-4 and the transform at mid and large sizes, `./tune -l 1000` for lopsided products).
- `test/ntt_kernel.cc` — the transform multiplication against independent arithmetic: the
  primes, roots and constants, the word primitives, the transform against a naive DFT (at
  lengths `2^k` and `3 2^k`), the product against schoolbook on dirty buffers with guard
  limbs around an exactly sized scratch, the dispatch and the size plan at 16- and 32-bit
  size types (opens up `private`; run by CTest at several limb widths, leaf sizes and on the
  portable primitives). `UINTEGER_T_MUL_NTT_THRESHOLD`, `UINTEGER_T_SQR_NTT_THRESHOLD` and
  `UINTEGER_T_NTT_LEAF` run the transform on small operands and short blocks.
- `test/dc_kernel.cc` — recursive division kernel against schoolbook division on
  raw limbs, with guard limbs around an exactly sized scratch buffer (opens up
  `private`; run by CTest). `DIV_DC_THRESHOLD` and `DIV_DC_STEP_THRESHOLD` (limbs)
  can be overridden to run the recursion on small operands.
- `test/newton_kernel.cc` — the Newton (reciprocal) division kernels: the reciprocal against its
  defining inequality, the division against a dividend built as Q d + R (and against schoolbook
  division where the top limbs equal the divisor's), guard limbs around an exactly sized scratch,
  the corrections of every block against their bounds, the dispatch predicate and the size plan
  at 16- and 32-bit size types (opens up `private`; run by CTest at several limb widths, tiny
  thresholds and tiny multiplication cutoffs). `UINTEGER_T_DIV_NEWTON_THRESHOLD`,
  `UINTEGER_T_DIV_NEWTON_QUOTIENT_THRESHOLD` and `UINTEGER_T_DIV_NEWTON_INV_BASE` (limbs) run
  it on small operands.
- `bench/tune_div.cc` — times the recursive division against the Newton division forced to the
  same shape over divisor sizes (`./tune_div 1 256 1024 4096` is a balanced division; the first
  argument is quotient length over divisor length), to find the dispatch thresholds.
- `test/radix_threads.cc` — races first uses of many bases and sizes across threads
  (the lazily built conversion tables); run by CTest, so CI's TSan job covers it.
- `test/alloc.cc` — allocation counts of the small-value operations (replaces the
  global `operator new`): a result of up to the inline capacity allocates nothing and a
  longer one once (a carrying sum included), in-place operations with room allocate
  nothing, shifts by a native count build no temporaries, no block leaks, and an
  allocation that fails leaves the operand as it was. Run by CTest, except in TSan
  builds (the runtime defines `operator new` too).
- `test/inline.cc` — the inline/heap boundary of the limb container: grow, shrink, copy,
  move, assign and self-assign on both sides of it, with guard limbs after each object
  (opens up `private`). CTest builds it at the default capacity, at 32- and 16-bit limbs
  and with `UINTEGER_T_INLINE_LIMBS` 1, 2 and 3; `property.cc` also runs with the buffer
  cut to 1 and 3 limbs, and with `UINTEGER_T_POISON_LIMBS` (limbs sized without zero
  fill are filled with a pattern).
- `test/public_api.cc` — the public members the other tests reach through the hook: `compare`, the
  output-reusing `mul` and `divmod`, `is_result`, `base_bits` and `base_size`. It does not define
  `UINTEGER_T_PUBLIC_IMPLEMENTATION`, so it stops compiling if one of them turns private. The aliasing
  of those forms runs over random values in `property.cc`, and their allocation counts are in
  `alloc.cc`. Run by CTest at three limb widths and a one-limb buffer.
- `test/wide_shift.cc` — shift counts of more than one limb at 32- and 16-bit limbs
  (`7 << 2^32` builds a 512 MiB value at 32-bit limbs, so it is its own CTest target).
- `test/recip16.cc` — the reciprocal and 3-by-2 division step over all 2^31 divisors at
  16-bit limbs (opens up `private`; run by CTest).
- `test/radix_tree.cc` — the conversion tree against repeated division with leaves of
  many sizes, its powers and shape, and a dirty output buffer (opens up `private`; run
  by CTest at both limb widths, portable primitives and tiny multiplication cutoffs).
- `test/fuzz_gmp.cc` — differential fuzz against GMP (needs `-lgmp`; not in CTest), including
  conversions up to 64K limbs against `mpz_get_str`/`mpz_set_str`:
  `c++ -std=c++17 -O2 -I. test/fuzz_gmp.cc -lgmp -o fuzz_gmp && ./fuzz_gmp 3000`.
- `bench/bench.cc` — add, mul, sqr, div, `str` and parse timings against GMP and
  Boost.Multiprecision when available (`-DHAVE_GMP -lgmp`, `-DHAVE_BOOST`);
  `./bench 64 256 1024 mul sqr` limits the sizes and ops.
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

In a Debug build (CI's sanitizer job runs unoptimized) CMake runs the heavy tests in a light mode:
`property.cc` takes `100 600 1` (iterations, largest size in words, light), `radix_tree.cc`
`1 80 1` (rounds, largest size in limbs, light), `ntt_kernel.cc` `1 1` (rounds, light) and
`recip16.cc` `256 1009` (strides over the divisors for the reciprocals and the division step).
Light mode runs every distinct check on a sample of the fixed sections: a subset of the bases,
the multiples of a cutoff only up to 150 limbs, fewer division shapes and conversion edges. In
`recip16.cc` a stride above 1 visits the first 1024 values of the high divisor limb whole: they
hold 228 of the 720 divisors that reach the reciprocal's last correction. Every other
build type runs the full tests.

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

- Limbs live in a `uinteger_t::container`, little-endian: index 0 is least
  significant. Leading-zero limbs are trimmed, so the value zero is the empty
  limb sequence and `operator bool()` is `size() != 0`.
- `container` is a small vector: up to `inline_capacity` limbs (`UINTEGER_T_INLINE_LIMBS`,
  8 at 64-bit limbs) live in the object, a longer value in one heap block, and the block
  is in use exactly when `capacity() > inline_capacity`. Its `data()` is a pointer member
  to the buffer or the block; keep it that way (a choice by capacity made every operation
  read the first limb of its operands' storage, which serialized them). It is not
  trivially relocatable: the pointer points into the object. A move of a heap value takes
  the block and points the source back at its buffer; of an inline value copies the limbs.
  Pointers into a value's limbs die when it grows past its capacity or is moved.
- A `uinteger_t` is its `container` and nothing else (`sizeof` is the container's): no
  offset, no view, no deferred carry. A result may be the same object as an operand, so an
  operation reads the operand sizes, resizes the result, then takes the data pointers (the
  resize may move the limbs). A moved-from value is zero. `resize_uninit` sizes a result
  without zero-filling for a kernel that writes every limb it adds; nothing may read such a
  limb first.
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
  private; a test reaches them, and every other private member, by defining
  `UINTEGER_T_PUBLIC_IMPLEMENTATION` before the header (`UINT_T_PUBLIC_IMPLEMENTATION` is the
  older spelling); never `#define private public`.
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
- The conversion kernels for bases that are not powers of two are unrolled up to
  `UINTEGER_T_RADIX_UNROLL` limbs (default 6, 0 to 8). Each unrolled size adds code (text
  over the general kernel alone: about 9 KB at 2, 27 KB at 4, 60 KB at 6, 109 KB at 8);
  6 covers keys up to 48 bytes, 8 is 20% faster at 64 bytes, 0 is the smallest. The per-base tables are
  built on first use, published with atomics and never freed.
- Above 8192 bits `digits()` and `from_digits()` run a tree of divisions and products
  (see ARCHITECTURE.md). Its cutoffs and leaf sizes are `UINTEGER_T_DIGITS_TREE_THRESHOLD`,
  `UINTEGER_T_DIGITS_TREE_LEAF`, `UINTEGER_T_FROM_DIGITS_TREE_THRESHOLD` and
  `UINTEGER_T_FROM_DIGITS_TREE_LEAF`, in limbs; the tests set them to 2 to run the tree at
  every size. `test/radix_tree.cc` opens up `private` and drives the tree with any leaf size.
  `UINTEGER_T_RADIX_MAX_BITS` (default 8192) is the largest value a kernel takes (its tables
  are built per limb count); the tree takes everything above it.
  The tree's powers are built per call and nothing is cached or shared between threads.
- `container::inline_capacity` is a `static constexpr` member with no out-of-class
  definition: read it by value (`constexpr std::size_t n = container::inline_capacity;`),
  never bind a reference to it (`std::min(x, inline_capacity)`), or the C++14 build fails
  to link.
- Code that must not allocate for a small result sizes it with `resize`/`resize_uninit`
  and does not `reserve` past the buffer first; `reserve(n + 1)` on a value of `n` limbs
  where `n` is the capacity sends it to the heap. A step that may need one more limb
  (the carry of a sum) gets its room before the first store, only when it can happen: a
  failed allocation after a store leaves the operand holding a half-written value, and
  `alloc.cc` fails each allocation of every growing operation to catch that.
- `UINTEGER_T_INLINE_LIMBS` sets the layout of the class: every translation unit of a
  program must use the same value.
- The multiplication cutoffs (`mul_karatsuba_threshold` 24, `mul_toom3_threshold` 140,
  `mul_toom4_threshold` 400, `sqr_karatsuba_threshold` 40, `sqr_toom3_threshold` 300)
  are literal limb counts, not bit counts: they come from `bench/tune.cc` sweeps and are
  the same at 64- and 32-bit limbs. Override them with
  `UINTEGER_T_MUL_KARATSUBA_THRESHOLD` and its four siblings
  (the tests set tiny values; a huge Toom-4 value disables it). A Karatsuba cutoff below
  2 limbs would recurse forever. Toom-4 takes operands of at most `toom4_max_limbs` limbs
  (`SIZE_MAX / (32 sizeof(digit))`), which keeps its scratch size from wrapping a 32-bit
  `std::size_t`; `property.cc` checks the cap and the scratch bound it rests on.
  The transform cutoffs (`mul_ntt_threshold` and `sqr_ntt_threshold`, 1400) are swept at
  64-bit limbs only: the transform packs `64 / digit_bits` limbs per word, so at narrower
  limbs the crossover is lower.
- The transform's scratch is `5N + 3` words for a product (`4N + 3` for a square, `N` the
  transform length, up to `5.33 N + 3` at lengths `3 2^k`) and is sized by `_ntt_plan`, which
  refuses (the caller takes Toom-4) when a size would wrap. Its scratch is read as 64-bit
  words at any limb width, aligned by `ntt_align`; change the plan and the carving together.
- Division above `div_newton_threshold` (1400 limbs) with a quotient of at least
  `div_newton_quotient_threshold` (5000) limbs and half the divisor's takes the Newton path inside
  `k_div_dc`; `k_div_dc_scratch_size` follows the same choice, and the scratch plan refuses sizes
  that could wrap a 32-bit `size_t` (the recursive division is taken then). The thresholds are
  literal limb counts from `bench/tune_div.cc` sweeps at 64-bit limbs.
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

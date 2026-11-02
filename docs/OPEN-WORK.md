# Open work

What `uinteger_t` does not do yet, where it trails, and what its tests do not reach. Each item is a
fact about the code as it is, with the measurement behind it where there is one. ARCHITECTURE.md
describes the design; the notes in `docs/design/` hold the bounds and the measured alternatives.

## Missing features

- **Modular arithmetic.** There is no `gcd`, modular inverse, `mulmod` or `powmod`. For public data
  they fit the existing kernel layer: Montgomery reduction (`k_redc`) for odd moduli, a windowed
  `k_powm` over `k_mul` and `k_sqr` with one scratch buffer sized like the others, reduction by
  division (Barrett, or `k_div_dc`) for even moduli, and free functions on `uinteger_t`. Products
  then take Karatsuba, Toom and the transform through the same dispatch, and `test/fuzz_gmp.cc` can
  check them against `mpz_powm`. These run in variable time.
- **A constant-time fixed-width type.** Every kernel branches on the data: the carry tests of
  `k_add_at`, Algorithm D's add-back, the recursive division's add-backs, `trim()`, the multiply-by-one
  shortcut in `long_mult` and the comparison shortcuts in `divide`. A type for secrets needs its own
  header: a fixed number of limbs in a `std::array`, no allocation, no trimming, sizes public and values
  secret. It can share only the limb primitives (`_addcarry`, `_subborrow`, `_mult`), moved into a small
  common header and audited per platform path, and it needs its own review with a timing-leak test
  (dudect style) and a secret-taint run (valgrind based).
- **Digits through an alphabet.** `digits()` writes digit values and `from_digits()` reads them, so
  base-x maps each digit to its character in a second pass, keeps a 256-byte buffer of digits to
  decode, and `from_digits()` checks every digit again after base-x has (about 3 ns of a 32-byte decode
  on an Apple M4 Pro). `digits(out, base, alphabet)` and `from_digits(src, n, base, ordinals)` (a
  256-entry table with invalid entries marked) would write and read characters directly, removing the
  4.5 ns map of a 32-byte encode and the 2.6 ns scan of a decode there (and the 11 ns scalar map of a
  142.5 ns encode on an Intel Core i9-9980HK), and `str()` and the string constructors would use them too.

## Performance

- **Decimal parsing of short values.** Up to 4,096 bits `uinteger_t(s, 10)` takes 1.3 to 1.5 times
  Boost `cpp_int`'s time on an Apple M4 Pro (0.105 against 0.075 us at 256 bits, 2.25 against 1.78 at
  4,096) and 1.2 to 1.5 times on an Intel Core i9-9980HK (0.241 against 0.189 us at 256 bits, 4.67 against
  3.93 at 4,096). It is faster from 16,384 bits on the M4 Pro and from 65,536 bits on the Intel (even at
  16,384). At 1 KiB GMP's quadratic `mpn_set_str` is 1.3 times as
  fast as the four-chunk Horner loop (2.47 against 3.20 us on an Apple M4 Pro); a four-row lagged Horner
  step, like the encoder's short product, is the candidate.
- **Power-of-two decoding.** `from_digits` packs one digit at a time in bases 2 to 256 that are powers of
  two (check, shift, or). Through base-x, a 1 KiB base64 decode costs 3.0 ns per byte against 1.1 to
  encode on an Apple M1 Pro. Packing eight digits per step with SWAR, as the Horner path builds its
  chunks, is the candidate.
- **Addition and subtraction.** 0.43 ns per limb against GMP's 0.31 on an Apple M1 Pro; the carry
  leaves the flag at every eight-limb boundary, and closing the rest takes assembly. Inlining a short
  loop for sums under 8 limbs is faster on the M1 Pro and slower on the Intel (see
  [multiplication.md](design/multiplication.md)).
- **A short quotient by a long divisor.** `knuth_divmod` normalizes the whole dividend and divisor into a
  buffer of `dn + nn + 1 + qn` limbs (on the heap above 256 limbs), where the truncated kernel reads only
  the dividend from limb `dn - 4` up and the top `qn + 3` divisor limbs unless it flags. A 4-limb quotient
  by a 2048-limb divisor takes 1.3 us against GMP's 0.09 on an Intel Core i9-9980HK. Normalizing that
  window, and the rest (or a reload) only on a flag, would take it near GMP; the repair reads the divisor
  and dividend limbs the truncated digits skip.
- **The Toom-4 cutoff is the Apple M4 Pro's.** On an Apple M1 Pro Toom-4 pays from about 700 limbs (a
  wash at 512, 2.2% at 768, 6% at 1024); `mul_toom4_threshold` is 400 everywhere.
- **The recursive division's cutoffs** (32 and 8 limbs) come from a sweep with Karatsuba products and the
  earlier schoolbook step, not against Toom-3, Toom-4, the transform or the current step, and no tool in
  `bench/` sweeps them (`tune_div` compares the recursion with Newton, `tune_divq` the truncated kernel
  with the exact one). A two-row Algorithm D step and a quotient-only recursive division are untried.
- **The Newton division.** At power-of-two sizes the reciprocal's two products (`n` by `h + 1` and `h + 1`
  by `l + 2` limbs) land just over a transform length and pay up to 33% to 50% more on those products;
  handling the top limb of `W_h'` (always 1) apart and choosing `h` to fit the lengths would recover it.
  A remainder formed as a product modulo `B^k - 1` needs a cyclic transform. The dispatch sweep has no
  quiet rerun and no 32-bit-limb sweep, and `div_newton_inv_base` is not swept.
- **The basecase.** Three rows per pass is 2% to 7% faster than two at 16 to 32 limbs and 17% at 12 (Apple
  M4 Pro), at the price of head and tail code and a rewrite of the squaring basecase.
- **Toom.** A leaner Toom-4 interpolation (fused passes or a shorter sequence) is worth about 1% to 3%;
  a Toom-4/Toom-3 split for moderately unbalanced operands and Toom-6.5 or Toom-8.5 are untried.
- **The transform.** Its twiddle table is built per product, a few percent of a product at the cutoff; a
  cache would help the small sizes. `ntt_lopsided` (16) sends a 16 times lopsided product with a shorter
  operand under 1400 limbs to Toom chunks: 1200 by 19200 limbs takes 1973 us against 1494 for one
  transform, and 32 fixes it at more scratch. The cost of a transform steps with its length (`2^k` or
  `3 2^k`), and a cutoff by size cannot follow the steps: in the transform's sweep against Toom-3 on an
  Apple M4 Pro, a 1600-limb product takes 190.9 us on Toom-3 and 208.1 on the transform.
- **Small divisions.** A division of a short value pays the divisor's reciprocal on every call (`k_inv_1`
  is one 128-by-64-bit division): `a / b` by one limb takes 16.6 to 33 ns at 2 to 8 limbs where `a + b`
  takes 2 to 5 ns (Apple M4 Pro).
- **Conversion tables.** Up to `radix_unrolled` limbs a table is built per bit length, but its content
  depends only on the limb count, the chunk count and the guard limb, so bit lengths that share those
  build identical tables.

## Code

- At 16- and 32-bit limbs the transform reads its `digit` scratch as 64-bit words
  (`reinterpret_cast<ntt_word*>` in `ntt_align`), a strict-aliasing violation in the letter of the
  standard; a `may_alias` word type or a scratch carved as words would remove it.
- A failed allocation of the transform's scratch throws `std::bad_alloc`; nothing retries with Toom's
  smaller scratch.
- The four-argument `divmod(q, r, a, b)` writes `q` before it allocates `r`, so a failed allocation leaves
  `q` written (and a dividend that is `q` lost). `test/alloc.cc` injects no failure into a division.
- `k_scratch` puts its heap pointer right after its 256-limb stack buffer, so a one-limb overrun at
  exactly 256 limbs corrupts the pointer; only ASan sees it.
- `knuth_divmod` keeps two `k_scratch` frames (about 4 KiB of stack at 64-bit limbs), the second for the
  fallback only.
- `_enc_general`'s stack buffer of `radix_work` limbs has no assert against its need, `n + RL + 8`: at
  8192 bits 298 limbs against at most 280 (base 139), 621 against 584 at 32-bit limbs (base 85); at
  Apple's 14336 bits 504 against 484 (4 KB), 1069 against 1016 at 32-bit limbs. It grows with
  `UINTEGER_T_RADIX_MAX_BITS`.
- In `k_div_sb_digit` the add-back after the largest digit never runs (see
  [division.md](design/division.md)) and `w[dn] = 0` is a store nothing reads.
- `k_mul` and `k_mul_scratch_size` (and the squaring pair) write the same dispatch twice; one plan
  function could choose for both.
- `DIV_DC_THRESHOLD`, `DIV_DC_STEP_THRESHOLD`, `DIGIT_T`, `HALF_DIGIT_T` and the `HAVE_*` macros have no
  `UINTEGER_T_` prefix, and the two division macros are not `#undef`d.
- `inv()` complements the value in place and returns a copy; on zero it leaves 0, where `~x` gives 1.

## Tests and tools

- No CTest target runs the quotient-only fallback (the kernel's status 3): the proof excludes it, so the
  reload from `lhs` and the three checks behind it are untested, and mutants that drop a check survive. A
  test macro that makes `k_div_sb_trunc` return 3 on every flag, with `property.cc` built under it, would
  cover it.
- Nothing pins that `a * a` takes the squaring kernel: with the check removed every test passes, and only
  `bench.cc` shows it (a 1024-limb square at 74 us against 106 as a product).
- Nothing pins the recursive division's split: `hi = n - 1` passes every test and makes the recursion
  quadratic.
- An untrimmed Toom-4 coefficient adds zeros past the result, which only the `assert` in `k_add_at` and a
  sanitizer see; a guard page after the result in `check_kernel_mul` would catch it under `NDEBUG`.
- The Newton division runs at production sizes in CTest only for divisors of 1400 and 1401 limbs with a
  5000-limb quotient; the conversion tree reaches it only from about 10K-limb values, which no CTest
  target converts. The tests see at most 3 upward corrections against bounds of 4 and 8, and the carry-in
  of the negation is invisible to outcomes (a check that `E + t W_h'` is 0 modulo `B^(n+1)` would see it).
- `_tree_leaf`'s second `std::logic_error` (more digits than a leaf holds) is reached by no test.
- The size guards are tested at narrow size types, but their call sites in the power-of-two paths are not:
  a call site that bypasses the guard passes every test.
- The big-endian branches (`_swar8`, the base-256 byte load) run on no CI host, and no build has a 32-bit
  `size_t`, so the left-shift guard (`Shift too large` past `SIZE_MAX` limbs) is reached by nothing.
- `test/test.cc` and `test/fuzz_gmp.cc` assume 64-bit limbs (`fuzz_gmp` builds values from `uint64_t`
  words, which narrower limbs truncate), and a Release build compiles out `test/test.cc`'s asserts; the
  kernel tests check with their own `CHECK` and run in every build type.
- A relaxed-ordering mutant of the table publication passes `radix_threads` on arm64; only the TSan job
  sees it.
- `bench/` has no sweep for the conversion tree's cutoffs and leaves or for `UINTEGER_T_RADIX_UNROLL`, and
  no rows for copy, move, shift or comparison of 1- to 8-limb values.
- The carry chains of `k_add_n` and `_multadd` are checked in clang's output only, not in GCC's.
- The sanitizer job builds about 30 `property.cc` variants that each change one axis (limb width,
  primitives, cutoffs, buffer size, poison, unroll); a pairwise covering set of about ten builds would
  cover every pair of values.

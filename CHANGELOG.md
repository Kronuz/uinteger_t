# Changelog

What changed in `uinteger_t`, newest first. There are no release tags: each section
is dated and links the commits on `master`.

## 2026-10-31

A performance pass over the whole library. Timings are at -O2 on Apple Silicon. GMP and
Boost.Multiprecision ran on the same machine through `bench/bench.cc`; GMP reuses its
output where `uinteger_t` allocates a result.

### Added

- `digits(out, base)` and `from_digits(src, n, base)`: digit-level conversion for bases
  2 to 256. `str()`, the string constructors and base-x are built on them ([bd7bab1]).
- Toom-3 multiplication from 140 limbs, and squaring kernels taken by `a * a` and
  `a *= a`: Karatsuba from 40 limbs, Toom-3 from 300 ([2b2199e], [5c40aee]).
- Recursive division (Burnikel-Ziegler) for divisors of 32 limbs (2048 bits) and up
  ([441a250], [17858eb]).
- Number-theoretic transform multiplication (three primes, Chinese remainder
  reconstruction) for products and squares of 1400 limbs and up, and for products up to 16
  times lopsided from about 1000 limbs; its scratch is 5 to 8 times the product ([ee0a379],
  [eb86d02], [60ab1c6]).
- Subquadratic conversion above 8192 bits for bases that are not powers of two: a tree of
  divisions and products, O(M(n) log n) ([6e74834]).
- An inline buffer: values of up to 8 limbs (64 bytes) live inside the object and allocate
  nothing. `UINTEGER_T_INLINE_LIMBS` sets the size ([8f1345e]).
- Tuning macros for the cutoffs (`UINTEGER_T_MUL_KARATSUBA_THRESHOLD` and its siblings,
  `DIV_DC_THRESHOLD`), `UINTEGER_T_RADIX_UNROLL` (0 to 8, default 6), `UINTEGER_T_NO_INT128`.
- `bench/bench.cc`, timings against GMP and Boost.Multiprecision, and `bench/tune.cc`, the
  sweeps behind the multiplication cutoffs.
- `uinteger_t::compare(lhs, rhs)` is public, as the documentation says it is; it sat in a
  private section.
- `uinteger_t::mul(result, lhs, rhs)` and `uinteger_t::divmod(quotient, remainder, lhs, rhs)`:
  public forms that store into existing values. An output may be an operand, and an output
  that is not one keeps its storage, so reused outputs with room allocate nothing.
- `test/public_api.cc` builds without the test hook, so it stops compiling if a public name
  turns private. docs/API.md and ARCHITECTURE.md document `is_result`, `base_bits` and
  `base_size` (which base-x uses), the limb mutators and `strtouint`, `grow`, `uint_0` and
  `uint_1` as internal members, and the memory and compile cost of the header.

### Changed

- Parsing throws `std::invalid_argument` on a character that is a digit but not below the
  base (`"9"` in base 8, `"z"` in base 10); it used to fold it into the value ([bd7bab1]).
- Parsing throws `Not a digit` on bytes 0x80 and up; where `char` is signed, they used to
  index the digit table with a negative value ([bd7bab1], [bcd908b]).
- A `uinteger_t` shift count of 2^64 bits or more throws `std::length_error` on a nonzero
  left shift and gives zero on a right shift, at every limb width ([6ff439f], [d50f9bd]).
- `uinteger_t::container` is the library's own small vector, no longer
  `std::vector<digit>`: `iterator` is `digit*`, `const_iterator` is `const digit*`, and it
  has only the members the class uses ([8f1345e], [10cf088]).
- `sizeof(uinteger_t)` is 88 bytes at the defaults on LP64 (56 before): a data pointer, a
  size, a capacity and the 64-byte buffer ([8f1345e], [10cf088]).
- A `uinteger_t` points into itself while its limbs are inline, so it is not trivially
  relocatable: `memcpy` or `realloc` of one leaves its pointer on the old storage.
- `UINTEGER_T_INLINE_LIMBS` changes the layout of the class, so every translation unit of a
  program must use the same value ([d4a2b0c]).
- `str()` names base 256 in its error ("Base must be in the range [2, 36] or 256").
- Tests reach the private members through one hook, `UINTEGER_T_PUBLIC_IMPLEMENTATION`;
  `UINT_T_PUBLIC_IMPLEMENTATION` is still accepted, and no test defines `private` as `public`.
- Moves are `noexcept` and leave the source zero. A move of an inline value copies its limbs,
  so pointers into it do not follow the new owner ([6ff439f], [8f1345e]).
- Copy assignment allocates before it releases, so a failed assignment leaves the target
  unchanged ([8f1345e], [aaecc12]).
- A conversion whose bit or digit count does not fit `std::size_t` throws `std::length_error`
  before it allocates (a 32-bit `size_t`, at 512 MiB of base-256 digits) ([819daa4], [b012716]).
- A quotient alone (`a / b`, `a /= b`) with a short quotient (under 64 limbs when it is as long as the
  divisor, under 128 when half as long, under 32 when twice as long; `UINTEGER_T_DIV_TRUNC_THRESHOLD`)
  runs a schoolbook division that skips the low part of every digit's product. When a digit's
  remainder is within the truncation error of a decision edge, it subtracts the skipped products and
  finishes exactly. An exact multiple (`a = q b`) always takes that path and costs about one
  schoolbook division: 0 to 15% more than the recursive division up to 130 limbs of dividend (10% at
  16 limbs, 5% at 64, 15% at 128). The cutoff is set there; the kernel alone still wins on random
  operands up to several hundred limbs ([b7a06cd], [a9af8cf]).

### Fixed

- A native integer literal on the left of a shift (`5 << big`) and `signed char` compound
  assignments (`c += big`) compile, as every other operator and native type does.
- Wrong products without `__int128` (the portable `_multadd` wrapped its middle sum) and on
  MSVC (a full carry limb passed as a one-bit carry-in, `_mult`'s low limb unset) ([a39ede0]).
- `x >>= 128; x *= 1` gave the shifted-out limbs back: a self copy-assignment after an
  in-place right shift restored them ([6ff439f]).
- A value moved from after a right shift reported `size()` as 2^64 - 1 ([6ff439f]).

### Removed

- `k_sub_at` and `k_add_wrap`, kernels nothing called (private, visible only through the
  test hook).
- `divmod_small`, `mul_add_small` and `base_chunk`, public from [aeeeb67] to [bd7bab1],
  are private again; `digits()` and `from_digits()` do what they did.

### Performance

- Addition and subtraction eight limbs per pass, so the carry chains through the flag: `a + b`
  on Apple M1 Pro 9% faster at 16 limbs, 20% at 64 and 30% from 256 (65536 limbs: 40.9 to
  28.5 us, GMP 20.4) ([bd83419]).
- Division by 2 to 256 limbs: one carry chain in the multiply-subtract, a product over all
  but the top two divisor limbs per quotient digit, and uninitialized stack scratch; div n/2
  on Intel 16% to 18% faster at 16 to 64 limbs, 5% at 256 ([1c07a32]).
- Multiplication at 1024 limbs: 383 to 127 us on raw limb kernels with a two-row
  schoolbook and Karatsuba ([5bb58c1]).
- Toom-3 and squaring at 1024 limbs: mul 125 to 106 us (GMP 57), sqr 125 to 74 us
  (GMP 45) ([2b2199e]).
- Adding the carry last in the multiply-add: mul 8-11% faster from 16 to 1024 limbs, sqr
  6-10% ([5ee8586]).
- Division: 1024 by 513 limbs 162 to 75 us (GMP 39), 2048 by 256 limbs 290 to 168 us
  (GMP 113) ([441a250]).
- Quotient-only division (`a / b`), L-limb dividend by L/2+1 limbs on an Intel i9-9980HK: 16
  limbs 147 to 135 ns (GMP `mpz_tdiv_q` 109), 32 limbs 446 to 336 (240), 64 limbs 1.39 to 0.89 us
  (0.62), 128 limbs 4.5 to 3.0 us (1.9); from 256 limbs the recursive division runs as before. A
  4-limb quotient by a 2048-limb divisor 11.7 to 1.3 us ([b7a06cd], [a9af8cf]).
- The transform, mul: 4K limbs 828 to 445 us (GMP 317), 16K 6.2 to 2.3 ms (GMP 1.5), 64K
  47.8 to 11.0 ms (GMP 9.1), 256K 372 to 52.7 ms (GMP 48.1); sqr at 256K 266 to 37.4 ms
  (GMP 33.3); division 2n by n at 64K limbs 120 to 62 ms (GMP 18) ([ee0a379]).
- Base conversion in limb-sized chunks: decimal `str()` 15-39x faster and parsing about
  76x faster on 4 to 1024 limbs ([aeeeb67]).
- Power-of-two parsing is linear: hex at 1024 limbs 5.0 ms to 32 us ([bd7bab1]).
- Division-free conversion kernels, base 58: 32 bytes 156 to 34 ns, 64 bytes 311 to 70 ns,
  1 KiB 37.4 to 6.2 us ([8486c75], [a1d5b48]).
- Digit splitting fixed at compile time to the base's chunk shape: base 58, 32 bytes encode
  163.7 to 142.5 ns on an Intel i9-9980HK, 8% faster on Apple Silicon.
- The conversion tree, decimal `str()`: 1024 limbs 2254 to 202 us (GMP 154), 65536 limbs
  9.2 s to 145 ms (GMP 48 ms) ([6e74834], [94329f6]).
- The conversion tree, decimal parsing at 1024 limbs: 234 to 132 us (GMP 76); 64 KiB in
  base 58: 153 to 5.7 ms ([6e74834]).
- Small values without temporaries: `a << 3` 74 to 17 ns, `<<=` and `>>=` 160 to 11 ns,
  `a % b` by one limb 60 to 25 ns, `a / b` by two limbs 83 to 43 ns ([6ff439f]).
- The inline buffer, 1 to 8 limbs: copy 10-13 to 2-3 ns, `a + b` 16-19 to 2-5 ns, `a * b`
  19-27 to 6-14 ns, `a << 3` 17-21 to 3-4 ns ([8f1345e], [10cf088], [ecba7ca]).
- Single-limb division on targets without `__int128` or `_udiv128` (MSVC ARM64 among them)
  runs two-digit long division on half limbs, no longer a 64-step bit loop ([876f49b]).

[5bb58c1]: https://github.com/Kronuz/uinteger_t/commit/5bb58c1
[bd83419]: https://github.com/Kronuz/uinteger_t/commit/bd83419
[b7a06cd]: https://github.com/Kronuz/uinteger_t/commit/b7a06cd
[a9af8cf]: https://github.com/Kronuz/uinteger_t/commit/a9af8cf
[aeeeb67]: https://github.com/Kronuz/uinteger_t/commit/aeeeb67
[a39ede0]: https://github.com/Kronuz/uinteger_t/commit/a39ede0
[bd7bab1]: https://github.com/Kronuz/uinteger_t/commit/bd7bab1
[819daa4]: https://github.com/Kronuz/uinteger_t/commit/819daa4
[bcd908b]: https://github.com/Kronuz/uinteger_t/commit/bcd908b
[441a250]: https://github.com/Kronuz/uinteger_t/commit/441a250
[17858eb]: https://github.com/Kronuz/uinteger_t/commit/17858eb
[2b2199e]: https://github.com/Kronuz/uinteger_t/commit/2b2199e
[5c40aee]: https://github.com/Kronuz/uinteger_t/commit/5c40aee
[8486c75]: https://github.com/Kronuz/uinteger_t/commit/8486c75
[a1d5b48]: https://github.com/Kronuz/uinteger_t/commit/a1d5b48
[5ee8586]: https://github.com/Kronuz/uinteger_t/commit/5ee8586
[6ff439f]: https://github.com/Kronuz/uinteger_t/commit/6ff439f
[d50f9bd]: https://github.com/Kronuz/uinteger_t/commit/d50f9bd
[6e74834]: https://github.com/Kronuz/uinteger_t/commit/6e74834
[94329f6]: https://github.com/Kronuz/uinteger_t/commit/94329f6
[b012716]: https://github.com/Kronuz/uinteger_t/commit/b012716
[8f1345e]: https://github.com/Kronuz/uinteger_t/commit/8f1345e
[aaecc12]: https://github.com/Kronuz/uinteger_t/commit/aaecc12
[10cf088]: https://github.com/Kronuz/uinteger_t/commit/10cf088
[ecba7ca]: https://github.com/Kronuz/uinteger_t/commit/ecba7ca
[d4a2b0c]: https://github.com/Kronuz/uinteger_t/commit/d4a2b0c
[876f49b]: https://github.com/Kronuz/uinteger_t/commit/876f49b
[ee0a379]: https://github.com/Kronuz/uinteger_t/commit/ee0a379
[eb86d02]: https://github.com/Kronuz/uinteger_t/commit/eb86d02
[60ab1c6]: https://github.com/Kronuz/uinteger_t/commit/60ab1c6
[1c07a32]: https://github.com/Kronuz/uinteger_t/commit/1c07a32

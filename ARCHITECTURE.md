# Architecture

`uinteger_t` is one header, `uinteger_t.hh`, holding a single class
`uinteger_t`. This document covers how the value is stored, the arithmetic
algorithms, their complexity, and the design choices that shape the code.

## Internal representation

A value is a sequence of 64-bit limbs ("digits") held in a
`std::vector<uint64_t>`, little-endian: the limb at index 0 is the least
significant. The limb type is `DIGIT_T` (default `std::uint64_t`) with a
`HALF_DIGIT_T` (default `std::uint32_t`) used where the code needs to split a
limb in half during multiplication and base conversion. A `static_assert`
enforces that the half digit is exactly half the width of the digit.

The object is its limb vector and nothing else: `sizeof(uinteger_t)` equals
`sizeof(container)`. There are no offsets, no views and no flags, so a copy is a
vector copy, a move leaves the source empty (zero), and every operation can hand
raw limb pointers to a kernel after sizing its result. In-place right shifts move
the limbs down and shrink the vector.

Leading-zero limbs are stripped by `trim()` so the limb
count tracks the significant magnitude and `size()` reflects it. The value zero
is the empty limb sequence, which is why `operator bool()` is just
`size() != 0`. A result may be the same object as an operand (`a += a`,
`x = x - y`): each operation reads its operand sizes first, resizes the result,
and takes the data pointers after that.

## Growth strategy

`append` grows the vector by a geometric `growth_factor` of 1.5: `grow(n)` reserves
`n * 1.5` when it needs more room, which keeps repeated appends to the most significant
end amortized O(1) without the 2x factor's tendency to leave large unreusable holes.
Operations that know their result size (`+`, `-`, `*`, shifts, division) size the
result once and allocate once; `prepend` inserts at the front of the vector.

## Addition and subtraction

Addition and subtraction walk the limbs from least to most significant doing
64-bit add/sub with a carry/borrow propagated limb to limb. Cost is linear in
the number of limbs.

The limb primitives (`_mult`, `_multadd`, `_divmod`, `_addcarry`,
`_subborrow`) pick their implementation at compile time through the `HAVE_*`
macros at the top of the header: `__int128` on GCC and clang 64-bit targets;
`__builtin_addcll` / `__builtin_subcll` on clang; `_umul128`, `_udiv128`,
`_addcarry_u64` and `_subborrow_u64` on MSVC x64; `__umulh` on MSVC ARM64.
Every other target takes the portable path, which builds the 128-bit product
from four half-limb products and synthesizes carries from wrapped-sum
comparisons. `UINTEGER_T_NO_INT128` forces the portable 64-bit path; CTest runs
the property test that way. MSVC itself is not in CI; its paths are checked
against a stand-in `<intrin.h>` with the same signatures.

Subtraction is unsigned: a smaller-minus-larger result wraps rather than going
negative, matching how a fixed-width unsigned type would behave for the operand
width involved.

## Limb kernels

The arithmetic runs on raw limb arrays (`digit*` plus an explicit length), in the
`k_*` kernels: add/sub with carry, multiply-by-limb (`k_mul_1`, `k_addmul_1`,
`k_submul_1`), shifts, and the multiplication and division routines below. The
`uinteger_t` operators size and trim the result and call a kernel once, so the
inner loops never touch a `std::vector` or allocate.

## Multiplication

`k_mul` dispatches on the operand sizes, `an >= bn`:

- Below `mul_karatsuba_threshold` limbs of the smaller operand (24) it runs
  `k_mul_basecase`, schoolbook multiplication that takes two limbs of the
  multiplier per pass over the multiplicand. Both rows' products and carries
  stay in registers, which is about 1.6x faster than one row per pass.
- When one operand has at least twice the limbs of the other, it multiplies
  the shorter one by successive equal-size chunks of the longer one and adds
  the partial products, so the algorithms below see near-balanced operands.
- From `mul_toom3_threshold` limbs (140), when the smaller operand reaches a third
  piece (`bn > 2k` with `k = ceil(an/3)`), it runs Toom-3. O(n^1.465).
- Otherwise it runs Karatsuba. O(n^1.585).

Karatsuba splits at `h = ceil(an/2)` and computes `a0*b0` and `a1*b1` straight
into the result. The middle term is `a0*b1 + a1*b0 = a0*b0 + a1*b1 -
(a0-a1)*(b0-b1)`: the two differences are taken as magnitudes of at most `h` limbs,
their signs are kept apart, and the third product is added or subtracted
according to the sign of their product. No operand grows a carry limb, which
the sum form `(a0+a1)(b0+b1)` needs; the subtractive form measured 3-7% faster.

Toom-3 splits each operand into three pieces of `k` limbs and treats them as
the coefficients of a degree-2 polynomial in `B^k`. It evaluates at 0, 1, -1, -2
and infinity (the pieces `x0` and `x2` give two of the points directly; `x(1)`,
`|x(-1)|` and `|x(-2)|` take `k + 1` limbs, with the signs of `x(-1)` and `x(-2)`
kept as flags). Of the five products, `c0 = a0*b0` and `c4 = a2*b2` land straight
in the result and the other three, of `k + 1` limbs each, go to scratch. The
interpolation is the sequence of Bodrato and Zanoni, "Integer and Polynomial
Multiplication: Towards Optimal Toom-Cook Matrices" (ISSAC 2007):

    t3 = (vm2 - v1) / 3        t1 = (v1 - vm1) / 2        t2 = vm1 - c0
    c3 = (t2 - t3 + 4 c4) / 2  c2 = t2 + t1 - c4          c1 = t1 - c3

It runs on two's complement values modulo `B^(2k+2)`, so the negative
intermediates need no sign branches: the products at -1 and -2 are negated
when their sign flag is set, and each step is an add or subtract over the same
limb count. The division by 3 is exact, so it goes limb by limb from the bottom
with the inverse of 3 mod `B` (the borrow into the next limb is the high part of
three times the quotient limb). The divisions by 2 are only done on
non-negative values (`c1 + c3`, and `2 c3` after adding `4 c4`). The three middle
coefficients are then added into the result at limbs `k`, `2k` and `3k`.

Squaring has its own kernels. `operator*` and `operator*=` take `k_sqr` when both
operands are the same value (the same object). `k_sqr_basecase` forms each
off-diagonal product `a[i]*a[j]`, `i < j`, once, two rows of the triangle per
pass, doubles the triangle and adds the squares `a[i]^2`, about 0.6 of a
multiplication. `k_sqr_karatsuba` uses `2 a0 a1 = a0^2 + a1^2 - (a0 - a1)^2`
(three half-size squares), and `k_sqr_toom3` takes five squares with no signs to
track. Squaring crosses over at `sqr_karatsuba_threshold` (40 limbs) and
`sqr_toom3_threshold` (300 limbs).

The four cutoffs are literal limb counts, not bit counts: a crossover depends on
the cost of a limb product against a limb add, and that ratio is the same at 64- and
32-bit limbs. They come from `bench/tune.cc` sweeps (each algorithm timed at the top
level, the recursion below it taking the library's cutoffs) and can be set with
`UINTEGER_T_MUL_KARATSUBA_THRESHOLD`, `UINTEGER_T_MUL_TOOM3_THRESHOLD`,
`UINTEGER_T_SQR_KARATSUBA_THRESHOLD` and `UINTEGER_T_SQR_TOOM3_THRESHOLD`; the
tests set tiny values so every path runs at small sizes.

One scratch buffer, sized up front by `k_mul_scratch_size` (or `k_sqr_scratch_size`
for a square), serves the whole recursion; each level carves its temporaries from
it and passes the rest down. The size functions follow the dispatch level by level,
so the buffer covers every level. A small one lives on the stack and a larger one on the heap,
uninitialized: the kernels write every limb before they read it.

## Division and modulo

`divmod` is the entry point. `operator/` and `operator%` run the same code but
build only the result they return (the other output is not materialized), so
`a % b` costs one allocation. Division or modulo by zero throws
`std::domain_error`. The Algorithm D wrapper keeps the normalized divisor and
dividend, the quotient and the recursion scratch in one buffer, on the stack up to
64 limbs.

- A single-limb divisor takes `divmod_small`: one pass from the top limb, each
  step a 2-by-1 division by a precomputed reciprocal (Moller and Granlund,
  "Improved division by invariant integers", 2011), so the loop multiplies
  instead of dividing.
- Larger divisors take schoolbook long division (Knuth's Algorithm D) in
  `k_div_sb`: normalize so the divisor's top bit is set, then estimate each
  quotient limb with the same paper's 3-by-2 division by the top two divisor
  limbs and their reciprocal. The estimate is exact or one too large; one
  multiply-subtract and at most one add-back fix it. O(n*m).
- From `div_dc_threshold` limbs (`2048 / digit_bits`, 32 at 64-bit limbs) the
  divisor takes `k_div_dc`, recursive division after Burnikel and Ziegler
  ("Fast Recursive Division", 1998), on the same normalized contract. A
  balanced division (2n limbs by n) cuts its quotient into a high half and a
  low half. Each half is a step: to get `c` quotient limbs from an `n`-limb
  divisor, divide the top `2c` limbs by the top `c` limbs of the divisor
  (a balanced division, recursively), then multiply that quotient by the
  divisor's remaining `n - c` low limbs with `k_mul` and subtract. The
  estimate is never below the true quotient and at most 2 above it, so up to
  two adds of the divisor fix the remainder. The divisor is never padded, so
  any size splits cleanly. A step with fewer than `div_dc_step_threshold`
  quotient limbs (a quarter of the threshold) runs `k_div_sb` instead. For
  a dividend much longer than the divisor, `k_div_dc` cuts the quotient into
  divisor-size blocks from the top (a shorter block first), each a balanced
  or short division over the remainder so far. One scratch buffer, sized up
  front by `k_div_dc_scratch_size`, serves the whole recursion. The work is a
  few multiplications of the divisor's size per level, so it is O(n^1.585)
  with Karatsuba, and the products inherit any faster multiplication.

## Base conversion (`str`, parsing, and the digit API)

The digit-level pair is the one place conversion happens. `digits(out, base)`
appends a value's digits as byte values `0..base-1`, most significant first;
`from_digits(src, n, base)` builds a value from such digits. `str()` and the string
constructors map the ASCII alphabet (`chr`/`ord`) around them, and base-x maps its
own alphabets the same way, so a speedup here reaches every caller. Bases run from
2 to 256; `str` and parsing expose 2 to 36 and 256.

Power-of-two bases (`base_bits(base)` is nonzero) never divide. `digits` slices
`base_bits` bits at a time out of the limbs, tracking the limb and bit position
incrementally; a digit that straddles a limb boundary takes its high bits from the
next limb. `from_digits` places each digit at its bit offset in a zeroed limb array,
spilling into the next limb at a boundary, then trims. Both are linear in the digit
count.

Other bases work in chunks: `base_chunk(base)` is the largest power D = base^k that
fits a limb (10^19 at 64-bit limbs). A per-base context holds `k`, `D`, the digit
extraction constants and the tables below; it is built on first use and published
with a compare-and-swap, so concurrent first calls are safe and a loser frees its
copy. Contexts and tables live until exit (about 70 MB at most, if every base 3..255 is used at every size; a few KB per base in practice).

`digits` writes the value as K chunks of k digits and splits each chunk into digits.
The chunks come from a fixed-point fraction instead of divisions (Bouvier and
Zimmermann, "Division-Free Binary-to-Decimal Conversion", IEEE Trans. Computers
63(8), 2014, Algorithm 2): x / D^K is held in L = K + g limbs and each pass
multiplies it by D, so the carry out of a pass is the next chunk, most significant
first, and the lowest limb is dropped. Their initial division is a multiplication
with R = floor(B^(L+n) / D^K), cached per base and size (B is the limb size, n the
limbs of x). R is built from K single-limb divisions of B^(L+n) by D (`divmod_small`,
Moller and Granlund, "Improved division by invariant integers", IEEE Trans.
Computers 60(2), 2011). The fraction starts at (x + 1/2) / D^K rounded down by at
most n + 2 units of its last limb and each pass drops one limb, so every chunk is
exact while the accumulated error stays below 1/2: `_make_radix_table` bounds it as
(n + 2) r^K + sum of r^m for m below K with r = D/B, and adds a guard limb (g = 1)
for the bases where that reaches 0.45. A chunk of k digits is cut into pieces of at
most h digits (base^h < 2^32) by a multiply-and-shift division (Granlund and
Montgomery, PLDI 1994); each piece becomes digits with fixed-point multiplies
(Lemire, Kaser and Kurz, 2019), two at a time through a pair table up to base 64.

Values up to `radix_unrolled` (6 by default) limbs run fully unrolled kernels (`_enc_fixed`) with
the fraction in registers; the table is keyed by bit length, so K is as small as the
value allows. Up to 8192 bits the general kernel (`_enc_general`) runs a four-row
short product and four fused passes, with one table per limb count. Larger values
keep the loop of `divmod_small` passes until a subquadratic tree replaces it.
Tables for sizes never used are never built. `UINTEGER_T_RADIX_UNROLL` (0 to 8,
default 6) sets the number of unrolled sizes; a smaller number trades speed for code
size. At 6 a 32-byte base58 encode takes about 34 ns and adds about 68 KB of text; 8
takes 58 ns instead of 70 at 64 bytes for 50 KB more; 0 takes 53 ns at 32 bytes for 8 KB.

`from_digits` checks every digit in one pass, builds each chunk of k digits with
SWAR multiplies (eight digit bytes in three multiplies, any base up to 255) and
folds four chunks per Horner step, x = x D^4 + a. The chunks are written into the
result's own limbs and each step reads only chunks above the limbs it writes, so no
scratch buffer is needed. The string parser gathers its mapped digits into a byte
buffer first and then takes the same path.

`divmod_small`, `mul_add_small`, `small_divisor` and `base_chunk` are private
implementation. A test can reach them, and the radix tables, by defining
`UINT_T_PUBLIC_IMPLEMENTATION` before including the header.

The parsers validate as they go: a character that is not a digit below the base, or
a digit byte at or above it, throws `std::invalid_argument`, scanning from the most
significant end so the first bad one is reported. The bit-sliced paths check their size
arithmetic (`_limbs_for_digits`, `_digits_for_limbs`): a count that does not fit in
`std::size_t` throws `std::length_error` before any allocation, so a 32-bit `size_t`
cannot wrap into a short buffer. Base 256 parsing through the string
constructors is a separate byte copy.

## Complexity summary

- Addition, subtraction: O(n) in limbs.
- Multiplication: O(n*m) schoolbook below 24 limbs, O(n^1.585) Karatsuba from 24 limbs,
  O(n^1.465) Toom-3 from 140 limbs. Squaring takes about 0.6 of the time.
- Division, modulo: O(n*m) schoolbook (Algorithm D) below 32 limbs; recursive division
  above, O(n^1.585) with Karatsuba.
- `digits`, `from_digits`, `str` and parsing for power-of-two bases: O(bits); other
  bases: O(n^2), about n limb products for each of the K chunks of digits (K is a
  little above n).
- `bits()`: O(1) given the limb count plus a leading-limb bit scan.

## Design decisions

- Limbs in a `std::vector<uint64_t>`, little-endian, trimmed of leading zeros.
  The empty vector is zero, which keeps `operator bool()` and emptiness checks
  trivial.
- The arithmetic lives in raw-limb kernels that take pointers and lengths, so
  recursion slices operands by pointer and shares one scratch buffer instead of
  building `uinteger_t` temporaries.
- Intrinsic carry/borrow/multiply primitives are selected at compile time
  through `HAVE_*` macros, so the hot limb loops use hardware add-with-carry and
  128-bit multiply where available, and a tested portable path elsewhere.
- Shifts split the count into a limb count and a bit count with `n / digit_bits` and
  `n % digit_bits` and run `k_lshift` / `k_rshift` on the limbs. A native-integer
  count (`a << 3`, truncated to one limb as before) and a `uinteger_t` count of up to
  64 bits (`64 / digit_bits` limbs: one at 64-bit limbs, two at 32-bit, four at 16-bit)
  take that path directly, with no temporary and no division. A longer count is 2^64
  bits or more, beyond any size a value can have: it throws `std::length_error` when a
  nonzero value would shift left by it, and gives zero shifting right.
- Appending grows the vector by 1.5x, which keeps repeated appends cheap.
- `divmod` is the primitive; `/` and `%` run its code and keep one output.

## Limitations

- Unsigned only. No negative values; subtraction wraps.
- Native-integer conversion operators are explicit and truncate to the low limb.
- The `namespace std` trait specializations (`is_arithmetic`, `is_integral`,
  `is_unsigned`) are a deliberate convenience and are technically undefined
  behavior; the header says so at the point it adds them.
- No signed type, rationals, or modular-arithmetic helpers; this is a plain
  arbitrary-precision unsigned integer.

# Architecture

`uinteger_t` is one header, `uinteger_t.hh`, holding a single class
`uinteger_t`. This document covers how the value is stored, the arithmetic
algorithms, their complexity, and the design choices that shape the code.

## Internal representation

A value is a sequence of 64-bit limbs ("digits") held in `uinteger_t::container`,
a vector with a small buffer, little-endian: the limb at index 0 is the least
significant. The limb type is `DIGIT_T` (default `std::uint64_t`) with a
`HALF_DIGIT_T` (default `std::uint32_t`) used where the code needs to split a
limb in half during multiplication and base conversion. A `static_assert`
enforces that the half digit is exactly half the width of the digit.

The object is its container and nothing else: `sizeof(uinteger_t)` equals
`sizeof(container)`, so every operation can hand raw limb pointers to a kernel after
sizing its result. In-place right shifts move the limbs down and shrink the container.

`container` keeps up to `inline_capacity` limbs inside the object
(`UINTEGER_T_INLINE_LIMBS`, by default 64 bytes' worth: 8 limbs at 64-bit limbs, 16
at 32-bit, 32 at 16-bit) and puts a longer value in one heap block. It is a data
pointer (the buffer or the block), a size, a capacity and the buffer: three words plus
the buffer, 88 bytes at the defaults on LP64. The block is in use exactly when the capacity
is above `inline_capacity`. `data()` returns the pointer member. Choosing
between the buffer and the block by comparing the capacity would make the compiler read
the block pointer, which overlaps the first buffer limb, so each operation's addresses
would wait for the previous operation's store to that limb. A copy of a value
that fits goes into the buffer, whatever storage the source uses; a move of an
inline value copies its limbs and a move of a heap value takes the block and points
the source back at its buffer, leaving it zero. Shrinking by `trim()` or a right
shift keeps the block, and copy assignment reuses whatever storage fits and
allocates before it releases, so a failed assignment leaves the target as it was.
A result of up to `inline_capacity` limbs costs no allocation; a longer one costs one.

Leading-zero limbs are stripped by `trim()` so the limb
count tracks the significant magnitude and `size()` reflects it. The value zero
is the empty limb sequence, which is why `operator bool()` is just
`size() != 0`. A result may be the same object as an operand (`a += a`,
`x = x - y`): each operation reads its operand sizes first, resizes the result,
and takes the data pointers after that.

## Growth strategy

`append` grows the container by a geometric `growth_factor` of 1.5: `grow(n)` reserves
`n * 1.5` when it needs more room, which keeps repeated appends to the most significant
end amortized O(1) without the 2x factor's tendency to leave large unreusable holes.
`resize` leaves the buffer for a block of exactly the size asked and doubles a block
that is already on the heap. Operations that know their result size (`+`, `-`, `*`,
shifts, division) size the result once and allocate once, or not at all while it fits
the buffer. A sum makes room for its carry limb before it writes anything: always when the
result is past the buffer (one allocation for the whole result), and within the buffer only
when the top limbs can carry (their sum wraps or is all ones) and the capacity has no spare
limb, so an inline sum that cannot carry allocates nothing and a failed allocation leaves the
operands as they were. Every operation that grows a value sizes the result before its first
store, for the same reason. Where a
kernel writes every limb it adds (the same operations), the result is sized without
zero-filling. `prepend` inserts at the front of the container.

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
the property test that way. MSVC itself is not in CI, so its intrinsic paths are
built only by MSVC users.

Subtraction is unsigned: a smaller-minus-larger result wraps rather than going
negative, matching how a fixed-width unsigned type would behave for the operand
width involved.

## Limb kernels

The arithmetic runs on raw limb arrays (`digit*` plus an explicit length), in the
`k_*` kernels: add/sub with carry, multiply-by-limb (`k_mul_1`, `k_addmul_1`,
`k_submul_1`), shifts, and the multiplication and division routines below. The
`uinteger_t` operators size and trim the result and call a kernel once, so the
inner loops never touch the container or allocate.

## Multiplication

`k_mul` dispatches on the operand sizes, `an >= bn`:

- Below `mul_karatsuba_threshold` limbs of the smaller operand (24) it runs
  `k_mul_basecase`, schoolbook multiplication that takes two limbs of the
  multiplier per pass over the multiplicand. Both rows' products and carries
  stay in registers, which is about 1.6x faster than one row per pass.
- When the product has at least twice `mul_ntt_threshold` limbs (1400) in all, the
  smaller operand at least two thirds of that, and the larger fewer than 16 times as
  many limbs as the smaller (`ntt_lopsided`), it runs the transform multiplication
  below. O(n log n). A balanced product takes it from `mul_ntt_threshold` limbs.
- When one operand has at least twice the limbs of the other, it multiplies
  the shorter one by successive equal-size chunks of the longer one and adds
  the partial products, so the algorithms below see near-balanced operands.
- From `mul_toom4_threshold` limbs (400), when the smaller operand reaches a fourth
  piece (`bn > 3k` with `k = ceil(an/4)`), it runs Toom-4. O(n^1.404).
- From `mul_toom3_threshold` limbs (140), when the smaller operand reaches a third
  piece (`bn > 2k` with `k = ceil(an/3)`), it runs Toom-3. O(n^1.465). It takes the
  sizes below the Toom-4 cutoff and the shapes between the two piece boundaries.
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

Toom-4 splits each operand into four pieces of `k` limbs and evaluates at 0, 1, -1,
2, -2, 1/2 and infinity. The value at 1/2 is taken scaled by 8 so that it stays an
integer, `8 x0 + 4 x1 + 2 x2 + x3`; every evaluation but those at 0 and infinity takes
`k + 1` limbs, with the signs of `x(-1)` and `x(-2)` kept as flags. Of the seven
products, `c0 = a0*b0` and `c6 = a3*b3` land straight in the result and the other five,
of `k + 1` limbs each, go to scratch. The values at `p` and `-p` give the even and odd
parts of the product at `p` (Knuth, TAOCP vol. 2, 4.3.3; Bodrato and Zanoni, ISSAC 2007;
Bodrato, "Towards Optimal Toom-Cook Multiplication for Univariate and Multivariate
Polynomials in Characteristic 2 and 0", WAIFI 2007). With `c0` and `c6` taken out, the
values at 1 and -1 give `p = c2 + c4` and `q = c1 + c3 + c5`, those at 2 and -2 give
`t2 = c2 + 4 c4` and `t1 = c1 + 4 c3 + 16 c5`, and the value at 1/2, less `64 c0`,
`16 c2`, `4 c4` and `c6`, halved, gives `h = 16 c1 + 4 c3 + c5`:

    c4 = (t2 - p) / 3          c2 = p - c4              c3 = (17 q - h - t1) / 9
    c1 - c5 = (h - t1) / 15    c1 = (q - c3 + (c1 - c5)) / 2    c5 = q - c3 - c1

The steps run modulo `B^(2k+2)` as in Toom-3. The divisions by 3, 9 and 15 are exact and
go limb by limb with the inverse of the divisor mod `B` (`k_divexact_3`, `k_divexact_1`);
`c1 - c5` is the one intermediate that can be negative, and the exact division is valid
on its two's complement form. Every halving and shift acts on a non-negative value. The
five middle coefficients are added into the result at limbs `k` to `5k`. The scratch is
`15 (k + 1)` limbs plus that of the products below it. Toom-4
takes operands of at most `toom4_max_limbs` (`SIZE_MAX / (32 sizeof(digit))`) limbs, so
the scratch, in bytes and summed through the recursion, cannot wrap `std::size_t`; a larger
product takes Toom-3.

Squaring has its own kernels. `operator*` and `operator*=` take `k_sqr` when both
operands are the same value (the same object). `k_sqr_basecase` forms each
off-diagonal product `a[i]*a[j]`, `i < j`, once, two rows of the triangle per
pass, doubles the triangle and adds the squares `a[i]^2`, about 0.6 of a
multiplication. `k_sqr_karatsuba` uses `2 a0 a1 = a0^2 + a1^2 - (a0 - a1)^2`
(three half-size squares) and `k_sqr_toom3` takes five squares with no signs to
track. Squaring crosses over at `sqr_karatsuba_threshold` (40 limbs) and
`sqr_toom3_threshold` (300 limbs), then takes Toom-3 up to the transform: Toom-4
squaring measures no faster than Toom-3 below 1400 limbs.

The Karatsuba, Toom-3 and Toom-4 cutoffs are literal limb counts: a crossover depends on
the cost of a limb product against a limb add, and that ratio is the same at 64- and 32-bit
limbs. They come from `bench/tune.cc` sweeps (each algorithm timed at the top level, the
recursion below it taking the library's cutoffs) and can be set with
`UINTEGER_T_MUL_KARATSUBA_THRESHOLD`, `UINTEGER_T_MUL_TOOM3_THRESHOLD`,
`UINTEGER_T_MUL_TOOM4_THRESHOLD`, `UINTEGER_T_SQR_KARATSUBA_THRESHOLD` and
`UINTEGER_T_SQR_TOOM3_THRESHOLD`; the
tests set tiny values so every path runs at small sizes. The transform cutoffs
(`mul_ntt_threshold` and `sqr_ntt_threshold`, 1400 limbs each, set with
`UINTEGER_T_MUL_NTT_THRESHOLD` and `UINTEGER_T_SQR_NTT_THRESHOLD`) are swept at
64-bit limbs; a narrower limb packs more limbs into a transform word, so the
crossover, in limbs, is lower there.

### Transform multiplication

Above the cutoffs `k_ntt_mul` multiplies by a number-theoretic transform (Pollard,
"The fast Fourier transform in a finite field", Math. Comp. 25, 1971; Knuth, TAOCP
vol. 2, 4.3.3; Crandall and Pomerance, Prime Numbers, 9.5). The operands are read as
polynomials in 2^64 whose coefficients are 64-bit words (one limb at 64-bit limbs,
`64 / digit_bits` limbs packed at narrower ones, so the transform is the same at
every limb width). A coefficient of the product is a sum of at most `min(aw, bw)`
products of two words, below `N 2^128` for a transform of `N` words. Three primes
`c 2^53 + 1` (`c` = 501, 471, 459, each between 2^61 and 2^62) multiply to 2^185.7, so
the cyclic convolutions of length `N` modulo the three primes determine every
coefficient exactly for any `N` up to 2^53, and Garner's form of the Chinese remainder
theorem gives them back as three-word values that are summed into the result with
the carries kept in two words. There is no splitting of limbs into smaller digits and
no rounding: the arithmetic is exact.

The length is the least of `2^k` and `3 2^k` that holds `aw + bw - 1` coefficients,
so the padding is at most half again of the product once it has more than eight
coefficients (the primes have `3 | c`, so a cube root of unity exists). The transform is decimation in frequency from natural to
bit-reversed order, the inverse decimation in time back, so the pointwise product
needs no permutation. A length `3M` is one radix-3 layer, which reduces the input
modulo `x^M - z^j` for the cube roots of unity `z` and twists each residue into a
cyclic convolution, followed by three transforms of length `M`; with `1 + z + z^2 = 0`
the 3-point butterfly takes one product. Twiddle factors are Shoup's (the
word `w` and the quotient `floor(w 2^64 / p)` stored together: Shoup, "New algorithms
for finding irreducible polynomials over finite fields", Math. Comp. 54, 1990;
Harvey, "Faster arithmetic for number-theoretic transforms", J. Symbolic Comput. 60,
2014), so a butterfly is one high and two low word products. Since `4p < 2^64` values
stay in `[0, 2p)` with one conditional subtraction per sum and are reduced fully only
before the Chinese remainder step. The inverse reads its twiddles from the forward
table backwards (`w^-t = -w^(h-t)`; the negated twiddle `p - w` has the quotient
`~q`), the last two layers are fused, and blocks of 2048 words (`UINTEGER_T_NTT_LEAF`)
are transformed layer by layer inside the cache while larger ones take one pass and
split. The pointwise step scales by `2^64 / N` and Montgomery-reduces
(Montgomery, "Modular multiplication without trial division", Math. Comp. 44, 1985),
which folds the inverse length in. The table is built per product, a few percent of
its time. A square transforms its operand once.

The word primitives `_mul64` and `_div64` take the `__int128`, MSVC `_umul128` and
`_udiv128`, MSVC ARM64 `__umulh` or portable paths like the limb ones, and the
Shoup quotients come from a 2-by-1 division with a reciprocal (Moller and Granlund).

The scratch comes from the same up-front sizing as the other kernels: three arrays of
`N` words for the residues, one for the second operand's transform (none for a
square), the twiddle table (`N + 2` words, `4N/3 + 2` for `3 2^k`) and a word to align
by, so `5N + 3` words (`4N + 3` for a square) and up to 5.33 N + 3 for the `3 2^k`
lengths. For an `n`-limb by `n`-limb product at 64-bit limbs `N` is between `2n` and
`3n`, so the scratch is 10 to 16 limbs per limb of operand, that is 5 to 8 times the
product, plus the product. `_ntt_plan` computes the length and the size in a wide type
and refuses, so `k_mul` takes Toom-4, when the length passes 2^53 or the scratch is
over a quarter of what `std::size_t` addresses in bytes, which also keeps the sums of
scratch sizes in the callers from wrapping. The transform takes products up to 16
times lopsided as one transform over the whole product (cheaper than chunking, with
scratch a few times the product); a longer operand is chunked first and the chunks
take the transform.

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
256 limbs and uninitialized (the kernels write every limb before they read it).

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
- From `div_newton_threshold` (1400) limbs of divisor, `div_newton_quotient_threshold` (5000)
  limbs of quotient, and a quotient of at least half the divisor's limbs, `k_div_dc` hands
  the division to `k_div_newton`. The two thresholds are literal limb counts, the same at every
  limb width; a block's two products cost the same however short the quotient, so a short
  quotient over a tall divisor stays with the recursive division. `k_div_newton` divides
  with an approximate reciprocal of the divisor (Barrett, "Implementing the Rivest Shamir and
  Adleman public key encryption algorithm on a standard digital signal processor", 1986; Brent
  and Zimmermann, *Modern Computer Arithmetic*, 3.3 and 3.4), on the same normalized contract.
  Every product goes through `k_mul`, so the transform serves the large ones.
  - The reciprocal of a normalized `n`-limb divisor `t` (B^n/2 <= t < B^n) is `W = B^n + X`
    with `X` of `n` limbs and `0 <= B^(2n)/t - W < 2`. Below `div_newton_inv_base` limbs it is
    exact: one schoolbook division of `(B^n - 1 - t) B^n + B^n - 1` by `t`. Above, with
    `l = (n - 1) / 2` and `h = n - l`, the reciprocal `W_h` of the top `h` limbs is refined by one
    Newton step. The truncation of `t` to `h` limbs lifts `B^(2h)/t_h` above `B^(n+h)/t` by less than
    4, so `W_h' = W_h - 4` is at most `B^(n+h)/t` and `E = B^(n+h) - t W_h'` is not negative. It is
    below `6 t`, so it is the negation of the low `n + 1` limbs of the product `t W_h'` (one
    `k_mul`). Then `W = W_h' B^l + floor(W_h' E / B^(2h))`, where only the top `l + 2` limbs of `E`
    enter the second `k_mul` (the dropped limbs change the quotient by less than `2/B`). A Newton
    step squares the error of its start, which is below `6 B^l` units of `W`, and `2l < n` keeps the
    square under one unit, so the two floors are the whole error: the result is below the
    exact value by less than `1 + 60/B` and, as that value exceeds `B^n + 1`, is at least `B^n`.
  - The quotient of `m` limbs is cut into at least two blocks of `c` limbs, `c <= dn`, from the
    top; one reciprocal, of the top `c` limbs of the divisor, serves every block, and the first
    block (`m mod c` limbs) takes the top limbs of it. A block divides the `dn + c` limbs `a` below
    `b B^c`. With `A` the top `c` limbs of `a` and `X` the reciprocal's low `c` limbs, the estimate
    is `q = A + floor(A X / B^c)`, below `B^c` since `A < B^c` and `W` is at most the exact reciprocal. The
    estimate is at most 2 above the quotient and at most 4 below it (8 for the first block, whose
    reciprocal is the top of a longer one). The remainder `a - q b` is formed modulo `B^(dn+1)`
    with one `c` by `dn` product, then `b` is added back while it is negative (at most twice) and
    subtracted while it is not below `b` (at most 8 times). `div_newton_down` and `div_newton_up`
    are those bounds, asserted in every block.
  - One scratch buffer holds the reciprocal (`c + 1` limbs) and, after it, the larger of the
    reciprocal's two products with their `k_mul` scratch and a block's two. `_div_newton_plan`
    sizes it in `unsigned long long` and refuses (`k_div_dc` takes the recursive division) when
    it is more than a quarter of the address space in bytes or the dividend more than a
    sixty-fourth of it in limbs, so no size sum wraps at a 32-bit `size_t`.
  - A balanced division is the reciprocal of half the divisor's limbs and two blocks of
    half the divisor's limbs, each a `c`-limb product and a `c` by `dn` product: a few
    multiplications of the divisor's size in all.
- A quotient-only division (`a / b`, `a /= b`) with a short quotient takes
  `k_div_sb_trunc`, a schoolbook division that skips part of every digit's product.
  The digit `i` places above the last one subtracts `q D` only over the top
  `min(dn - 2, i + 2)` limbs below the 3-by-2 window; the lower limbs of the window stay
  stale, and no later digit reads them (the last digit reads from limb `dn - 4` up). The work
  is about `qn^2 / 2` limb products where `k_div_sb` takes `qn * dn`, and it does not depend on
  the divisor's length once `dn > qn + 2`. The skipped limbs make the window of every
  later digit exceed the true one by `delta`: with `B` the limb base and `D` the divisor, a
  digit at position `m < dn - 4` leaves out `q_m (D mod B^k) B^m < B^(dn-3)`, `k = dn - 4 - m`, so
  `delta <= qn B^(dn-3-i) + 1` at digit `i`.
  The estimate `q^` is the exact 3-by-2 one (`floor(W / D)` or one more), the
  subtraction leaves `Y = W - q^ (D - D mod B^k)` and the true remainder is `Y - u`, `u = q^ (D mod B^k)`.
  The recorded digit `q^ - [Y < 0]` is therefore the true one unless `0 <= Y < u + delta`, and for
  `qn < B` that sum is below `B^(dn-2)`: the digit is right whenever the top two limbs of `Y` are not both zero
  (flag 2) or the window's top two limbs differ from the divisor's (flag 1, which also keeps the
  3-by-2 step's precondition; the step's own remainder keeps them at most equal). On a flag the
  kernel repairs its state. Every digit up to the flag is the true one, so the limbs hold the exact
  state plus the sum of `q_m (D mod B^k_m) B^m` over the truncated digits applied so far, and nothing
  else (the limbs the truncated digits skipped are dividend limbs). The kernel subtracts those terms,
  which leaves the exact remainder after the last digit applied. At flag 2 that digit used `q^`, the
  true digit is `q^` or `q^ - 1`, and the remainder is negative exactly when a subtraction wrapped (the
  integer is the limbs minus one wrap times the modulus, and one wrap at most happens): then `D` is
  added back and `q^` lowered. The exact digit step finishes the remaining digits, so a flagged
  division returns the exact quotient and remainder. If a check on the repaired remainder (below `D`,
  no wrap at flag 1) fails, which the argument excludes, the kernel returns a failure and the wrapper
  loads the dividend again from the operand (untouched until the outputs are written) and runs the
  exact kernels. A flag has probability about `2^-(2 * limb bits)` per digit on random operands; an
  exact multiple (`a = q b`) always flags, at the quotient's lowest nonzero digit, and then costs
  the truncated run and the skipped products, about one schoolbook division: the cost of the exact
  path below 32 limbs, and more than the recursive division above, so `div_trunc_threshold` sits
  where that excess stays small. A quotient of `B` limbs or more (16-bit limbs only) runs the
  exact kernel, outside the bound (`div_trunc_max_quotient`).
  The wrapper takes the kernel when the quotient is no longer than the divisor and under
  `div_trunc_threshold` limbs, under twice that when it is at most half as long, and under half of it when
  it is at most twice as long: a short quotient needs only the top limbs of the divisor, where the
  recursive division multiplies by all of them. The full proof, lemma by lemma against the code, is in
  [docs/design/quotient-only-division.md](docs/design/quotient-only-division.md).

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

Base 256 is whole bytes. The parser (`uinteger_t(bytes, size, 256)`) skips leading zero bytes
(they carry no value, so the value has no zero top limb and zero bytes make zero), then builds each limb from the
`digit_octets` bytes that end it, loaded most significant first, and the top limb from the
bytes left over. The load is the same on little- and big-endian hosts. An empty input throws. `digits`
into a one-byte result writes each limb big-endian, the top limb as many bytes as it has
significant ones, and gives one 0 byte for zero.

Other bases work in chunks: `base_chunk(base)` is the largest power D = base^k that
fits a limb (10^19 at 64-bit limbs). A per-base context holds `k`, `D`, base^8 (the step of the
decoder's eight-digit groups), the digit extraction constants and the tables below; it is built on first use and published
with a compare-and-swap, so concurrent first calls are safe and a loser frees its
copy. Contexts and tables live until exit (43 MiB at most at 64-bit limbs, if every base 3..255 is used at every size; a few KiB per base in practice; see Memory and compile cost).

`digits` writes the value as K chunks of k digits and splits each chunk into digits.
The chunks come from a fixed-point fraction, with no division per chunk (Bouvier and
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
(Lemire, Kaser and Kurz, 2019), two at a time through a pair table up to base 64. The
chunk shape (k, h) takes only 17 values over the bases 3 to 256 that are not powers of
two at 64-bit limbs, and each has its own splitter, `_split_chunk_kh<K, H>`: with the
digit counts constants, a piece is a straight run of multiplies and table loads and
no loop or count test is left to resolve at run time. `_make_radix` stores the one
for the base's shape in `radix::split` (`_pick_split` holds the shapes). The general
`_split_chunk_general` reads the shape from the context and serves any other (every
base at 32-bit limbs, where a chunk is one piece); `UINTEGER_T_SPLIT_SHAPES=0`
selects it for every base. `test/property.cc` checks each splitter against it and
against division, for every base that takes it.

Values up to `radix_unrolled` (6 by default) limbs run fully unrolled kernels (`_enc_fixed`) with
the fraction in registers; the table is keyed by bit length, so K is as small as the
value allows. Up to 8192 bits the general kernel (`_enc_general`) runs a four-row
short product and four fused passes, with one table per limb count. Larger values
take the tree below.
Tables for sizes never used are never built. `UINTEGER_T_RADIX_UNROLL` (0 to 8,
default 6) sets the number of unrolled sizes; a smaller number trades speed for code
size. At 6 a 32-byte base58 encode takes about 34 ns and adds about 68 KB of text; 8
takes 58 ns instead of 70 at 64 bytes for 50 KB more; 0 takes 53 ns at 32 bytes for 8 KB.

`from_digits` checks every digit in one pass, builds each chunk of k digits with
SWAR multiplies (eight digit bytes in three multiplies, any base up to 255) and
folds four chunks per Horner step, x = x D^4 + a. The chunks are written into the
buffer that becomes the value (`_horner_limbs`) and each step reads only chunks above the
limbs it writes, so it touches only the ceil(m / k) limbs of the chunks and needs no scratch.
Up to 16 chunks that buffer is on the stack and the result is sized to the limbs the value
has; longer values work in the result. The string parser gathers its mapped digits into a byte
buffer first and then takes the same path.

Above 8192 bits both directions go through a tree over those kernels at 32- and 64-bit limbs
(at 16-bit limbs `digits()` keeps the loop of `divmod_small` passes and `from_digits()` the
limb accumulator) (Brent and
Zimmermann, *Modern Computer Arithmetic*, 2010, section 1.7). The tree is built per
call from the powers P_j = D^(T 2^j): P_0 = D^T by square-and-multiply, then P_(j+1) =
P_j^2 with `k_sqr`, so a leaf is T chunks, s = T k digits, and values below P_0. The
number of levels J and T follow the size of the value: J is the fewest levels for which
T 2^J chunks cover it with a leaf of at most the leaf limit (7680 bits to encode, 8192
to decode), and T >= 2 so that P_0 has two limbs, the least a division takes. The
tree is full, every leaf is between half and all of the limit, and the top power is
about the size of the value. Powers are not cached between calls: building them costs
about 6% of an encode and 11% of a decode at 1K to 4K limbs.

`digits` takes a value below P_j and writes exactly s 2^j digits, zero-padded. It
divides by P_(j-1) with `k_div_dc` (the dividend and the power are shifted left to
normalize the divisor, the remainder shifted back): the quotient is below P_(j-1)
and is the high half, the remainder is the low half. A value with fewer limbs than
the power has a zero high half and skips the division. A leaf (below P_0) is padded
with zero limbs to P_0's limb count and run through the general kernel with that
size's table, and its digits are right-aligned. The top level takes J so that the
value is below P_(J-1)^2 (checked from the bit length of P_(J-1), `_tree_plan`) and
strips the leading zeros at the end, so P_J is never built. `from_digits` takes
m <= s 2^j digits: the low half is the last s 2^(j-1) digits, and the value is
high * P_(j-1) + low, with the high half converted first and multiplied by `k_mul`
(the sum has no carry out, since low < P_(j-1)). Leading zero digits are stripped
first and a leaf is the Horner decoder above. Each level costs a few multiplications
of its size, so both directions are O(M(n) log n) with M the multiplication.

The tree takes over at 8192 bits for both directions (a size the kernels' tables stop
at for encoding; for decoding the tree and Horner cross at about 8192 to 10000 bits).
The cutoffs and leaf limits are `UINTEGER_T_DIGITS_TREE_THRESHOLD`, `_DIGITS_TREE_LEAF`,
`_FROM_DIGITS_TREE_THRESHOLD` and `_FROM_DIGITS_TREE_LEAF`, in limbs (the defaults are
bit counts over the limb width, from sweeps at 64- and 32-bit limbs in five bases; any
leaf from 4096 to 12288 bits is within 3% of the best). The tests set them to two limbs
so the tree runs at every size. Raising `UINTEGER_T_RADIX_MAX_BITS` would let the
quadratic kernel run to about 320 limbs, up to 15% faster than the tree between 129 and
320 limbs, but the table of each new limb count is built from K single-limb divisions
of about 2n limbs, so a first use of every size in the range costs a quadratic build.

A scaled remainder tree (Bernstein; Bouvier and Zimmermann, Algorithm 3) would replace
each division by a middle product of the same size. With only full products a middle
product costs 2 M(n/2), the same as the division (about 2.1 M(n/2) measured), so it
would pay only with a middle-product kernel (a transposed Karatsuba or Toom).

`divmod_small`, `mul_add_small`, `small_divisor` and `base_chunk` are private
implementation. A test can reach them, the radix tables and every other
private member by defining `UINTEGER_T_PUBLIC_IMPLEMENTATION` before including the header.

The parsers validate as they go: a character that is not a digit below the base, or
a digit byte at or above it, throws `std::invalid_argument`, scanning from the most
significant end so the first bad one is reported. The bit-sliced paths and the digit tree check their size
arithmetic (`_limbs_for_digits`, `_digits_for_limbs`, `_tree_digits_for`): a bit count or a digit capacity that does not fit in
`std::size_t` throws `std::length_error` before any allocation, so a 32-bit `size_t`
cannot wrap into a short buffer. The tests pass narrower size types to take each guard. Base 256 parsing through the string
constructors is a separate byte copy.

## Complexity summary

- Addition, subtraction: O(n) in limbs.
- Multiplication: O(n*m) schoolbook below 24 limbs, O(n^1.585) Karatsuba from 24 limbs,
  O(n^1.465) Toom-3 from 140 limbs, O(n^1.404) Toom-4 from 400 limbs (900 for squares),
  O(n log n) transform from 1400 limbs. Squaring
  takes about 0.6 of the time below the transform and 0.75 of it above.
- Division, modulo: O(n*m) schoolbook (Algorithm D) below 32 limbs; recursive division
  above, a few multiplications per level, so it follows the multiplication; from 1400
  limbs of divisor and 5000 of quotient, a reciprocal and a few multiplications of the
  divisor's size per block of quotient, O(M(n)) for a balanced division. A quotient alone
  with a short quotient: O(m^2) in the quotient's limbs, whatever the divisor's length.
- `digits`, `from_digits`, `str` and parsing for power-of-two bases: O(bits); other
  bases: O(n^2) below 8192 bits, about n limb products for each of the K chunks of
  digits (K is a little above n), and O(M(n) log n) above, where M is the multiplication
  (O(n^1.404) with Toom-4, so O(n^1.404 log n) below the transform).
- `bits()`: O(1) given the limb count plus a leading-limb bit scan.

## Memory and compile cost

Working memory beyond the operands and the result, at 64-bit limbs. The scratch of a
multiplication is sized up front (`k_mul_scratch_size`, `k_sqr_scratch_size`) and the
numbers below are that size over the product's length, for operands of equal length:

| Operation | Limbs of operand | Scratch / product |
|---|---|---|
| Product | below 24 | none |
| Product, Karatsuba | 24 to 139 | 1.0 to 1.8 |
| Product, Toom-3 | 140 to 399 | 2.7 to 2.8 |
| Product, Toom-4 | 400 to 1399 | 2.3 to 2.6 |
| Product, transform | 1400 and up | 5.0 at a power-of-two length, up to 8.0 one limb above it |
| Square, Karatsuba | 40 to 299 | 0.8 to 1.3 |
| Square, Toom-3 | 300 to 1399 | 2.1 to 2.4 |
| Square, transform | 1400 and up | 4.0 at a power-of-two length, up to 6.5 one limb above it |
| Division by two or more limbs | dividend of `m` limbs | the buffer holds the divisor, the dividend and the quotient (about 2m) and the scratch: none below a 32-limb dividend, 1.0 to 2.1 m for divisors of 60 to 1400 limbs, 3.0 to 3.8 m for 3000 and more |

A product up to 16 times lopsided takes one transform over the whole product, with up to 8.0
times the product of scratch; a longer operand is chunked first. Addition, subtraction,
shifts and the bitwise operators need nothing beyond the result, which may be an operand.
A multiplication or division scratch of up to 256 limbs is on the stack;
larger ones are on the heap. The multiplication allocates its scratch before it touches the
result, so a failed allocation leaves the result as it was.

The peak of a whole operation is the scratch with the operands and the result. The square of a
1M-limb value (an 8 MB operand, a 16 MB product) peaks at 98 MiB. A base-58 decode of 1M
characters (a 0.73 MiB value) peaks at 14.8 MiB, 15 bytes per character and 20 times the
value, and the peak grows linearly with the length of the input: a caller that decodes untrusted
text caps its length first.

The per-base conversion tables are built on first use and never freed. Once every size up to
8192 bits has been converted in a base, its tables hold about 180 KiB (330 KiB at 32-bit limbs);
all 247 bases that are not powers of two hold 43 MiB (83 MiB at 32-bit limbs). A program that
converts keys of one size in one base holds a few KiB. The transform's twiddle tables are
built per product and live in its scratch.

Including the header costs 6.3 G instructions in the compiler (an empty translation unit, at -O0
and at -O2, Apple clang 17). The rest is the code a translation unit instantiates: `str()`, `operator<<`, `digits()` and
the string constructors pull in the conversion kernels, and the kernels unrolled to
`UINTEGER_T_RADIX_UNROLL` limbs are most of it. A typical translation unit (construct, multiply,
divide, shift, print in base 16 and 10, parse) at -O2:

| `UINTEGER_T_RADIX_UNROLL` | Compiler instructions | Object |
|---|---|---|
| 0 (general kernel only) | 21.2 G | 88 KiB |
| 6 (default, keys up to 48 bytes) | 46.9 G | 173 KiB |
| 8 | 72.5 G | 237 KiB |

At -O0 the three are within 6% of each other (7.6 to 8.1 G instructions). A translation unit that
does not convert text compiles the same at every setting (17.1 G instructions and 49 KiB for the
same code without `str()` and the string constructors). One that wants the smallest build defines
`UINTEGER_T_RADIX_UNROLL=0` before the header.

## Design decisions

- Limbs in a small vector, little-endian, trimmed of leading zeros. The empty
  container is zero, which keeps `operator bool()` and emptiness checks trivial.
- Values of up to 8 limbs (at 64-bit limbs) live in the object, so the common
  small result, such as a base58 key or the product of two 4-limb values, has no heap
  block. The price is the object size (88 bytes, where a `std::vector` takes 24) and
  that a move of a small value copies its limbs; only a heap value hands over its block.
- The arithmetic lives in raw-limb kernels that take pointers and lengths, so
  recursion slices operands by pointer and shares one scratch buffer, with no
  `uinteger_t` temporaries.
- Intrinsic carry/borrow/multiply primitives are selected at compile time
  through `HAVE_*` macros, so the hot limb loops use hardware add-with-carry and
  128-bit multiply where available, and a tested portable path elsewhere.
- Shifts split the count into a limb count and a bit count with `n / digit_bits` and
  `n % digit_bits` and run `k_lshift` / `k_rshift` on the limbs. A native-integer
  count (`a << 3`, truncated to one limb) and a `uinteger_t` count of up to
  64 bits (`64 / digit_bits` limbs: one at 64-bit limbs, two at 32-bit, four at 16-bit)
  take that path directly, with no temporary and no division. A longer count is 2^64
  bits or more, beyond any size a value can have: it throws `std::length_error` when a
  nonzero value would shift left by it, and gives zero shifting right.
- Appending grows the container by 1.5x, which keeps repeated appends cheap.
- `divmod` is the primitive; `/` and `%` run its code and keep one output.

## Limitations

- Unsigned only. No negative values; subtraction wraps.
- Native-integer conversion operators are explicit and truncate to the low limb.
- The `namespace std` trait specializations (`is_arithmetic`, `is_integral`,
  `is_unsigned`) are a deliberate convenience and are technically undefined
  behavior; the header says so at the point it adds them.
- No signed type, rationals, or modular-arithmetic helpers; this is a plain
  arbitrary-precision unsigned integer.

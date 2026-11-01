# Multiplication: bounds, cutoffs and measured shapes

ARCHITECTURE.md describes the multiplication algorithms. This note holds what a maintainer needs
before changing one of their kernels: the size bounds each Toom step rests on, the transform's
constants and value ranges, the measurements behind the cutoffs, and the kernel shapes that measure
slower. Sweeps are `bench/tune.cc` on an Apple M4 Pro (Apple clang 17, `-O2`) unless a line names
another machine; the Apple M1 Pro and Intel Core i9-9980HK rows are interleaved A/B runs of
`bench/bench.cc` or of a kernel microbenchmark.

## The limb product and the basecase

A limb product in a multiply-add chain is four flag operations, about 1.1 cycles: 0.29 ns on the M4
Pro. GMP's assembly basecase runs at that bound (0.29 to 0.31 ns per product) and `_multadd` at 0.33
to 0.35. Adding the incoming carry last (`_multadd` adds `a`, then `c`) leaves two dependent
operations per product on the carry chain where `x * y + a + c` in the other order leaves three; that
is 8% to 11% on every multiplication. Regrouping the C++ does not reach the last 10% to 15%, and
assembly could at best match GMP.

`k_mul_basecase` takes two rows of the multiplier per pass over the multiplicand. Other shapes, against
it:

| shape | measured |
|---|---|
| one row per pass | two rows are about 1.6 times as fast |
| three rows per pass | 2% to 7% faster at 16 to 32 limbs, 17% at 12; needs head and tail code and a rewrite of the squaring basecase |
| four rows per pass | no faster than three |
| four rows unrolled; two rows unrolled by two columns | slower |
| the first pass peeled | about 3% faster at 12 to 24 limbs, nothing elsewhere |

## Addition and subtraction

`k_add_n` and `k_sub_n` take eight limbs per pass, eight `_addcarry` (or `_subborrow`) calls in a row,
so the compiler chains the carry through the flag (`adds` then `adcs`) inside a pass. At a pass
boundary it moves the carry to a register and back, about 3 cycles per eight limbs, and no C++ form
keeps the flag across a loop edge. That is the gap left to GMP: 0.43 ns per limb against 0.31 on an
Apple M1 Pro (65536 limbs: 28.4 against 20.4 us). On the Intel Core i9-9980HK it is under 10% from
16384 limbs (8.9 against 8.3 us), where both are bound by memory.

| shape | measured |
|---|---|
| one limb per pass (the carry in a register) | 2 cycles per limb, 0.62 ns on the M1 Pro |
| sixteen limbs per pass | 0.375 ns per limb in the kernel, but a 16- or 17-limb sum swings between 7 and 15 ns with code placement |
| a 4/2/1 ladder for the tail | 5% to 20% faster at 3 to 7 limbs, swings with placement at multiples of 8 |
| a short loop inline and the eight-limb loop out of line from 8 limbs | M1 Pro: 2 limbs 5 to 4 ns, 4 limbs 7 to 5; Intel: 4 limbs 8 to 9 ns, 8 limbs 9 to 10, 16 limbs 54 to 56; within 1% on both from 64 limbs |

Below about 64 limbs the new result's heap block costs more than the kernel. With the inline buffer
raised to 72 limbs (no allocation) a 16-limb `a + b` takes 0.008 us (GMP 0.007) and a 64-limb one 0.025
(GMP 0.018) on the M1 Pro, against 0.032 and 0.061 with the allocation: about two thirds of the gap.
The output-reusing form `mul`, `+=`, or a larger `UINTEGER_T_INLINE_LIMBS` avoids it.

## Toom-3 bounds

The pieces have `k` limbs, `x(1)`, `|x(-1)|` and `|x(-2)|` take `k + 1`, and the interpolation runs
modulo `B^N`, `N = 2k + 2` (`B` the limb base). That modulus holds every value it needs:
`|x(-2)| = |x0 - 2 x1 + 4 x2| < 5 B^k`, so each of the three products of `k + 1`-limb values is below
`25 B^(2k) < B^(2k+2)`, and so are the coefficients. The exact division by 3 (`k_divexact_3`) runs limb by
limb from the bottom with the inverse of 3 modulo `B`; the borrow into the next limb is the high limb of
`3 q` (one for each of `B / 3` and `2B / 3` that the quotient limb `q` exceeds) plus the wrap of the
subtraction. `c0 = a0 b0` and
`c4 = a2 b2` land in the result, and the interpolation reads them before it writes the middle
coefficients over them. Dispatch requires `bn > 2k`, so the top piece of the shorter operand has
between 1 and `an - 2k` limbs.

Scratch: `13 m` limbs, `m = k + 1` (six for the three products, six for the evaluations, one for a
temporary), and `10 m` for a square, plus the scratch of the products below.

## Toom-4 bounds

The evaluations at 1 and -1 come from the even part `x0 + x2` (below `2 B^k`) and the odd part
`x1 + x3`; at 2 and -2 from `x0 + 4 x2` and `2 x1 + 8 x3` (below `10 B^k`); and `8 x(1/2) =
8 x0 + 4 x1 + 2 x2 + x3` is below `15 B^k`. Each fits `k + 1` limbs for `B >= 16`, so the top limb is a
small sum (a limb below 8, a shift-out and a carry) and never overflows. A tie of the even and odd
parts gives a zero magnitude, which takes the positive sign harmlessly.

Every halving and shift in the interpolation acts on a non-negative value below `B^N`, `N = 2k + 2`; the
largest, `2 v(2)`, is below `450 B^(2k)`, which is below `B^(2k+2)` for `B >= 22`, every limb width. The
divisions by 3, 9 and 15 are exact: `k_divexact_1` takes the inverse of the divisor modulo `B` by Newton's
iteration from `d d = 1 (mod 8)` (five doublings reach 96 bits) and runs from the bottom with a borrow
of `hi(q d)` plus the wrap, below 16. `c1 - c5` is the one value that can be negative; the exact
division is valid on its two's complement form.

Each coefficient is trimmed before `k_add_at` adds it into the result at limb `i k`. `c5` has at most
`k + max(a3n, b3n) + 1` limbs, and `5k` plus that fits inside `an + bn` because the top pieces have at
least one limb each. An untrimmed coefficient of `N` limbs would add zeros past the end of the result:
the value stays right, so only the `assert` in `k_add_at` or a sanitizer sees it, and a Release build
does not.

Scratch: `15 m` limbs plus the products below. Toom-4 takes operands of at most `toom4_max_limbs`
(`SIZE_MAX / (32 sizeof(digit))`): the scratch is about 5 limbs per limb of the longer operand (7.75
with a transform below), under 16, and the cap compares before `an + 3` can wrap.

## Scratch is not monotone in the size

The scratch of a product depends on the algorithm the size takes, and a larger product can need less:
a 399-limb product takes Toom-3 and 2,214 limbs of scratch, a 400-limb product Toom-4 and 1,875. A caller
sizes scratch with `k_mul_scratch_size` (or `k_sqr_scratch_size`) for the exact operand sizes it
multiplies (`k_div_dc` asks for each of its products), never for a larger product as a bound. The size
functions recurse up to three times per level: 121 calls for a 1024-limb product with Toom-4 (364
without), never timed.

## The cutoffs as measured

| cutoff | default | sweep |
|---|---:|---|
| Karatsuba | 24 | wins from 23 to 24 limbs (26 to 28 at 32-bit limbs) |
| Toom-3 | 140 | wins from 140 to 148 limbs, 5% to 8% ahead by 300; 3% to 8% ahead on unbalanced shapes where it applies (`bn > 2 ceil(an/3)`) |
| Toom-4 | 400 | 4% to 6% ahead of Toom-3 from 448 limbs; within the 3% noise at 256 to 384; 5% to 6% behind at 128 and 192; 2% to 9% ahead on unbalanced shapes (`an` 640 and 1024) |
| square Karatsuba | 40 | wins from about 40 limbs |
| square Toom-3 | 300 | wins from 290 to 300 limbs, 2% to 6% ahead by 330 |
| transform, products | 1400 | Toom-4 is 10% to 20% ahead at 1152 to 1344 limbs; the transform is 2.6% ahead at 1408 and 13% at 1536 |
| transform, squares | 1400 | equal at 1408 |

The Toom-4 cutoff is a property of the machine. On an Apple M1 Pro Toom-4 against Toom-3 is a wash at
512 limbs (49.6 to 51.0 us against 49.9), 2.2% faster at 768 and 6% at 1024, so there it pays from about
700 limbs. Toom-4 squaring is 2% to 7% ahead of Toom-3 only from 896 limbs and up to 7% behind at 288 to
768, so squares take Toom-3 up to the transform.

Toom-4 can gain little over Toom-3: seven products of `n/4` limbs against five of `n/3` save at most
about 10%, and the linear work (about 40 passes of `N` limbs in the interpolation and 20 of `k` limbs per
operand in the evaluation) is about 15% of a 1024-limb product, which leaves about 5%. At 192 limbs the
Toom-3 interpolation is 1045 of a level's 7490 ns, and the division by 3 a fifth of that.

The subtractive Karatsuba (`(a0 - a1)(b0 - b1)` with the signs apart) measures 3% to 7% faster than the
sum form `(a0 + a1)(b0 + b1)` at 32 to 160 limbs, at the same cutoff.

## The transform

Primes and roots. The primes are `c 2^53 + 1` with `c` = 501, 471 and 459, each between 2^61 and 2^62
and each `c` a multiple of 3, so `p - 1` has the factor `3 2^53` and both transform lengths `2^k` and
`3 2^k` have roots. `ntt_root(i)` is `x^(c/3)` for the least `x` that is neither a square nor a cube modulo
the prime (7, 11 and 7), a root of order exactly `3 2^53`. `test/ntt_kernel.cc` checks the primes, the
orders and every derived constant.

Margin. A coefficient of the product is a sum of at most `N` products of two words, below `N 2^128`:
below 2^180 at `N = 2^53`, and 2^179.6 at the largest length `3 2^51`. The three primes multiply to
2^185.7, 5.7 bits spare. Lengths of `3 2^k` start at 12, so the `2^k` part is a multiple of 4 for the
fused last two layers.

Value ranges. `4p < 2^64` lets the values stay in `[0, 2p)`:

- `ntt_mulq`, a Shoup product, gives `[0, 2p)` for any word input (its quotient is short by less than 2).
- A butterfly forms `u + v` or `u - v + 2p`, both below `4p`, and subtracts `2p` once.
- The radix-3 layer forms `a + 2p - half(sum)`, in `(0.5 p, 4p)`.
- The pointwise product's high word is below `p` (as `4p < 2^64`) and its Montgomery reduction lands in
  `[0, p)`.
- Garner's step reduces the first residue exactly and takes the other two unreduced, below `2p`:
  `a1 + p1 - a0'` lies in `(0, 3 p1)` and `a2 + 2 p2 - a0' - e` in `(2, 4 p2)`, both below 2^64.
- The inverse scales by `2^lg` or `3 2^lg` (the inverse radix-3 layer multiplies by 3), with
  `1/3 = (2p + 1) / 3` modulo `p`.

Sizes. At a 32-bit `size_t` the plan takes 2^23-word operands (a 2^24-word transform, 640 MiB of
scratch) and refuses 2^23 + 1 words (a `3 2^23` transform, just over 1 GiB), so `k_mul` takes Toom-4
there.

Tuning. Leaves of 256 to 16384 words measure within noise of each other; 2048 is the default. Radix-4
butterflies are not used: a butterfly takes about 1.2 ns at 256K words against 0.9 at 1K, so the passes
over memory cost under 30%. The transform against Toom-3 (before Toom-4), in us: 146 against 136 at 1250
limbs, 148 against 161 at 1400, 204 against 269 at 2000, 705 against 1059 at 6000. One transform over a
lopsided product beats chunked Karatsuba and Toom-3 from a shorter operand of about 1000 limbs (2400 by
1200: 202 against 247 us; 8000 by 1000: 680 against 757; 12000 by 1200: 1047 against 1477), and the two
are equal at 16 times lopsided, which sets `ntt_lopsided`.

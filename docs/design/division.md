# Division: contracts, bounds and measured shapes

ARCHITECTURE.md describes the four division kernels and how `knuth_divmod` picks one, and
[quotient-only-division.md](quotient-only-division.md) proves the truncated schoolbook kernel. This
note holds the rest of what a maintainer needs before changing a division kernel: the bounds the
schoolbook step, the recursive division and the Newton division rest on, the measurements behind the
thresholds, and the shapes that measure slower.

All kernels share one contract: the divisor `b` of `dn >= 2` limbs is normalized (its top bit set,
`B^dn / 2 <= b < B^dn`, `B` the limb base), the top `dn` limbs of the dividend are below `b`, the
quotient goes to its own array, and the remainder is left in the low `dn` limbs of the dividend.
`knuth_divmod` shifts both operands left to normalize and shifts the remainder back.

## The schoolbook step

`k_div_sb_digit` estimates each quotient limb with the 3-by-2 division of the top three window limbs
by the top two divisor limbs `(d1, d0)` and their reciprocal (Moller and Granlund). That step leaves
the top two limbs of the remainder, `(r1, r0)`, so the multiply-subtract runs over the low `dn - 2`
limbs only and its borrow comes off `(r1, r0)`. The estimate is the true digit or one more, so a
borrow out of `(r1, r0)` adds the divisor back once.

When the window's top two limbs equal `(d1, d0)` the 3-by-2 step does not apply and the digit is
`B - 1`. Its add-back never runs: with `D` the divisor, `dlow` its low `dn - 2` limbs and `wlow` the
window's low `dn - 1` limbs, the remainder is `D - B dlow + wlow`, positive because
`B dlow < B^(dn-1) < D`. Three mutants of that add-back survive every test for this reason.

On an Intel Core i9-9980HK the 3-by-2 step costs about 7 ns (24 cycles) per digit and `k_submul_1`
0.96 to 1.03 ns per limb, against 0.63 to 0.88 for GMP's `mpn_submul_1`. Shapes that measure slower:

| shape | measured |
|---|---|
| `k_submul_1` with the carry through three dependent operations per limb, and the multiply-subtract over all `dn` limbs | 1.10 to 1.26 ns per limb; the kernel 12% to 23% slower at 9 to 129 limbs |
| complementing the remainder to reuse `_multadd` | no faster, slower below 33 limbs |
| `k_submul_1` unrolled by two | no faster |
| `k_mul_1` into a buffer, then `k_sub_n` | 1.3 to 1.5 ns per limb |
| a branch-free mask form of the 3-by-2 correction | no change |

## The recursive division

`k_div_dc_bal` divides `2n` limbs by `n`: the high `ceil(n/2)` quotient limbs, then the low ones over
the remainder so far. `k_div_dc_step` gets `c` quotient limbs (`1 <= c < dn`) from a `dn`-limb divisor
`b`: it divides the top `2c` limbs of the dividend by `bh`, the top `c` limbs of `b`, then subtracts that
quotient times the low `l = dn - c` limbs of `b` with one `k_mul`. With `A` the dividend of the step
(below `b B^c`):

- The estimate is never low: `floor(A / (bh B^l)) >= floor(A / b)`, since `b >= bh B^l`.
- It is at most 2 high: `A / (bh B^l) - A / b < (A / b) / bh < B^c / (B^c / 2) = 2`, since `A < b B^c` and
  `bh >= B^c / 2`.
- When the top `c` limbs of the dividend equal `bh`, the quotient is clamped to `B^c - 1`, which is still
  at least the true one and below the estimate, and the top limbs plus `bh` carry into `a[dn]`.
- The remainder lies in `[-2b, b)`. `a[dn]` is its sign word: the borrow modulo `B`, nonzero exactly
  while the remainder is negative, so the loop adds `b` back at most twice and lowers the quotient by
  one each time.
- `bh` keeps the top limb of `b`, so every nested divisor is normalized: no divisor is ever padded or
  shifted, and any size splits.

`k_div_dc` cuts a long quotient into blocks of `dn` limbs from the top, the partial block of
`m mod dn` limbs first; each block's top `dn` limbs are the previous block's remainder.
`k_div_dc_bal_scratch_size` takes only the high step's size: the low step has the same product and a
smaller recursion, and the recursion term is the larger one for many `(dn, c)` pairs.

The cutoffs come from a sweep on an Apple M4 Pro, in us per division:

| dividend : divisor (limbs) | schoolbook | 8 | 16 | 32 | 64 | 128 |
|---|---:|---:|---:|---:|---:|---:|
| 1024 : 513 | 162.7 | 75.7 | 73.9 | 75.4 | 78.4 | 88.2 |
| 2048 : 1025 | 635.4 | 235.6 | 234.5 | 235.1 | 240.5 | 258.2 |
| 2048 : 256 | 287.0 | 171.7 | 166.3 | 172.1 | 182.4 | 211.7 |
| 3000 : 1000 | 1209.7 | 465.9 | 465.7 | 475.0 | 506.2 | 582.6 |

Balanced cutoffs from 8 to 32 limbs are within 2% of each other; `div_dc_threshold` is 2048 bits (32
limbs at 64-bit limbs). A step cutoff of a quarter of that (8 limbs) is 15% to 25% faster than a higher
one on short quotients over tall divisors (a 10-limb quotient by 1000 limbs: 6.03 us against 7.09 with
a step cutoff of 24; 20 limbs: 10.29 against 13.15), because a step's product of 8 limbs or more runs on
the two-row multiplication basecase, which beats the schoolbook division's loop. The products in this
sweep run on Karatsuba and the earlier schoolbook step; the cutoffs are not swept against Toom-3,
Toom-4, the transform or the current step.

## The Newton division

The bounds of the reciprocal and of a block are in ARCHITECTURE.md and the header comments of
`k_div_newton_inv` and `k_div_newton_block`. Three of their choices carry a reason that is easy to undo:

- `W_h' = W_h - 4`. The reciprocal `W_h` of the top `h` limbs exceeds `B^(n+h) / t` by less than 4, so
  `W_h - 4` is at most the exact value and `E = B^(n+h) - t W_h'` is never negative: the step forms `E` as
  the negation of the low `n + 1` limbs of `t W_h'` with no comparison and no correction loop.
- For a power-of-two divisor `W_h'` is all ones, and the add of the correction into it carries out of its
  top limb. The carry is real; it is not an overflow.
- The block estimate `q = A + floor(A X / B^c)` stays below `B^c` because `A`, the top `c` limbs of the
  dividend, is at most `t`, the top `c` limbs of the divisor, and `W` is below `B^(2c) / t` (below
  `2 B^c` when `t = B^c / 2`): `A W < B^(2c)`. `A < B^c` alone gives only `A W < 2 B^(2c)`.

The dispatch keeps three tiers. `bench/tune_div.cc` times the recursive division over the Newton
division forced to the same shape (above 1 the Newton division is faster):

| quotient / divisor | divisor limbs and ratio |
|---|---|
| 1 (balanced) | 32: 0.87, 256: 0.75, 1024: 0.78, 2048: 0.89, 4096: 1.05, 8192: 1.17, 16384: 1.27 |
| 8 | 32: 0.91, 512: 0.87, 1280: 0.92, 1536: 1.13, 2048: 1.27, 4096: 1.55 |
| 0.5 | 2048: 0.77, 8192: 0.89, 16384: 0.99 |
| 0.25 | 4096: 0.60, 8192: 0.61, 16384: 0.70 |

The recursive division is 10% to 27% faster on balanced divisions from 32 to about 3,000 limbs, 6% to
17% faster with a quotient eight times the divisor up to about 1,300 limbs, and 30% to 40% faster on
short quotients at every size measured. Hence the Newton division from 1400 limbs of divisor and a
quotient of 5000 limbs and at least half the divisor's. The worst choice near the edges costs 5%: a
4096-limb quotient over 4096 limbs takes the recursion and loses 5%; 4096 over 2048 takes it and wins
8%. The sweep comes from a busy machine, with no quiet rerun, and nothing is swept at 32-bit
limbs. The reciprocal base `div_newton_inv_base` (48) is not swept: the reciprocal is under 5% of a
division from 1400 limbs.

On an Apple M1 Pro the Newton division takes `a / b` with a 32768-limb `a` and a 16385-limb `b` (the
`div n/2` row of `bench/bench.cc`) from 14.48 ms to 10.5 to 10.8 ms, and with a 131072-limb `a` from
81.6 to 89.1 ms to 50.8 to 52.9 ms, against the recursive division alone.

Its scratch is about 5 times the dividend for a balanced division (the recursion's about 3): 655,364
limbs for 131,072 by 65,536, and 50,964 for 10,000 by 5,000.

## The quotient-only kernel's cutoff

The truncated kernel against the recursive division on random operands (`tune_divq`, Intel Core
i9-9980HK; sizes are the divisor's limbs): with a quotient as long as the divisor it is 27% to 34%
faster from 16 to 128 limbs, 5% at 512, 1% at 576 and 4% slower at 640; with a quotient half as long,
40% faster at 1024 limbs, 20% at 2048 and even at 3072; with a quotient twice as long, 10% to 19% faster
at 8 to 64 limbs, even at 128 and 14% slower at 256. A 4-limb quotient by a 2048-limb divisor takes 1.3
us against 11.7, a 16-limb one 1.5 against 29 and a 64-limb one 4.2 against 92.

An exact multiple always flags and costs about one schoolbook division, so the cutoff trades the two:

| `UINTEGER_T_DIV_TRUNC_THRESHOLD` | random operands | exact multiples |
|---:|---|---|
| 64 (the default; dividends up to about 130 limbs) | 26% to 37% faster | 0 to 15% slower |
| 192 (dividends up to about 390 limbs) | 27% to 37% faster | up to 42% slower |
| 512 | faster everywhere | up to 86% slower at 1024 limbs |

Redoing the whole division with the exact kernel on a flag, instead of repairing the state, costs exact
multiples 64% to 94% more than the recursive division (273 against 146 ns for a 16-limb dividend, 2259
against 1377 at 64), where the repair costs 7% to 13% up to 64 limbs (165 and 1477 ns).

The flag bound needs a quotient under `B` limbs, and it is tight: a base-4 model with the guard off gives
a wrong unflagged digit at `qn = B + 2`, `dn = 10`.

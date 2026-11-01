# Division-free base conversion

`digits()` and `from_digits()` convert between limbs and digits in every base from 3 to 255 that
is not a power of two (the power-of-two bases slice bits and need none of this). ARCHITECTURE.md
describes the kernels. This note gives what a maintainer needs before changing one: why the
encoder multiplies instead of dividing, how far it sits from the multiplier's limit, the error
bound that makes every chunk exact, and the designs that measure slower.

The kernel timings below come from prototypes of the kernels on an Apple M4 Pro (Apple clang 17,
`-O2`, one core, medians of 7 interleaved trials), limbs in and digit values out, with no string
and no alphabet. The prototypes unroll to 8 limbs; the library unrolls to `UINTEGER_T_RADIX_UNROLL`
(6 by default) and is otherwise the same code.

## Why the encoder does not divide

`D` is the largest power of the base that fits a limb (10^19 in base 10, 58^10 in base 58), and a
chunk is the `k` digits of a value below `D`. Dividing the value by `D` once per chunk, limb by
limb, with a 2-by-1 step and a precomputed reciprocal (Moller and Granlund) is a chain: each step
needs the remainder of the step before (multiply-high, add, multiply, subtract, two corrections),
about 14 cycles a step. A pass runs at the latency of that chain however wide the core is.

Multiplications that do not depend on each other overlap. On the M4 Pro a dependent `umulh` or
`madd` chain costs about 4 cycles a step, while independent multiplies issue two or more per cycle
and full 64-by-64-to-128-bit products about one per cycle. Bouvier and Zimmermann's Algorithm 2
holds `x / D^K` as a fixed-point fraction of `L` limbs and multiplies it by `D` once per chunk:
every limb of a pass is an independent product, the carry out of the pass is the next chunk, most
significant first, and the lowest limb is dropped. Their one division, the initial `x / D^K`, is a
product here with `R = floor(B^(L+n) / D^K)` (`B = 2^digit_bits`, `n` the limbs of `x`), built once
per base and size and cached. `R` takes no long division: it is `K` single-limb divisions of
`B^(L+n)` by `D` (`divmod_small`), since `floor(floor(a / b) / c) = floor(a / (b c))`. The encoder
then has no division at all.

## The distance to the multiplier

A 32-byte base58 key has `n = 4` limbs and `K = 5` chunks (44 digits). It takes about 30 limb
products (14 in the short product `x R`, 15 in the passes) and about 80 multiplies to split the
chunks into digits: some 140 multiplier operations, about 60 cycles or 13 ns on the M4 Pro, with a
critical path about as long. The unrolled kernel takes 27.6 ns, twice that; the rest is digit
extraction, the strip of leading zeros and the call.

At 1 KiB the fused passes run at about one 64-by-64 product per cycle and the short product at
about 1.5. The short product takes four rows per sweep, row `r` lagged by `r` limbs so that all
four add into the same limb, the shape of the fused passes. In base 58 it takes 3.36 us at 1 KiB
where two rows a sweep take 6.41, and 250 ns at 256 bytes where two rows take 396.

## The error bound

The kernel starts from

    Y0 = floor((x R + floor(R / 2)) / B^n)

so `Y0 / B^L` is `(x + 1/2) / D^K` less a truncation error. Every chunk is exact while the error
accumulated so far stays below 1/2 in units of `x` (Bouvier and Zimmermann's invariant: the fraction
never crosses a chunk boundary). With `r = D / B`:

- the floors of `R` and `R / 2` and the short product's skipped partial products cost at most
  `(n + 2) r^K`;
- pass `m` drops the lowest limb of the fraction, which costs at most `r^m`, for `m` from 1 to `K - 1`;
- a guard limb (`L = K + 1` where it is `K` otherwise) divides the sum by `B`.

`_make_radix_table` evaluates `(n + 2) r^K + r + r^2 + ... + r^(K-1)` in `double` and takes a guard
limb when it reaches 0.45, which leaves room for the rounding of the `double` sum. One guard limb
always suffices: `r < 1`, so the sum is below `n + K + 1`, and `B` divides it to far below 1/2. The
guard limb follows from `r`:

| base | `D` | `r` | guard limb |
|---:|---|---:|---|
| 3 | 3^40 | 0.66 | always |
| 5 | 5^27 | 0.40 | always |
| 10 | 10^19 | 0.54 | always |
| 13 | 13^17 | 0.47 | always |
| 36 | 36^12 | 0.26 | one- and two-chunk values |
| 58 | 58^10 | 0.023 | never |
| 255 | 255^8 | 0.97 | always |

`R` has `L` limbs, or `L + 1` when `D^K < B^n`. `test/property.cc` (`mutant_failures`) builds tables
without the guard limb in bases with `r >= 0.5` and without the 1/2 offset in five bases, and each
must fail on the edge values next to `b^j`, `D^j` and `2^(limbs bits) - 1`; the prototypes got
37,691 wrong encodes without the guard and nearly every input wrong without the offset.

## The tables

Up to `radix_unrolled` limbs, and when `K - n <= 2`, the table is keyed by the value's bit length,
so `K` is as small as the value allows: a 25-byte key takes 4 chunks where its limb count would
give 5. That is 17% to 19% at 20 and 25 bytes. Above, one table per limb count serves every value of
that many limbs.

The unrolled kernel is a template on `(n, L)`, the guard limb and the length of `R`, four variants per
`(n, K - n)` (`_enc_fixed_k`), so the fraction stays in registers. Unrolling is worth 1.4 times at 32
to 64 bytes: the general kernel takes 40.4 ns at 32 bytes against 27.6 unrolled. Taking the guard
limb and the length of `R` at run time would halve the instantiations, and the unrolled loop would
then carry a run-time bound.

## Splitting a chunk

A chunk of `k` digits is cut into pieces of at most `h` digits, `b^h < 2^32`, by a multiply-and-shift
division (Granlund and Montgomery). At 64-bit limbs `k <= 2h + 1`, so a chunk is at most three
pieces; at 32-bit limbs `k <= h` and a chunk is one piece. A piece of `m` digits gives its digits from
the fraction `f = v ceil(2^64 / b^m)`, one `umulh(f, b)` per digit (Lemire, Kaser and Kurz), or two at
a time through a pair table of `b^2` entries for bases up to 64 (`2 b^2` bytes: 6.7 KB in base 58, 8
KiB in base 64). The pair table is worth 15% to 19%: 27.6 ns against 35.2 at 32 bytes, 51.3 against
64.7 at 64. Base 10 is the slow base at small sizes: it takes the guard limb, and its 19-digit chunk
is three pieces.

The 17 chunk shapes `(k, h)` of the bases 3 to 255 at 64-bit limbs each have a splitter with the
counts as constants (`_split_chunk_kh`). The general splitter, which reads `k` and `h` at run time,
costs its control flow, not its multiplies: the fixed shapes take a base58 encode of 32 bytes from
163.7 to 142.5 ns end to end on an Intel Core i9-9980HK (the kernel alone 0.76 times), and 8% off on
the M4 Pro.

## Decoding

Decoding has no division to remove. A chunk of `k` digit bytes becomes a limb by SWAR multiplies
(`_swar8`: eight bytes in three multiplies, pairs into 16-bit lanes, then 32, then 64), and the
chunks fold into the value by Horner's rule, four per step with `D^4` as a four-limb constant. That
is 1.3 to 2.6 times as fast as building each chunk with a multiply-add per digit and folding the
chunks in one at a time. The SWAR lanes stay below `b^2`, `b^4` and
`b^8` only when every digit is below the base, and the tree's carry-free combine (`low < P`) holds
only for valid digits: `from_digits` checks every digit before any kernel runs, and a new entry point
into the kernels has to check them first.

Two chunks per step measure within 1% of four at 32 bytes or less (14.5 against 14.4 ns) and slower
above (33.9 against 29.6 ns at 64 bytes, 276.9 against 223.3 at 256), so the decoder takes single
chunks until the rest is a multiple of four, then four at a time.

## The tree

Above 8192 bits both directions go through the tree in ARCHITECTURE.md; these are the sweeps behind
its constants (8 variants, 5 bases, 9 sizes, at 64- and 32-bit limbs, on the M4 Pro):

- Encoding, the quadratic kernel would win to about 320 limbs at 64-bit limbs (192 limbs: 11.4 us
  against 12.7 for the tree), but each limb count needs its own table built from `K` single-limb
  divisions. At 32-bit limbs the kernel and the tree tie at 128 to 192 limbs (in 64-bit-limb units)
  and the tree wins by 16% at 256.
- Decoding, Horner and the tree tie at 128 limbs (3.6 us) and the tree wins from 192 (7.5 against
  7.9 us).
- Encode leaves of 3072, 6144 and 7680 bits and decode leaves of 4096, 8192 and 12288 bits are within
  3% of each other in every base at both widths.
- The powers cost 13.7 us of a 225.6 us encode and 13.5 of a 124.5 us decode at 1024 limbs, 120 us of
  2135 and of 1107 at 4096. A cache would save at most that, and its key is `(base, T, J)`, one entry
  per distinct size, so it needs an eviction policy to bound its memory.

## Designs that measure slower

Base 58 encoding, kernel ns on the M4 Pro:

| design | 16 B | 32 B | 64 B | 1 KiB | why it loses |
|---|---:|---:|---:|---:|---|
| repeated division by `D`, 2-by-1 steps | 30.3 | 76.9 | 195.6 | 36.87 us | one dependent chain per pass |
| the same with the fixed-point digit split | 41.5 | 74.7 | 175.4 | 36.69 us | the hardware divide is fast; the split pays once the divisions are gone |
| six division passes pipelined (pass `j` divides the limb pass `j - 1` finished a step before) | 41.0 | 69.5 | 138.6 | 10.11 us | small values have too few limbs to fill the pipeline |
| two-limb chunks, 3-by-2 steps by `D^2` | 47.6 | 80.2 | 175.8 | 23.66 us | a 3-by-2 step is as long as two 2-by-1 steps |
| carry-deferred table (Firedancer's base58 scheme, any base and length) | 28.8 | 56.3 | 115.1 | - | its serial carry pass (11 steps at 32 bytes) is longer than the fused passes; ties at 128 to 256 bytes |
| the fraction, one pass per sweep | 33.3 | 59.2 | 114.1 | 19.25 us | |
| the fraction, two passes per sweep | 30.7 | 50.7 | 96.4 | 17.28 us | |
| the fraction, four fused passes and the lagged short product | 32.3 | 40.4 | 70.3 | 5.94 us | the general kernel; it serves 9 limbs and up |
| unrolled, no pair table | 29.0 | 35.2 | 64.7 | 8.99 us | |
| GMP 6.3.0 `mpn_get_str` (for comparison) | 27.5 | 63.1 | 155.4 | 7.78 us | |
| **unrolled, bit-length `K`, pair table; the general kernel above 8 limbs** | **19.8** | **27.6** | **51.3** | **5.94 us** | |

The pipelined division needs no table per base. From 25 bytes up it is 1.1 to 3.7 times as fast as
the repeated division, and slower below; nothing uses it.

## Literature

- C. Bouvier and P. Zimmermann, "Division-Free Binary-to-Decimal Conversion", IEEE Trans. Computers
  63(8), 2014: Algorithm 2 (the fraction, its `+1/2` offset and its error analysis) and Algorithm 3
  (the scaled remainder tree). They report up to 55% over GMP 5.1.2 at 20 to 28 words in base 10 and
  a quadratic crossover near 240 words, in line with the 320 limbs above.
- D. E. Knuth, *TAOCP* vol. 2, 4.4: the fraction method.
- R. P. Brent and P. Zimmermann, *Modern Computer Arithmetic*, 2010, 1.7: the trees.
- D. J. Bernstein, "Scaled remainder trees", 2004.
- N. Moller and T. Granlund, "Improved division by invariant integers", IEEE Trans. Computers 60(2),
  2011: the 2-by-1 step and the reciprocal behind `divmod_small`.
- T. Granlund and P. Montgomery, "Division by invariant integers using multiplication", PLDI 1994:
  the cut of a chunk into pieces.
- D. Lemire, O. Kaser and N. Kurz, "Faster remainder by direct computation", Software: Practice and
  Experience 49(6), 2019: digits from a fixed-point fraction.
- The GMP manual, 15.6.1 and 15.6.2 (documentation only): a one-limb fraction for digit extraction,
  and a note that a multi-limb `r / b^n` scheme gave no noticeable improvement there.
- simdjson's eight-digit parse: the SWAR chunk, here for any base up to 256.
- Firedancer's `fd_base58` (Apache-2.0) and libbase58 (MIT): comparators only; nothing is taken from
  their code.

Every ingredient is published or folklore. What this code adds is the cached reciprocal per base and
bit length in place of Bouvier and Zimmermann's initial division, which makes their quadratic
algorithm division-free end to end; no published implementation of that step turned up.

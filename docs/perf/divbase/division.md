# Division 2.3-2.7x behind GMP at 16-256 limbs: where it goes, and the changes

Branch `explore/divbase` (worktree `wt/explore-divbase`, from master 30b787c).
Host for every number: Intel i9-9980HK (x86-64, Apple clang 17, `-O2`), load average 4-5 on 16 threads,
best of 5 rounds per cell, work in `~/.scratch/divx` there. M-series not measured (the local Mac is
loaded). Harnesses in the branch: `bench/divmb.cc` (library call vs kernel vs GMP), `bench/submulmb.cc`
(submul variants), `bench/sbmb.cc` (schoolbook variants, identity-checked against `k_div_sb`).

## Where the time goes (master, `div n/2`: L-limb dividend, (L/2+1)-limb divisor)

| L | `a / d` | `mpz_tdiv_q` | kernel `k_div_dc` (no alloc, no normalization) | `mpn_tdiv_qr` (quotient and remainder) |
|---|---|---|---|---|
| 16 | 178 ns | 109 | 154 | 157 |
| 32 | 699 | 248 | 472 | 368 |
| 64 | 1645 | 632 | 1443 | 977 |
| 256 | 14770 | 6307 | 14522 | 9052 |

Three separate gaps, in the order they show up:

1. **Allocation and zero-fill around the kernel** (library minus kernel): 24 ns at 16 limbs,
   **227 ns at 32**, 200 at 64, 250 at 256. `knuth_divmod` kept 64 limbs on the stack and above that
   built a zero-filled `std::vector` (every call at 32 and 64 limbs), then `fill_n` the scratch, which the
   kernels do not read before writing (`test/dc_kernel.cc` runs them on pattern-filled scratch).
2. **Kernel speed**: `k_submul_1` costs 1.12-1.26 ns per limb against GMP's `mpn_submul_1` 0.63-0.88
   (and `k_addmul_1` 0.85-0.93): the carry word goes through three dependent operations per limb, the
   same shape as the multiplication fix. The 3-by-2 step is about 7 ns (24 cycles) per quotient digit, so
   at 16 limbs a digit is 7 ns of 3-by-2 plus 9 limbs of submul. `k_div_sb` also subtracted all `dn` limbs per digit,
   though the 3-by-2 remainder already holds the top two. The kernel gap to `mpn_tdiv_qr` is 1.0x at 16-32 limbs, 1.5x at 64-256
   (multiplication speed in the recursion).
3. **`a / b` needs only the quotient; GMP's `mpz_tdiv_q` takes about 0.69x the time of its own
   `mpn_tdiv_qr`** (109 vs 157 at 16 limbs, 248 vs 368, 632 vs 977, 6307 vs 9052). The cause is inferred from timings, not
   from GMP's code (not read): a quotient-only division can skip part of the products (see below).

At 16 limbs gaps 1 and 2 are most of the 1.6-1.8x; at 64-256 limbs gap 3 and the multiplication speed
dominate.

## Candidate 1: three changes, committed

- `k_submul_1`: one carry word through the chain; `r[i] - lo` and the high limb are off it (the
  `_multadd` rule: the carry takes the last operation). Kernel 1.10-1.26 -> 0.96-1.03 ns per limb
  (-12%); the variant that complements `r` and reuses `_multadd` (`s5`) was no better and worse below 33 limbs.
- `k_div_sb`: after the 3-by-2 step, subtract the product over `dn-2` limbs only; the borrow comes off the
  two-limb remainder `(r1, r0)`, which becomes `w[dn-2], w[dn-1]`; a borrow out of it adds the divisor back
  (the estimate is at most one too large, as before). The `n2 == d1 && n1 == d0` case keeps the old full
  path. Together with the new submul: -19% at 9 limbs, -23% at 17 and 33, -12% at 65, -14% at 129
  (kernel, `sbmb`). A branch-free mask form of the 3-by-2 correction changed nothing (not inspected;
  presumably the compiler already emits a conditional move).
- `knuth_divmod`: one `k_scratch` (stack up to 256 limbs, heap above, uninitialized) instead of the
  64-limb stack array plus zero-filled vector; no `fill_n` of the scratch. Library minus kernel
  227 -> 99 ns at 32 limbs, 200 -> 131 at 64, 25 ns at 16 (remaining: the quotient's own allocation,
  trim, normalization copies).

### A/B in the library (`bench` `div n/2`, us, master vs candidate, three interleaved pairs)

A/A is the three master runs.

| limbs | master (A/A spread) | candidate | change | GMP |
|---|---|---|---|---|
| 4 | 0.063 0.064 0.063 (1.6%) | 0.062 0.063 0.061 | -2% | 0.033 |
| 16 | 0.170 0.170 0.169 (0.6%) | 0.139 0.139 0.144 | -17.5% | 0.100 |
| 64 | 1.486 1.475 1.485 (0.8%) | 1.225 1.243 1.238 | -16.5% | 0.58 |
| 256 | 13.87 13.61 14.01 (2.9%) | 13.18 13.19 13.03 | -5.5% | 6.1 |

Against GMP: 16 limbs 1.7x -> 1.4x, 64 limbs 2.55x -> 2.1x, 256 limbs 2.3x -> 2.15x. Every row outside
the A/A spread except 4 limbs (within it).

### Identity

- Kernel: `sbmb` compares `k_div_sb` with each variant on 20000 trials per run (random, all-ones, mixed
  zero/ones, equal top limbs, `dn` 2-21, `m` 1-12): 0 mismatches in quotient and remainder.
- Local (arm64, 64-bit limbs): `property`, `property32`, `property16` (16-bit limbs, `DIV_DC_THRESHOLD=4`),
  `property_dc`, `property_dc32`, `property_portable`, `dc_kernel` (default, small threshold, 32-bit,
  portable), `test`, `fuzz_gmp 3000`: all pass. `recip16` (3-by-2 over 2^31 divisors, 16-bit build) not
  run: `k_div_3by2` is unchanged.
- Intel (x86-64): `fuzz_gmp 3000` against GMP 6.3.0, `property`, `dc_kernel` (default and
  `DIV_DC_THRESHOLD=4`): pass.

## Lead not finished: quotient-only schoolbook (the big one)

`a / b` discards the remainder, so the product of quotient digit `i` need only reach the divisor limbs
that can still change a later 3-by-2 estimate. With `i` digits left, subtract over `min(dn-2, i+G)` limbs
(the low part of the window is left stale) and the work is about `qn^2/2` limb products instead of
`qn * dn`: half, for the `n/2` shape. Prototype `div_sb_T<G>` in `bench/sbmb.cc` (no safety check yet), kernel
time against the candidate's `k_div_sb`, ns per call:

| dn (qn = dn-1) | candidate `k_div_sb` | quotient-only G=2 | G=4 |
|---|---|---|---|
| 9 | 127 | 114 (-11%) | 121 |
| 17 | 388 | 279 (-28%) | 306 |
| 33 | 1271 | 795 (-37%) | 866 |
| 65 | 4392 | 2521 (-43%) | 2710 |
| 129 | 17036 | 8545 (-50%) | 8931 |

Quotients equal the exact ones in 0 of 20000 random trials per size at G=2 and G=4. At dn = 129 the
prototype (8.5 us) is already ahead of the recursive kernel (14.1 us in the library) and about 1.35x
`mpz_tdiv_q` (6.3 us); at dn = 65 it is 2.5 us against the recursive 4.4. So below a threshold well above
today's 32 limbs, the truncated schoolbook beats the recursion for the quotient alone; a quotient-only
recursive form would be the next step above it.

Not done, and why it needs the performance engineer: correctness. The truncation makes each digit's
3-by-2 input inexact by under one unit at limb `dn-2` (errors from the skipped limbs add to less than
`qn * B^(dn-3)`). The digit is then right unless the two-limb remainder `(r1, r0)` is within that error of
0 or of `(d1, d0)`; probability about `qn / 2^64` per digit. The safe version flags those digits
(`r1 == 0 && r0 < qn + 2`, or `(r1, r0) >= (d1, d0) - (qn + 2)`, and the
`n2 == d1 && n1 == d0` case) and, on any flag, redoes the whole division with the exact `k_div_sb` on a
saved copy of the dividend. That needs a written bound, an adversarial test that forces the flagged cases
(remainder at 0, at `d - 1`, equal top limbs), and the same for the 32- and 16-bit limb builds.

## Other results

- Mask-form 3-by-2 correction: no change. Unrolled-by-two submul: no better than the single chain.
  Temporary-buffer submul (`k_mul_1` then `k_sub_n`): 1.3-1.5 ns per limb, worse.
- The recursion threshold (32 limbs) was not re-tuned: with the quotient-only prototype the crossover
  moves above 129 limbs, so tuning it before that lands would be wasted; the full-division crossover with
  the faster schoolbook was not measured.
- M-series not measured. x86-64 clang only; gcc not checked.

## Verdict

Finish candidate 1 (small, identity-checked, -17% at 16-64 limbs, no downside seen). Hand the quotient-only
schoolbook to the performance engineer for the guard bound and the fallback; it is worth 28-50% on the
kernel at 17-129 limbs, which is more than everything else here together.

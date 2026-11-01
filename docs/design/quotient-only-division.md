# Quotient-only schoolbook division

`a / b` and `a /= b` need the quotient alone. For a short quotient they run `k_div_sb_trunc` in
`uinteger_t.hh`: a schoolbook division that subtracts only the top part of each quotient digit's
product, flags the digits it cannot prove right, and repairs its state when it flags. This document
proves that the quotient it returns is exact, in the code's own names, so a maintainer can check
each step against `k_div_sb_trunc`, `k_div_sb_digit`, `k_div_uses_trunc` and `knuth_divmod`.

## Notation

- `B = 2^digit_bits` is the limb base. The proof needs `B >= 4`, which every limb width meets.
- `D` is the divisor `dp[0..dn)`, normalized (top bit set), `dn >= 2`, so `B^dn / 2 <= D < B^dn`.
  `D' = floor(D / B^(dn-2))` is its top two limbs, `(d1, d0)`.
- `N` is the dividend `np[0..nn)` and `qn = nn - dn`. Its top `dn` limbs are below `D`, so
  `N < B^qn D` and the quotient has `qn` limbs, the digits `q_0` (lowest) to `q_(qn-1)`.
- `G = div_trunc_window = 2` and `x = max(dn - 2 - G, 0)` (`exact` in the code). Digits `i >= x` are
  *exact*: they subtract their whole product. Digits `i < x` are *truncated*.
- A truncated digit `m` leaves out the low `k_m = dn - 2 - G - m >= 1` divisor limbs (`k0` in the
  main loop, `k` in the repair), so `m + k_m = x`. `D_lo(m) = D mod B^(k_m)` and
  `D_hi(m) = D - D_lo(m)`. An exact digit has `D_hi = D`.
- The true division: `Q_i = floor(N / (B^i D))` (the digits from `i` up, as one number),
  `T_i = floor(N / B^i) - Q_i D` in `[0, D)` (the remainder after digit `i`), and
  `V_i = N - Q_i D B^i = T_i B^i + (N mod B^i)`, which `k_div_sb` holds in `np` after digit `i`.
  Digit `i` divides the window `W_t = floor(V_(i+1) / B^i) = T_(i+1) B + n_i`, with `n_i` limb `i`
  of `N`, and `q_i = floor(W_t / D)`.
- The kernel: `C_i` is the value of `np[0..i+dn)` after digit `i`, and `C_qn = N`. Digit `i` reads
  the window `W_c = floor(C_(i+1) / B^i)`, the limbs `w[0..dn]` with `w = np + i`, and
  `delta = W_c - W_t` is its error.

## The kernel

`k_div_sb_trunc(q, np, nn, dp, dn, at)` takes `k_div_sb`'s contract and returns a status.

1. If `qn > div_trunc_max_quotient` (`B - 1`), it runs `k_div_sb` and returns 0.
2. Digits `qn - 1` down to `x` run `k_div_sb_digit`, the exact digit step: the 3-by-2 estimate from
   `(w[dn], w[dn-1], w[dn-2])` and `(d1, d0)` with the reciprocal `dinv` (or the largest digit when
   the top two limbs equal `D'`), the product over the low `dn - 2` limbs, at most one add-back, and
   `w[dn] = 0`.
3. Each truncated digit `i`, from `min(qn, x) - 1` down to 0:
   - Flag 1: if `w[dn] == d1 && w[dn-1] == d0`, it stops with `lo = i + 1`.
   - `qd = k_div_3by2(w[dn], w[dn-1], w[dn-2], ...)` is the estimate `q^ = floor(W' / D')`, with `W'`
     the window's top three limbs, and `(r1, r0)` is `rho = W' - q^ D'`.
   - With `len = i + G` and `k0 = dn - 2 - len = k_i`, `cy = k_submul_1(w + k0, dp + k0, len, qd)`
     subtracts the product from `w[k0..dn-2)`, and `(r1, r0) - cy` goes to `(w[dn-1], w[dn-2])` with
     the borrow `bor`. Together they subtract `q^ D_hi(i)` from the window. `w[0..k0)` is not
     touched and `w[dn]` is not written.
   - If `bor`, the digit is `q^ - 1`, and `k_add_n` adds `dp[k0..dn)` to `w[k0..dn)`, carry dropped.
   - Otherwise, flag 2: if `w[dn-1]` and `w[dn-2]` are both zero, it stores `q[i] = q^` and stops
     with `lo = i`.
   - It stores the digit in `q[i]`.
4. With no flag it returns 0. The quotient is in `q`; `np[0..dn)` is not the remainder.
5. The repair (Lemma 5). With `top = lo + dn`, for each truncated digit `m` from `lo` up it subtracts
   `q[m] * dp[0..k)` from `np[m..m+k)` (`k_submul_1`), the borrow limb from `np[m+k]`, and ripples the
   borrow up to `top` (`k_sub_1`). `wrap` collects the borrows out of `top`. At flag 2 with `wrap` set
   it lowers `q[lo]` and adds `dp` to `np[lo..top)`, carry dropped.
6. It returns 3 if `wrap` is set at flag 1 or `np[lo..top)` is not below `D`. Otherwise digits
   `lo - 1` down to 0 run `k_div_sb_digit` and it returns the flag: the quotient is in `q` and the
   remainder in `np[0..dn)`. `at`, when given, receives the flagged digit `i`.

An exact digit `i` writes `np[i..i+dn]`, at or above `x`. A truncated digit `i` writes from
`np[i + k_i] = np[x]` up, and its 3-by-2 step reads from `np[i+dn-2]` up. So before the repair no
digit writes below `x`: the limbs a truncated digit leaves out still hold the dividend's, and no later
digit reads them. A truncated digit takes `i + 2` limb products where `k_div_sb_digit` takes `dn - 2`,
about `qn^2 / 2` in all where `k_div_sb` takes `qn (dn - 2)`.

## Lemma 1: the 3-by-2 estimate

Let `W` be a window of `dn + 1` limbs whose top two limbs `(n2, n1)` are below `D'`, `W'` its top
three limbs and `q^ = floor(W' / D')`. Then `q^ <= B - 1`, `floor(W / D) <= q^ <= floor(W / D) + 1`,
and `W > q^ D - B^(dn-1)`.

*Proof.* `W' < D' B` gives `q^ <= B - 1`. Let `q = floor(W / D)`. From
`q D' B^(dn-2) <= q D <= W < (W' + 1) B^(dn-2)` follows `q D' <= W'`, so `q <= q^`. Since
`D' B^(dn-2) > D - B^(dn-2)`,
`W >= q^ D' B^(dn-2) >= q^ D - q^ B^(dn-2) >= q^ D - (B - 1) B^(dn-2) > q^ D - B^(dn-1)`. With
`D >= B^dn / 2 >= B^(dn-1)` that gives `W > (q^ - 1) D`, so `q >= q^ - 1`.

`k_div_3by2` returns this `q^` and `rho = W' - q^ D'` in `[0, D')` (Moller and Granlund, "Improved
division by invariant integers", 2011). It requires `(n2, n1) < D'`.

## Lemma 2: the error of the skipped limbs

Suppose every digit above `i` is the true one and every step above left its window in `[0, B^dn)`
(Lemma 4). Then

```text
C_(i+1) = N - sum over m > i of q_m D_hi(m) B^m = V_(i+1) + e
e       = sum over truncated m > i of q_m D_lo(m) B^m >= 0
```

Each term of `e` is at most `(B - 1)(B^(k_m) - 1) B^m < B^(k_m + m + 1) = B^(x+1)`, so with `c`
truncated digits above `i`, `e < c B^(x+1)`.

`delta = floor((V_(i+1) + e) / B^i) - floor(V_(i+1) / B^i)` is 0 when `c = 0`, and otherwise
`0 <= delta <= floor(e / B^i) + 1 <= c B^X`, with `X = x + 1 - i = dn - 1 - G - i`. The digit's own
product leaves out `u = q^ D_lo(i) <= (B - 1)(B^(k_i) - 1) < B^X`. Hence
`u + delta <= (c + 1) B^X - 1`, which is below `B^(dn-2)` when `c + 1 <= B^(dn-2-X) = B^(G-1+i)`.
With `G = 2` the right side is at least `B`, and `c <= qn - 1`, so a quotient of at most `B` limbs
gives, at every truncated digit,

```text
u + delta < B^(dn-2)        (the bound)
```

The kernel runs only quotients of at most `B - 1` limbs, one inside it.

## Lemma 3: the decision

Assume Lemma 2's hypothesis, the bound, and `(n2, n1) < D'` for `W_c`. Let `Y = W_c - q^ D_hi(i)`,
the window after the subtraction, read as a signed integer. Its top two limbs are `rho - cy` and the
limbs below are in `[0, B^(dn-2))`, so `Y < 0` exactly when `bor` is set, and a nonnegative `Y` is
below `B^(dn-2)` exactly when `w[dn-1]` and `w[dn-2]` are both zero. The kernel records
`q^ - [Y < 0]`, and `W_c - q^ D = Y - u`.

First, in every case `W_t > (q^ - 1) D`. Lemma 1 gives `W_c > q^ D - B^(dn-1)`, so
`W_t = W_c - delta > (q^ - 1) D + (D - B^(dn-1) - delta)`, and the bracket is positive because
`D >= B^dn / 2`, `delta < B^(dn-2)` and `B >= 4`. With `W_t <= W_c < (q^ + 1) D` (Lemma 1), the
true digit `q_i` is `q^` or `q^ - 1`. Then:

- `Y >= B^(dn-2)`: `W_c - q^ D >= B^(dn-2) - u > delta`, so `W_t > q^ D` and `q_i = q^`, the digit
  recorded.
- `Y < 0`: `W_t <= W_c < q^ D`, so `q_i = q^ - 1`, the digit recorded.
- `0 <= Y < B^(dn-2)`: flag 2. The digit `q^` is wrong only if `W_t < q^ D`, that is
  `Y < u + delta`, and the bound puts that whole range inside this case.

A digit that does not flag is the true one.

## Lemma 4: the state and the next window

With the digit recorded right and no flag, the step leaves in `w[0..dn)`:

- without a borrow, `Y`, whose top two limbs are `rho - cy <= rho < D'`, so
  `B^(dn-2) <= Y < D' B^(dn-2)`;
- with a borrow, `Y + D_hi(i) = W_c - (q^ - 1) D_hi(i)`. It is below `D_hi(i)` because `Y < 0`, and
  positive because `W_c > (q^ - 1) D >= (q^ - 1) D_hi(i)` (Lemma 1; `q^ >= 1`, since `q^ = 0` leaves
  `Y = W_c >= 0`). The add-back's carry out is the wrap of `Y`, so dropping it is exact.

Both lie in `[0, B^dn)`, so `C_i = C_(i+1) - q_i D_hi(i) B^i = V_i + e + q_i D_lo(i) B^i`: Lemma 2's
form for digit `i - 1`, with one more term. The next window's top two limbs are this result's top
two: below `D'` without a borrow, at most `D'` with one (the result is below `D_hi(i)`, whose top two
limbs are `D'`). Equality is flag 1, so every 3-by-2 step gets `(n2, n1) < D'`. The first truncated
digit's top limbs are at most `D'` too: it follows exact digits, which leave a remainder below `D`, or
it is the top of `N`, whose top `dn` limbs are below `D`.

## Lemma 5: the repair

Let the kernel flag at digit `i`, with `lo` the lowest digit applied (`i + 1` at flag 1, `i` at
flag 2) and `top = lo + dn`. By Lemmas 3 and 4 every digit above `i` is the true one, the limbs
`np[0..top)` hold `C = N - sum over m >= lo of q[m] D_hi(m) B^m`, and at flag 2 `q[i] = q^` with `q_i`
equal to `q^` or `q^ - 1` (Lemma 3). So `C = V + e`, where

```text
V = N - sum over m >= lo of q[m] D B^m
e = sum over truncated m >= lo of q[m] D_lo(m) B^m
```

The loop subtracts each term of `e` at its position: `q[m] * dp[0..k)` is `q[m] D_lo(m)`, its borrow
limb comes off `np[m+k]`, and the borrow ripples up to `top`. Each step is a subtraction modulo
`B^top`. Every value on the way lies between `V` and `C`, and `-B^top < V` and `C < B^top` (below),
so a step borrows out of `top` exactly when it takes the value from nonnegative to negative, which
happens at most once. The limbs end as `V mod B^top`, and `wrap` is set exactly when `V < 0`.

- Flag 1: `V = V_lo = T_lo B^lo + (N mod B^lo)`, in `[0, B^top)`. There is no wrap, and
  `np[lo..top)` is `T_lo`.
- Flag 2: `V = R B^lo + (N mod B^lo)` with `R = W_t - q^ D`, in `(-D, D)` by Lemma 3, so
  `V > -D B^lo > -B^top`. If `R >= 0` there is no wrap, `q_i = q^`, and `np[lo..top)` is `R = T_lo`.
  If `R < 0` there is one wrap, `q_i = q^ - 1`, and `np[lo..top)` is `R + B^dn`; the kernel lowers
  `q[lo]` and adds `D`, and with the carry out dropped the limbs are `R + D = T_lo`.

Either way the digits `q[lo..qn)` are the true ones, `np[lo..top)` is `T_lo < D`, and `np[0..lo)` is
`N mod B^lo`: the state `k_div_sb` holds after digit `lo`. The truncated digits' unwritten `w[dn]`
limbs lie at or above `top`, where the exact steps that follow read nothing. Those steps produce
digits `lo - 1` to 0 and the remainder. The two checks that return 3 (a wrap at flag 1, `np[lo..top)`
not below `D`) test this lemma's conclusions.

## Theorem

On `k_div_sb`'s contract:

- Status 0: `q` holds the exact quotient. Past the bound, or with no truncated digit (`dn <= 4`),
  `k_div_sb_digit` computes every digit. Otherwise, by induction from the top digit, the exact
  digits read exact windows (`e = 0`) and each truncated digit is the true one by Lemmas 1 to 4.
- Status 1 or 2: `q` holds the exact quotient and `np[0..dn)` the exact remainder, by Lemma 5 and
  `k_div_sb`'s own argument for the digits below `lo`.
- Status 3 does not occur.

## The window and the quotient bound

At the last digit (`i = 0`) the bound needs `c + 1 <= B^(G-1)`. With `G = 1` that allows no
truncated digit above the last one, so a quotient with two truncated digits can record a wrong digit
without a flag; `test/divq_kernel.cc` fails on a kernel built with `G = 1`. With `G = 2` the bound
holds for `qn <= B`. A wider window costs one more limb product per digit and only raises the bound
(`B^2` limbs at `G = 3`).

`div_trunc_max_quotient` is `B - 1`, written `std::numeric_limits<digit>::max()` because at 16-bit
limbs `~digit(0)` promotes to the `int` -1. No quotient reaches it at 32- and 64-bit limbs. At 16-bit
limbs it is 65535 limbs, beyond what the dispatch sends at the default threshold, so only a direct
call to the kernel or a raised `UINTEGER_T_DIV_TRUNC_THRESHOLD` meets it.
`UINTEGER_T_DIV_TRUNC_MAX_QUOTIENT` lowers it for a test.

## The fallback

On status 3, `knuth_divmod` normalizes the dividend again from `lhs` (the `load` lambda), takes the
scratch `k_div_dc` needs, and runs `k_div_dc`. This relies on `lhs` staying intact until the outputs
are written, which holds even when the quotient is `lhs` itself (`a /= b`). The proof excludes status
3 and `test/divq_kernel.cc` requires it never to occur, so no test runs this path.

## Exact multiples and the cost of a flag

On random operands each flag is a two-limb equality, about one chance in `B^2` per truncated digit.
An exact multiple (`N = Q D`) with a truncated digit, inside the bound, always flags. Every remainder
`T_i` from the quotient's lowest nonzero digit down is zero. At a truncated digit with `T_i = 0`,
`W_t = q_i D`, so `(q^ - 1) D < W_t` (Lemma 3) gives `q^ = q_i` and `Y = u + delta`, in
`[0, B^(dn-2))`: flag 2, unless flag 1 stops the kernel at that digit first. So the kernel flags, at
the latest, at the first truncated digit at or below the quotient's lowest nonzero digit.

A flagged division does the limb products of `k_div_sb`. A truncated digit `m` multiplies `m + 2`
limbs in the main loop and `k_m = dn - 4 - m` in the repair, `dn - 2` in all, as `k_div_sb_digit`
does, and the digits below `lo` run `k_div_sb_digit`. So a flagged division costs about one schoolbook
division, wherever it flags.

## The dispatch

`knuth_divmod` takes the kernel when the caller wants no remainder (`a / b` and `a /= b`, through
`divide` with a null remainder) and `k_div_uses_trunc(m, dn)` holds, with `m = nn - dn` the dividend's
limbs beyond the divisor's and `T = div_trunc_threshold` (64 limbs; `UINTEGER_T_DIV_TRUNC_THRESHOLD`):

| quotient | takes the kernel when |
|---|---|
| `m <= dn / 2` | `m < 2 T` |
| `dn / 2 < m <= dn` | `m < T` |
| `dn < m <= 2 dn` | `m < T / 2` |
| `m > 2 dn` | never |

The wrapper adds a limb to the dividend for normalization, so the kernel's `qn` is `m + 1`. A
single-limb divisor or a dividend below the divisor never reaches `knuth_divmod`.

The kernel's work, about `qn^2 / 2` limb products, stops growing with the divisor once
`dn > qn + 2`, while the recursive division multiplies by the whole divisor. A quotient longer than
the divisor truncates only its last `dn - 4` digits. An exact multiple costs one schoolbook division:
the exact path's own cost below `div_dc_threshold` divisor limbs (32 at 64-bit limbs), and more than
the recursive division above it. So exact multiples set `T`: up to the cutoff an exact multiple costs
0 to 15% more than the same division without the kernel, and random operands 8 to 36% less
(`bench/tune_divq.cc` in `n` and `xn` modes, dividends of 16 to 128 limbs by divisors of half their
length plus one, on an Intel Core i9-9980HK). On random operands alone the kernel wins up to several
times `T`.

## Tests

`test/divq_kernel.cc` opens up `private` and, for every case:

- divides with `k_div_sb` and checks `Q D + R = N` and `R < D`;
- runs `k_div_sb_trunc` and requires that the status is never 3, that the quotient equals
  `k_div_sb`'s with or without a flag, that after a flag `np[0..dn)` equals `k_div_sb`'s remainder
  and `at` is a truncated digit, that a quotient past `div_trunc_max_quotient` returns 0, and that an
  exact multiple with a truncated digit, inside the bound, flags;
- divides through `divide` quotient-only, through `divide` with a remainder, and with `/=`, and
  compares each with `k_div_sb`'s result.

Over a run it requires both flags, each at the first, a middle and the last truncated digit, and, at
16-bit limbs or with a lowered bound, a quotient past the bound. The divisors are random, all ones,
all ones below random top two limbs, zero below them, the smallest top limbs `(B/2, 0)` over random
limbs and over all ones, and random limbs of mixed bit lengths. The dividends are `Q D + T` for `T`
of 0, 1, small, `D - 1`, `D - s` (`s` of 1 to `dn - 2` limbs), `D - B^j` and random; `B^qn D - 1`,
whose remainder is `D - 1` after every digit; a remainder of 0, 1, `D - 1` or `D - 2` after each
digit in turn; and random dividends. The shapes are `dn` 2 to 12 by `qn` 1 to 14, random shapes (up
to 41 divisor and 50 quotient limbs with the default arguments), size classes (`dn` 16, 33, 64, 130
and 257 by `qn` 1, 4, `dn / 2`, `dn` and `2 dn`), and at 16-bit limbs exact multiples with quotients
of `B - 1`, `B` and `B + 1` limbs. CTest builds it at 64-, 32- and 16-bit limbs, on the portable
primitives (`UINTEGER_T_NO_INT128`), with `DIV_DC_THRESHOLD=4` at 64- and 32-bit limbs, and with
`UINTEGER_T_DIV_TRUNC_MAX_QUOTIENT=6`.

`test/divq_model.cc` checks the proof's lemmas on an independent model of the algorithm at base 4, which
does not include the header: every small case of `dn = 5` and `qn = 2`, and adversarial dividends at
`dn = 7`, give the exact quotient and remainder without the repair's checks failing. A window of one limb
does not, and neither does a quotient of `B + 2` limbs without the guard (a wrong digit no flag catches),
so the bound on the quotient is needed.

`test/property.cc` also runs with `UINTEGER_T_DIV_TRUNC_THRESHOLD=100000` at 64- and 32-bit limbs,
where it checks `a / b` against `divmod` with the kernel taking every quotient-only division up to
twice the divisor's length. `test/fuzz_gmp.cc` (not in CTest) checks `/` and `/=` against GMP's
`mpz_tdiv_q` on dividends built as `Q D + T`.

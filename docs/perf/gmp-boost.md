# uinteger_t against GMP and Boost.Multiprecision: measurements

Measured 2026-11-01 at `uinteger_t:7b600e8`. The [README](../../README.md#-performance) shows three
of the sizes below; the raw runs are in [`bench/results/`](../../bench/results/).

## Method

- Bench: [`bench/bench.cc`](../../bench/bench.cc), run through [`bench/run.sh`](../../bench/run.sh)
  ([BENCHMARKING.md](../BENCHMARKING.md) describes both). Operands are random 64-bit limbs from a
  fixed seed with a nonzero top limb.
- Rows: `a + b`, `a * b`, `a * a`, `a / b` (quotient only, `b` of half the limbs of `a` plus one),
  `a / b` with `b` an eighth of `a`, `a.str(10)`, `uinteger_t(s, 10)` (parsing the decimal string of
  `a`) and `a.str(16)`. The `bits` column is the size of `a` (and of `b` in the sum and the
  products).
- Timing: the best of 5 rounds, each repeating the call for at least 20 ms; a table cell is the
  minimum over two runs. The spread of uinteger_t's two runs (maximum minus minimum over the
  minimum) has a median of 0.7% on the Apple M4 Pro and 3.0% on the Intel Core i9-9980HK. It is
  largest where a time is a few nanoseconds, since the output has a resolution of 0.001 µs (a 9 ns
  row cannot show less than 11%): up to 11% on the M4 Pro and 14% on the i9, which also shows 10%
  to 12% on the products and the eighth-divisor division of 1,048,576 bits. Other benchmark jobs
  on each machine were held off for the run; `host.txt` in each results directory records the load
  averages before and after (1-minute averages of 8.0 and 8.4 on the M4 Pro, 4.2 and 7.9 on the
  i9).
- Result placement: GMP writes into an `mpz_t` that exists and `mpz_get_str` into an existing
  buffer, while uinteger_t's operators and Boost's return a new value, so their time includes
  allocating the result. A uinteger_t result of up to 8 limbs (512 bits) lives inside the object;
  a longer one is one heap allocation. `uinteger_t::mul` and `uinteger_t::divmod` write into
  existing values like GMP; `bench.cc` does not time them.

## Comparators

| | version | what is called | where it comes from |
|---|---|---|---|
| uinteger_t | `7b600e8` | operators, `str()`, `uinteger_t(s, 10)` | this repository |
| [GMP](https://gmplib.org/) | 6.3.0 | `mpz_add`, `mpz_mul`, `mpz_tdiv_q`, `mpz_get_str`, `mpz_set_str` | Homebrew |
| [Boost.Multiprecision](https://github.com/boostorg/multiprecision/tree/boost-1.92.0) | 1.92.0 (tag `boost-1.92.0`) | `cpp_int` operators, `str()`, the string constructor, and `operator<<` with `std::hex` for `a.str(16)` | Homebrew |

All three are built into one translation unit with the same compiler and flags. GMP and Boost are
the ones `bench/run.sh` finds (`-p PREFIX`, the compiler's own paths, then Homebrew's prefix), not
pinned: a pin would mean a Boost source tree (about 180 MB of headers) and a GMP build on every
machine. The versions are recorded in the first lines of every run file.

Before it times a size, `bench.cc` computes each operation with GMP and with Boost on the same
operands and exits with an error unless the result equals uinteger_t's (lowercase hex, or the
decimal text for `a.str(10)`; the operands and the decimal round trip too). Each run file ends with
`# 159 results compared, all equal`.

No other comparator is in the tables. GMP and Boost.Multiprecision are the two arbitrary-precision
libraries a C++ program chooses between; `unsigned __int128` ends at 128 bits, below this library's
range, and a hand-written fixed-width loop is the fixed width that the
[README](../../README.md#-when-to-use-it) already sends readers to.

## Machines and commands

| | Apple M4 Pro | Intel Core i9-9980HK |
|---|---|---|
| OS | macOS 26, arm64 | macOS 15, x86-64 |
| compiler | Apple clang 17.0.0 (clang-1700.3.19.1) | Apple clang 17.0.0 (clang-1700.6.4.2) |
| flags | `-std=c++17 -O2` | `-std=c++17 -O2` |
| prefix of GMP and Boost | `/opt/homebrew` | `/usr/local` |

```sh
bench/run.sh -p /opt/homebrew -n 2 -o out/m4 2 4 8 16 64 256 1024 4096 16384    # on the M4 Pro
bench/run.sh -p /usr/local -n 2 -o out/i9 2 4 8 16 64 256 1024 4096 16384       # on the i9
python3 bench/summarize.py --table out/m4
python3 bench/summarize.py --table out/i9
```

## Apple M4 Pro

Time per call; the ratio columns divide uinteger_t's time by GMP's or Boost's, so below 1
uinteger_t is faster. For the eighth-divisor rows `bits` is the size of the dividend.

| operation | bits | uinteger_t, µs | GMP, µs | Boost, µs | × GMP's time | × Boost's time |
|---|---:|---:|---:|---:|---:|---:|
| `a + b` | 128 | 0.004 | 0.003 | 0.003 | 1.33 | 1.33 |
|  | 256 | 0.004 | 0.003 | 0.017 | 1.33 | 0.24 |
|  | 512 | 0.004 | 0.004 | 0.018 | 1.00 | 0.22 |
|  | 1,024 | 0.019 | 0.005 | 0.043 | 3.80 | 0.44 |
|  | 4,096 | 0.037 | 0.013 | 0.061 | 2.85 | 0.61 |
|  | 16,384 | 0.094 | 0.060 | 0.264 | 1.57 | 0.36 |
|  | 65,536 | 0.377 | 0.261 | 0.822 | 1.44 | 0.46 |
|  | 262,144 | 1.57 | 1.06 | 3.22 | 1.48 | 0.49 |
|  | 1,048,576 | 5.96 | 4.27 | 14.9 | 1.40 | 0.40 |
| `a * b` | 128 | 0.009 | 0.007 | 0.020 | 1.29 | 0.45 |
|  | 256 | 0.014 | 0.011 | 0.026 | 1.27 | 0.54 |
|  | 512 | 0.049 | 0.026 | 0.049 | 1.88 | 1.00 |
|  | 1,024 | 0.102 | 0.081 | 0.182 | 1.26 | 0.56 |
|  | 4,096 | 0.998 | 0.805 | 2.29 | 1.24 | 0.44 |
|  | 16,384 | 10.2 | 8.06 | 22.9 | 1.27 | 0.45 |
|  | 65,536 | 85.6 | 58.3 | 218 | 1.47 | 0.39 |
|  | 262,144 | 448 | 322 | 1,998 | 1.39 | 0.22 |
|  | 1,048,576 | 2,382 | 1,542 | 18,143 | 1.55 | 0.13 |
| `a * a` | 128 | 0.010 | 0.006 | 0.020 | 1.67 | 0.50 |
|  | 256 | 0.014 | 0.010 | 0.026 | 1.40 | 0.54 |
|  | 512 | 0.036 | 0.021 | 0.048 | 1.71 | 0.75 |
|  | 1,024 | 0.081 | 0.067 | 0.185 | 1.21 | 0.44 |
|  | 4,096 | 0.724 | 0.691 | 2.29 | 1.05 | 0.32 |
|  | 16,384 | 7.08 | 6.60 | 23.0 | 1.07 | 0.31 |
|  | 65,536 | 61.2 | 45.5 | 217 | 1.34 | 0.28 |
|  | 262,144 | 326 | 234 | 1,989 | 1.40 | 0.16 |
|  | 1,048,576 | 1,728 | 1,165 | 18,107 | 1.48 | 0.095 |
| `a / b`, b half of a | 128 | 0.002 | 0.009 | 0.006 | 0.22 | 0.33 |
|  | 256 | 0.020 | 0.020 | 0.061 | 1.00 | 0.33 |
|  | 512 | 0.044 | 0.028 | 0.117 | 1.57 | 0.38 |
|  | 1,024 | 0.113 | 0.059 | 0.253 | 1.92 | 0.45 |
|  | 4,096 | 0.822 | 0.529 | 2.59 | 1.55 | 0.32 |
|  | 16,384 | 7.07 | 4.34 | 46.3 | 1.63 | 0.15 |
|  | 65,536 | 62.6 | 39.1 | 652 | 1.60 | 0.096 |
|  | 262,144 | 547 | 285 | 10,427 | 1.92 | 0.052 |
|  | 1,048,576 | 3,510 | 1,514 | 168,979 | 2.32 | 0.021 |
| `a / b`, b an eighth of a | 131,072 | 143 | 114 | 3,859 | 1.25 | 0.037 |
|  | 262,144 | 434 | 254 | 14,462 | 1.71 | 0.030 |
|  | 1,048,576 | 3,631 | 1,768 | 233,927 | 2.05 | 0.016 |
| `a.str(10)` | 128 | 0.045 | 0.047 | 0.091 | 0.96 | 0.49 |
|  | 256 | 0.071 | 0.099 | 0.232 | 0.72 | 0.31 |
|  | 512 | 0.133 | 0.235 | 0.559 | 0.57 | 0.24 |
|  | 1,024 | 0.275 | 0.613 | 1.74 | 0.45 | 0.16 |
|  | 4,096 | 1.85 | 3.22 | 23.0 | 0.57 | 0.080 |
|  | 16,384 | 23.2 | 20.3 | 418 | 1.14 | 0.056 |
|  | 65,536 | 198 | 156 | 7,483 | 1.27 | 0.026 |
|  | 262,144 | 1,806 | 1,212 | 120,195 | 1.49 | 0.015 |
|  | 1,048,576 | 15,038 | 8,303 | 1,975,209 | 1.81 | 0.008 |
| `uinteger_t(s, 10)` | 128 | 0.054 | 0.040 | 0.033 | 1.35 | 1.64 |
|  | 256 | 0.105 | 0.079 | 0.075 | 1.33 | 1.40 |
|  | 512 | 0.206 | 0.163 | 0.138 | 1.26 | 1.49 |
|  | 1,024 | 0.446 | 0.332 | 0.298 | 1.34 | 1.50 |
|  | 4,096 | 2.25 | 1.67 | 1.78 | 1.35 | 1.27 |
|  | 16,384 | 15.9 | 11.1 | 20.4 | 1.44 | 0.78 |
|  | 65,536 | 121 | 77.6 | 301 | 1.56 | 0.40 |
|  | 262,144 | 919 | 566 | 4,735 | 1.62 | 0.19 |
|  | 1,048,576 | 6,681 | 3,674 | 75,598 | 1.82 | 0.088 |
| `a.str(16)` | 128 | 0.039 | 0.023 | 0.244 | 1.70 | 0.16 |
|  | 256 | 0.058 | 0.041 | 0.430 | 1.41 | 0.13 |
|  | 512 | 0.096 | 0.082 | 0.841 | 1.17 | 0.11 |
|  | 1,024 | 0.175 | 0.168 | 1.93 | 1.04 | 0.090 |
|  | 4,096 | 0.617 | 0.595 | 16.2 | 1.04 | 0.038 |
|  | 16,384 | 2.37 | 2.29 | 223 | 1.04 | 0.011 |
|  | 65,536 | 9.42 | 9.02 | 3,161 | 1.04 | 0.003 |
|  | 262,144 | 37.6 | 36.2 | 49,122 | 1.04 | <0.001 |
|  | 1,048,576 | 151 | 144 | 784,037 | 1.05 | <0.001 |

## Intel Core i9-9980HK

| operation | bits | uinteger_t, µs | GMP, µs | Boost, µs | × GMP's time | × Boost's time |
|---|---:|---:|---:|---:|---:|---:|
| `a + b` | 128 | 0.007 | 0.007 | 0.010 | 1.00 | 0.70 |
|  | 256 | 0.008 | 0.007 | 0.053 | 1.14 | 0.15 |
|  | 512 | 0.010 | 0.008 | 0.056 | 1.25 | 0.18 |
|  | 1,024 | 0.056 | 0.011 | 0.179 | 5.09 | 0.31 |
|  | 4,096 | 0.139 | 0.028 | 0.146 | 4.96 | 0.95 |
|  | 16,384 | 0.157 | 0.098 | 0.263 | 1.60 | 0.60 |
|  | 65,536 | 0.480 | 0.386 | 0.501 | 1.24 | 0.96 |
|  | 262,144 | 2.02 | 1.65 | 1.84 | 1.23 | 1.10 |
|  | 1,048,576 | 9.33 | 8.71 | 12.4 | 1.07 | 0.75 |
| `a * b` | 128 | 0.023 | 0.009 | 0.061 | 2.56 | 0.38 |
|  | 256 | 0.034 | 0.022 | 0.077 | 1.55 | 0.44 |
|  | 512 | 0.120 | 0.048 | 0.135 | 2.50 | 0.89 |
|  | 1,024 | 0.293 | 0.148 | 0.342 | 1.98 | 0.86 |
|  | 4,096 | 2.37 | 1.62 | 3.59 | 1.46 | 0.66 |
|  | 16,384 | 22.6 | 14.1 | 32.5 | 1.60 | 0.69 |
|  | 65,536 | 176 | 96.2 | 307 | 1.83 | 0.57 |
|  | 262,144 | 1,086 | 652 | 2,702 | 1.66 | 0.40 |
|  | 1,048,576 | 5,451 | 3,208 | 24,457 | 1.70 | 0.22 |
| `a * a` | 128 | 0.023 | 0.009 | 0.059 | 2.56 | 0.39 |
|  | 256 | 0.035 | 0.016 | 0.075 | 2.19 | 0.47 |
|  | 512 | 0.111 | 0.037 | 0.133 | 3.00 | 0.83 |
|  | 1,024 | 0.215 | 0.092 | 0.330 | 2.34 | 0.65 |
|  | 4,096 | 1.61 | 1.00 | 3.60 | 1.61 | 0.45 |
|  | 16,384 | 15.5 | 9.20 | 32.7 | 1.69 | 0.48 |
|  | 65,536 | 127 | 64.6 | 304 | 1.96 | 0.42 |
|  | 262,144 | 808 | 453 | 2,677 | 1.78 | 0.30 |
|  | 1,048,576 | 4,042 | 2,129 | 24,256 | 1.90 | 0.17 |
| `a / b`, b half of a | 128 | 0.004 | 0.016 | 0.016 | 0.25 | 0.25 |
|  | 256 | 0.060 | 0.045 | 0.204 | 1.33 | 0.29 |
|  | 512 | 0.086 | 0.056 | 0.379 | 1.54 | 0.23 |
|  | 1,024 | 0.127 | 0.096 | 0.643 | 1.32 | 0.20 |
|  | 4,096 | 0.767 | 0.614 | 3.27 | 1.25 | 0.23 |
|  | 16,384 | 13.4 | 6.66 | 33.4 | 2.00 | 0.40 |
|  | 65,536 | 128 | 63.6 | 406 | 2.01 | 0.31 |
|  | 262,144 | 1,102 | 526 | 6,155 | 2.09 | 0.18 |
|  | 1,048,576 | 8,269 | 3,236 | 106,725 | 2.56 | 0.077 |
| `a / b`, b an eighth of a | 131,072 | 286 | 196 | 2,012 | 1.46 | 0.14 |
|  | 262,144 | 910 | 476 | 7,521 | 1.91 | 0.12 |
|  | 1,048,576 | 8,612 | 3,306 | 126,198 | 2.60 | 0.068 |
| `a.str(10)` | 128 | 0.117 | 0.092 | 0.311 | 1.27 | 0.38 |
|  | 256 | 0.175 | 0.187 | 0.835 | 0.94 | 0.21 |
|  | 512 | 0.318 | 0.427 | 1.95 | 0.74 | 0.16 |
|  | 1,024 | 0.698 | 1.04 | 5.66 | 0.67 | 0.12 |
|  | 4,096 | 4.24 | 5.57 | 69.8 | 0.76 | 0.061 |
|  | 16,384 | 45.1 | 35.0 | 969 | 1.29 | 0.047 |
|  | 65,536 | 404 | 262 | 14,573 | 1.54 | 0.028 |
|  | 262,144 | 3,750 | 2,205 | 235,373 | 1.70 | 0.016 |
|  | 1,048,576 | 31,827 | 15,272 | 3,891,337 | 2.08 | 0.008 |
| `uinteger_t(s, 10)` | 128 | 0.134 | 0.079 | 0.081 | 1.70 | 1.65 |
|  | 256 | 0.241 | 0.153 | 0.189 | 1.58 | 1.28 |
|  | 512 | 0.438 | 0.292 | 0.321 | 1.50 | 1.36 |
|  | 1,024 | 1.04 | 0.597 | 0.702 | 1.75 | 1.49 |
|  | 4,096 | 4.67 | 3.53 | 3.93 | 1.32 | 1.19 |
|  | 16,384 | 34.4 | 20.5 | 35.3 | 1.68 | 0.98 |
|  | 65,536 | 266 | 140 | 448 | 1.90 | 0.59 |
|  | 262,144 | 1,980 | 1,030 | 6,791 | 1.92 | 0.29 |
|  | 1,048,576 | 15,551 | 6,985 | 106,632 | 2.23 | 0.15 |
| `a.str(16)` | 128 | 0.105 | 0.056 | 0.542 | 1.88 | 0.19 |
|  | 256 | 0.152 | 0.096 | 1.05 | 1.58 | 0.14 |
|  | 512 | 0.239 | 0.190 | 2.13 | 1.26 | 0.11 |
|  | 1,024 | 0.469 | 0.350 | 5.21 | 1.34 | 0.090 |
|  | 4,096 | 1.46 | 1.34 | 49.0 | 1.09 | 0.030 |
|  | 16,384 | 5.47 | 5.20 | 683 | 1.05 | 0.008 |
|  | 65,536 | 21.8 | 20.8 | 10,152 | 1.05 | 0.002 |
|  | 262,144 | 87.1 | 84.5 | 160,268 | 1.03 | <0.001 |
|  | 1,048,576 | 359 | 337 | 2,588,766 | 1.07 | <0.001 |

## Where it loses

- **GMP.** uinteger_t is faster only at decimal printing from 256 to 4,096 bits on both machines
  (0.45 to 0.94 times GMP's time; also at 128 bits on the M4 Pro, 0.96) and at the 128-bit
  division, whose quotient has at most one limb.
  Elsewhere it takes 1.0 to 3.0 times GMP's time, except the sums of 1,024 and 4,096 bits (2.9 to
  5.1 times), where allocating the result costs more than the addition: a result up to 512 bits
  has no allocation, which is why the 256-bit sum is 1.1 to 1.3 times
  ([multiplication.md](../design/multiplication.md) measures it). [OPEN-WORK.md](../OPEN-WORK.md)
  lists the known gaps.
- **Boost.** `cpp_int` parses decimal faster up to 4,096 bits: uinteger_t takes 1.3 to 1.6 times its
  time on the M4 Pro and 1.2 to 1.7 times on the i9, from 128 to 4,096 bits. Besides that,
  uinteger_t is level or ahead on every row except the 128-bit sum on the M4 Pro (0.004 against
  0.003 µs, one unit of the output's resolution) and the 262,144-bit sum on the i9 (1.10).
- **Hex printing** (`a.str(16)`) takes 1.0 to 1.9 times GMP's time. Boost's `operator<<` with
  `std::hex` is quadratic in the size: it takes 5,200 to 7,200 times uinteger_t's time at a million
  bits.

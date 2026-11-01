# Benchmarking

How the timings in the README are produced, how to reproduce them, how to tell whether a change
made the library faster, and how to retune the algorithm cutoffs.

| file | what it does |
|---|---|
| `bench/bench.cc` | checks that GMP and Boost.Multiprecision return uinteger_t's results, then times the three, one row per limb count and operation |
| `bench/run.sh` | builds `bench.cc` at `-O2`, runs it several times, keeps each run's output |
| `bench/ab.sh` | builds `bench.cc` against two versions of `uinteger_t.hh` and runs them interleaved |
| `bench/summarize.py` | reads their output: the best time per row, A against B, the README table, the table of every row for `docs/perf/` |
| `bench/tune.cc`, `bench/tune_div.cc`, `bench/tune_divq.cc` | sweeps that find the algorithm cutoffs |
| `bench/results/` | the raw output behind the README and `docs/perf/` tables, one directory per machine and date |

The scripts are POSIX `sh` and Python 3 (standard library only), and run on macOS and Linux.
`bench/common.sh` holds what `run.sh` and `ab.sh` share.

## What `bench.cc` measures

```sh
c++ -std=c++17 -O2 -I. bench/bench.cc [-DHAVE_GMP -lgmp] [-DHAVE_BOOST] -o bench
./bench [limbs...] [ops...]
```

Sizes are counts of 64-bit limbs (64 bits each, so 4 limbs are 256 bits). The ops are `add`, `mul`,
`sqr`, `div`, `str10`, `parse10` and `str16`; the default is the sizes 2, 4, 16, 64, 256 and 1024
and every op. For each size the operands are random (a fixed seed) with a nonzero top limb:

| row | what is timed |
|---|---|
| `add`, `mul`, `sqr` | `a + b`, `a * b`, `a * a` for two `L`-limb operands |
| `div n/2` | `a / d` for an `L`-limb `a` and a divisor of `L / 2 + 1` limbs |
| `div n/8` | `n / d` for a divisor an eighth of the dividend: 2048 limbs, and each requested size of at least 4096 limbs |
| `str10`, `str16` | `a.str(10)`, `a.str(16)` |
| `parse10` | `uinteger_t(s, 10)` for the decimal string of `a` |

Each row prints microseconds per call to three decimals, for uinteger_t, GMP and Boost's
`cpp_int` (`-` where a library is not built in). A time is the best of 5 rounds; a round repeats
the call until it has run for at least 20 ms.

Before it times a size, `bench.cc` computes every requested operation with GMP and with Boost on
the same operands and compares the result with uinteger_t's (lowercase hex; decimal text for
`str10`; the operands themselves and the decimal round trip too). A difference prints the library,
operation and size to standard error and exits with status 1, so a table cannot come from
libraries that disagree. A run's output starts with `# GMP x.y.z` and `# Boost x.y.z`, the
versions it was built against, and ends with `# N results compared, all equal`; `summarize.py`
skips lines that start with `#`.

The columns do the same work but differ in where the result goes. GMP writes into an `mpz_t` that
already exists (and `mpz_get_str` into a buffer that already exists), while uinteger_t's
operators and Boost's return a new value, so their time includes allocating the result. A
uinteger_t result of up to 8 limbs (512 bits) lives inside the object; a longer one is one heap
allocation. `uinteger_t::mul` and `uinteger_t::divmod` write into existing values like GMP does;
`bench.cc` does not time them.

## Reproducing the README table

The README table is the best of two runs of `bench.cc` on each of two machines, at 4, 64 and 16384
limbs (256; 4,096 and 1,048,576 bits), rows `add`, `mul`, `div n/2`, `str10` and `parse10`;
[docs/perf/gmp-boost.md](perf/gmp-boost.md) has every size and operation. It needs GMP and Boost
headers; `-p` names the prefix that holds `include/` and `lib/` (without it the compiler's own
paths and Homebrew's prefix are tried; `-G` and `-B` leave GMP or Boost out). The libraries are
the ones found, not pinned: each run's output starts with their versions, and `host.txt` records
where they were found.

```sh
bench/run.sh -p /opt/homebrew -n 2 -o out/m4 2 4 8 16 64 256 1024 4096 16384
bench/run.sh -p /usr/local -n 2 -o out/i9 2 4 8 16 64 256 1024 4096 16384    # on the other machine
python3 bench/summarize.py --readme 'M4 Pro=out/m4' 'i9=out/i9'
python3 bench/summarize.py --table out/m4
```

`run.sh` writes `out/m4/run_1.txt`, `run_2.txt` (`-n` sets the number of runs) and `host.txt`,
which records the CPU, compiler, flags, library prefixes, header commit and the load before and
after. `summarize.py --readme` prints the README's table, one value per machine in each cell
(`LABEL=DIR` labels a machine), taking the minimum of each cell over the runs; `--table` prints
every size and operation of one machine, as in `docs/perf/`.

Without `--readme` and `--table`, `summarize.py out/m4` prints every row: the best time of
uinteger_t, GMP and Boost, uinteger_t's time over each of theirs (below 1, uinteger_t is faster)
and the spread of uinteger_t's runs. `--sizes 4,64`, `--ops add,div` and `--stat median` select
rows and the statistic.

Other compilers, library versions and CPUs give other absolute times; the ratios to GMP and Boost
are the part that carries over.

`bench/results/` holds the runs behind the tables, one directory per machine and date:
[`2026-11-01-apple-m4-pro`](../bench/results/2026-11-01-apple-m4-pro/) and
[`2026-11-01-intel-i9-9980hk`](../bench/results/2026-11-01-intel-i9-9980hk/) for the README and
`docs/perf/`; the `2026-10-31` directories hold the runs on an Apple M1 Pro and the i9 that
the design notes cite. Each has the unedited `bench` output, the machine, compiler, library
versions and commit, and the `summarize.py` command that prints the table from the files. To record
a measurement, copy the `run.sh` output directory there (its `host.txt` carries the machine and
the commit) and add a `README.md` with the versions and the command; remove host names and paths
first.

## Telling whether a change is faster

A change is measured against `master` with an A/A run first and an interleaved A/B run second, on
a quiet machine.

**A quiet machine.** Nothing else heavy runs while the benchmark does. On a shared host, take
the machine's exclusive hold if it has one; `nice -n 19` keeps the benchmark from slowing others
but does not quiet them. `host.txt` records the load average before and after.

**A/A first.** The noise of a machine is what the same build shows against itself:

```sh
bench/ab.sh -a -r 3 -o out/aa master 2 4 8 16 64 256 1024 add mul sqr
```

The two sides are one binary, run alternately three times (A B A B A B). The `spread` columns and
the largest `|B/A - 1|` are the noise of those rows on that machine, and on a quiet machine every
verdict is blank. A `faster` or `slower` row means the machine is noisier than the spreads of
three rounds show: rerun with more rounds (`-r`) or on a quieter machine. Take the largest
`|B/A - 1|` of the rows being compared as the floor for the A/B run that follows.

**Then A/B.** The change interleaved against `master`:

```sh
bench/ab.sh -r 3 -o out/ab master HEAD 2 4 8 16 64 256 1024 add mul sqr
```

A and B are each a git ref of this repository (its `uinteger_t.hh` is taken with `git show`), a
directory holding `uinteger_t.hh`, or a header file; `HEAD` against `.` compares a commit with
the working tree. Both sides are built with the same `bench.cc`, compiler and flags, and they
alternate, so drift in the machine's speed reaches both. `-g -p PREFIX` links GMP too: its column
runs the same code on both sides, which is a second reading of the noise. To compare two
machines or builds instead, run `run.sh` for each and use `summarize.py --ab out/one out/two`;
those runs are not interleaved.

**Sizes.** Measure at the sizes users hit and at the ones the change touches: below and above
the inline capacity (8 limbs), around each cutoff the change moves, and a large size.

**Reading the result.** `summarize.py --ab` prints, for each row, A and B (the minimum over the
rounds, or the median with `--stat median`), the spread of each side, `B/A` (below 1, B is
faster) and a verdict. The spread of a side is `(max - min) / min` over its rounds. A row is
`faster` or `slower` when `B/A` is off 1 by more than the larger of the two spreads, the
threshold (`--threshold`, 3% by default) and the resolution of the printed times: `bench.cc`
prints 0.001 microseconds, so a row of 0.005 microseconds cannot show less than 20%.

**When a change counts.** A change or tier stays only if it is faster beyond the spread, which
means about 3% or more on the rows it targets; 10% counts without argument. A difference inside
the A/A spread is noise. Report the minimum (or median) per row together with the A/A spread.

## Retuning the cutoffs

Each algorithm cutoff is a `static constexpr` of the class with a macro that overrides it. The
tune tools take the same macros, so a sweep can force an algorithm on or off. They print
nanoseconds per call (the best of several rounds), and each is run on a quiet machine like the
benchmark.

| cutoff (default, in limbs) | macro | sweep |
|---|---|---|
| `mul_karatsuba_threshold` (24) | `UINTEGER_T_MUL_KARATSUBA_THRESHOLD` | `./tune 8 400 8`: `mul base` against `mul kara` |
| `mul_toom3_threshold` (140) | `UINTEGER_T_MUL_TOOM3_THRESHOLD` | `./tune 8 400 8`: `mul kara` against `mul toom3` |
| `mul_toom4_threshold` (400) | `UINTEGER_T_MUL_TOOM4_THRESHOLD` | `./tune -t 300 400 600`: `mul toom3` against `mul toom4` |
| `mul_ntt_threshold` (1400) | `UINTEGER_T_MUL_NTT_THRESHOLD` | `./tune -t 1000 1400 2000`: `mul toom4` against `mul ntt` |
| `sqr_karatsuba_threshold` (40) | `UINTEGER_T_SQR_KARATSUBA_THRESHOLD` | `./tune 8 400 8`: `sqr base` against `sqr kara` |
| `sqr_toom3_threshold` (300) | `UINTEGER_T_SQR_TOOM3_THRESHOLD` | `./tune 8 400 8`: `sqr kara` against `sqr toom3` |
| `sqr_ntt_threshold` (1400) | `UINTEGER_T_SQR_NTT_THRESHOLD` | `./tune -t 1000 1400 2000`: `sqr toom3` against `sqr ntt` |
| `div_newton_threshold` (1400) | `UINTEGER_T_DIV_NEWTON_THRESHOLD` | `./tune_div 1 256 1024 4096`: `dc` against `newton` |
| `div_newton_quotient_threshold` (5000) | `UINTEGER_T_DIV_NEWTON_QUOTIENT_THRESHOLD` | `./tune_div 4 1024 1400 2048`: the first argument is the quotient length over the divisor length |
| `div_newton_inv_base` (48) | `UINTEGER_T_DIV_NEWTON_INV_BASE` | `./tune_div -b 24 48 96`, built with each value of the macro: the reciprocal alone |
| `div_trunc_threshold` (64) | `UINTEGER_T_DIV_TRUNC_THRESHOLD` | `./tune_divq`, in the `r1`, `q8`, `n` modes and their exact-multiple forms (`xr1`, `xn`) |

Build each tool from the repository root and run it from where it is built:

```sh
c++ -std=c++17 -O2 -I. bench/tune.cc -o tune
c++ -std=c++17 -O2 -I. bench/tune_div.cc -o tune_div
c++ -std=c++17 -O2 -I. bench/tune_divq.cc -o tune_divq
# with GMP's mpz_tdiv_q alongside (PREFIX holds GMP's include/ and lib/):
c++ -std=c++17 -O2 -I. bench/tune_divq.cc -DHAVE_GMP -I"$PREFIX/include" -L"$PREFIX/lib" -lgmp -o tune_divq
```

- `tune [min max step]` times each multiplication and squaring algorithm at the top level (the
  recursion below takes the library's own cutoffs); its first line prints the cutoffs it was
  built with. `-t n...` times Toom-3, Toom-4 and the transform at the given limb counts,
  `-r an` unbalanced shapes, and `-l bn` lopsided products against the transform.
- `tune_div ratio dn...` times the recursive division (`dc`) against the Newton division forced
  to the same shape, with a quotient of `ratio * dn` limbs: 1 is a balanced division, 0.25 a short
  quotient, 8 a long one. Build with `-DUINTEGER_T_DIV_NEWTON_THRESHOLD=100000000` for the first
  column to be the recursive division at every size.
- `tune_divq r1|q8|n|xr1 sizes...` times `a / d` over divisor and quotient sizes (`r1` a quotient
  as long as the divisor, `q8` a quotient of 8 limbs, `n` the `bench.cc` `div n/2` shape, an `x`
  prefix a dividend that is an exact multiple of the divisor), with GMP's `mpz_tdiv_q` alongside
  when built with `-DHAVE_GMP`. Build with `-DUINTEGER_T_DIV_TRUNC_THRESHOLD=0` to never take the
  truncated kernel and with a large value to take it for every quotient no longer than the
  divisor. The cutoff is set where the exact-multiple case, which always repairs, stays near the
  recursive division's time, not where the kernel stops winning on random operands.

To move a cutoff, find the first size where the next algorithm wins in a sweep (an algorithm is
used from its cutoff up: `bn >= mul_toom3_threshold`, for one), put that value in the header,
and confirm with an A/B run of `bench.cc` over the sizes around it. `AGENTS.md` and
`ARCHITECTURE.md` state the default values; they change with the header. The tests force every
algorithm at small sizes with tiny values of the same macros.

The cutoffs above are literal limb counts, not bit counts, swept at 64-bit limbs. The
multiplication cutoffs are the same at 32-bit limbs, since they depend on the cost of a limb
product against a limb addition, and so are the Newton ones. The transform cutoffs hold at 64-bit
limbs only: at narrower limbs it packs more limbs per word and its crossover is lower. A few
defaults are bit counts divided by the limb width, from sweeps at both widths: `DIV_DC_THRESHOLD`
(2048 bits, with `DIV_DC_STEP_THRESHOLD` a quarter of it) and the conversion tree's
`UINTEGER_T_DIGITS_TREE_THRESHOLD`, `UINTEGER_T_DIGITS_TREE_LEAF`,
`UINTEGER_T_FROM_DIGITS_TREE_THRESHOLD` and `UINTEGER_T_FROM_DIGITS_TREE_LEAF`.

# Apple M1 Pro, 2026-10-31

Timings on an Apple M1 Pro that [docs/OPEN-WORK.md](../../../docs/OPEN-WORK.md) and
[docs/design/](../../../docs/design/) cite. The README's table comes from
[2026-11-01-apple-m4-pro](../2026-11-01-apple-m4-pro/) and
[2026-11-01-intel-i9-9980hk](../2026-11-01-intel-i9-9980hk/).

| | |
|---|---|
| machine | Apple M1 Pro, arm64 |
| OS | macOS (version not recorded) |
| compiler | Apple clang 21.0.0 (clang-2100.3.33.1) |
| flags | `-std=c++17 -O2` |
| GMP | 6.3.0 |
| Boost | 1.92 (Boost.Multiprecision `cpp_int`) |
| uinteger_t | `6ba8ffa` for `uinteger_now_*.txt`, `afdded1` for `uinteger_div_*.txt` |

Both commits are on `master`: `6ba8ffa` is "Note the eight-limb addition in the changelog" and
`afdded1` is "Divide: say where an exact multiple flags; link the changelog entries". The measured
header is each commit's `uinteger_t.hh`, built with that commit's `bench/bench.cc`.

## Files

| file | what it is |
|---|---|
| `uinteger_now_1.txt`, `uinteger_now_2.txt` | two runs of `bench` over 2, 4, 8, 16, 64, 256, 1024, 4096, 16384 and 65536 limbs, every op, at `6ba8ffa` |
| `uinteger_div_1.txt`, `uinteger_div_2.txt` | two runs of the `div` rows alone, at `afdded1`; the table takes its division rows from these |
| `host.txt` | the machine, CPU, compiler, the commits of the whole measurement and the uptime and load before and after the runs |
| `uinteger_div_host.txt` | the commit of the division runs and the uptime and load before and after them |

Each run file is the output of `bench/bench.cc`, unedited. The runs were made with

```sh
c++ -std=c++17 -O2 -I. -I"$PREFIX/include" -DHAVE_GMP -DHAVE_BOOST bench/bench.cc -L"$PREFIX/lib" -lgmp -o bench
./bench 2 4 8 16 64 256 1024 4096 16384 65536          # at 6ba8ffa, twice
./bench 2 4 8 16 64 256 1024 4096 16384 65536 div      # at afdded1, twice
```

where `PREFIX` holds GMP's and Boost's `include/` and `lib/`; `bench/run.sh -p "$PREFIX" -n 2 -o out
2 4 8 16 64 256 1024 4096 16384 65536` builds and runs the same way.

## The table

From this directory:

```sh
python3 ../../summarize.py --table 'uinteger_now_*.txt'
python3 ../../summarize.py --table --ops div 'uinteger_div_*.txt'    # the division rows
```

print every row, each cell the minimum over the runs.

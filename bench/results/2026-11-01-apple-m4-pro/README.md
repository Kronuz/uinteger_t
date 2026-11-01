# Apple M4 Pro, 2026-11-01

The timings behind the Apple M4 Pro column of the [README](../../../README.md#-performance)'s table
and the table of [docs/perf/gmp-boost.md](../../../docs/perf/gmp-boost.md).

| | |
|---|---|
| machine | Apple M4 Pro, arm64 |
| OS | macOS 26 (Darwin 25.6.0) |
| compiler | Apple clang 17.0.0 (clang-1700.3.19.1) |
| flags | `-std=c++17 -O2` |
| GMP | 6.3.0 (Homebrew) |
| Boost | 1.92.0 (Homebrew; Boost.Multiprecision `cpp_int`) |
| uinteger_t | `7b600e8` |

## Files

| file | what it is |
|---|---|
| `run_1.txt`, `run_2.txt` | two runs of `bench` over 2, 4, 8, 16, 64, 256, 1024, 4096 and 16384 limbs, every op, unedited; each starts with the GMP and Boost versions and ends with the count of results compared equal |
| `host.txt` | the machine, CPU, compiler, flags, library prefixes, header commit and checksum, and the load before and after the runs |

The runs were made one after the other, with the machine's other benchmark jobs held off, with

```sh
bench/run.sh -p /opt/homebrew -n 2 -o out/m4 2 4 8 16 64 256 1024 4096 16384
```

## The tables

From this directory:

```sh
python3 ../../summarize.py --readme 'M4 Pro=.' 'i9=../2026-11-01-intel-i9-9980hk'
python3 ../../summarize.py --table .
```

The first prints the README's table with the Intel directory's runs beside these; the second prints
every row of this machine, as in `docs/perf/gmp-boost.md`. Each cell is the minimum over the two
runs.

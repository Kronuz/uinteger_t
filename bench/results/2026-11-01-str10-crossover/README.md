# Decimal printing against GMP, 4,096 to 16,384 bits, 2026-11-01

Where GMP's `mpz_get_str` overtakes `str(10)`, between the README table's rows at 4,096 and
16,384 bits. The header (checksum 3385084435 in each `host.txt`) runs the encoding kernel up to
14,336 bits on Apple's arm64 cores and to 8,192 elsewhere; it differs from `c8156b7`'s only in
comments and in the default printing cutoff at 32-bit limbs on Apple's cores, so at the 64-bit limbs
of these runs it is the same code.

| directory | machine | compiler | GMP | sizes (limbs) |
|---|---|---|---|---|
| `apple-m4-pro/` | Apple M4 Pro, macOS 26 | Apple clang 17.0.0 (clang-1700.3.19.1) | 6.3.0 (Homebrew) | 64, 128, 160, 192, 208, 224, 225, 240, 256 |
| `apple-m1-pro/` | Apple M1 Pro, Darwin 27.2.0 | Apple clang 21.0.0 (clang-2100.3.33.1) | 6.3.0 (Homebrew) | 64, 96, 128, 160, 192, 208, 224, 225, 240, 256 |
| `intel-i9-9980hk/` | Intel Core i9-9980HK, macOS 15 | Apple clang 17.0.0 (clang-1700.6.4.2) | 6.3.0 (Homebrew) | 64, 96, 128, 144, 160, 176, 192, 208, 224, 256 |
| `intel-i9-9980hk-fine/` | the same | the same | the same | 96, 104, 112, 120, 128 |

Each directory holds three runs of `bench` (`run_1.txt` to `run_3.txt`, unedited) and `host.txt`,
made under each machine's exclusive benchmark hold (the load averages in `host.txt` count jobs waiting
for it; the spreads are 0.0% to 3.5%):

```sh
bench/run.sh -p /opt/homebrew -B -n 3 -o out/m4 64 128 160 192 208 224 225 240 256 str10
bench/run.sh -p /opt/homebrew -B -n 3 -o out/m1 64 96 128 160 192 208 224 225 240 256 str10
bench/run.sh -p /usr/local -B -n 3 -o out/i9 64 96 128 144 160 176 192 208 224 256 str10
bench/run.sh -p /usr/local -B -n 3 -o out/i9-fine 96 104 112 120 128 str10
```

`python3 bench/summarize.py DIR` prints the best time of each row and uinteger_t's time over GMP's.
On the M4 Pro uinteger_t is faster up to 224 limbs (14,336 bits: 15.8 against 17.3 us), ties at 225
(17.6 against 17.5 us), where the tree takes over, and GMP is faster from 240. On the M1 Pro uinteger_t is faster up to 160
limbs (10,240 bits: 13.5 against 13.8 us) and GMP from 192 (12,288 bits: 18.7 against 18.0 us). On the i9 uinteger_t is faster up to 96
limbs (6,144 bits: 8.6 against 9.0 us), ties at 104, 112 and 120 (0.99, 1.00 and 1.03, within the
spreads) and GMP is faster from 128 (8,192 bits: 14.1 against 13.1 us).

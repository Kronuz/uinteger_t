# Testing

How a change to `uinteger_t.hh` is verified: which check catches which failure, how to run each
one, and how the measurements in the README and `docs/design/` are reproduced.
[AGENTS.md](../AGENTS.md) maps every test file, [BENCHMARKING.md](BENCHMARKING.md) holds the timing
protocol, [MUTATION.md](MUTATION.md) the mutation tool, and [OPEN-WORK.md](OPEN-WORK.md) what no test
reaches.

## The checks and what each catches

The library computes exact values, so every oracle is a second way to the same value: a reference
schoolbook, an algebraic identity, an enumeration, an independent model, or GMP. `CMakeLists.txt`
defines 95 CTest tests (90 in a TSan build). Every test but the smoke test reports its failures
itself, so it checks under `NDEBUG` too.

| check | CTest tests or command | what it is there to catch |
|---|---|---|
| smoke test | `uinteger_t` (`test/test.cc`) | a broken constructor, operator or `str` at 64-bit limbs; it checks with `assert`, so only without `NDEBUG` |
| property tests | `uinteger_t_property` and 35 builds of it (next table) | a wrong value: products against a reference schoolbook on 32-bit words, `Q d + R = N` with `R < d`, shift and base round trips, `digits()` and `from_digits()` against repeated division, each digit splitter against the general one, results aliasing their operands |
| kernel tests | `uinteger_t_dc_kernel*` (9), `_divq_kernel*` (7), `_ntt_kernel*` (5), `_newton_kernel*` (7), `_radix_tree*` (5) | a raw-limb kernel that writes past its result or scratch (guard limbs around an exactly sized scratch), whose result depends on what the buffers held before (dirty fills), or whose scratch plan or size guard is wrong at a 16- or 32-bit `size_t` |
| exhaustive reciprocal | `uinteger_t_recip16` | a wrong `k_inv_3by2` on any of the 2^31 normalized divisors at 16-bit limbs, including the 720 that reach its last correction, and a wrong `k_div_3by2` |
| quotient-only model | `uinteger_t_divq_model` | a gap in the proof of [quotient-only-division.md](design/quotient-only-division.md): a model of the kernel that does not include the header enumerates every small case at base 4 |
| limb container | `uinteger_t_inline*` (6), `_public_api*` (4), `_wide_shift*` (3) | a write past the inline buffer, a grow, shrink, copy, move or self-assignment that goes wrong at the inline/heap boundary, a public member that turned private, a shift count wider than a limb |
| allocation counts | `uinteger_t_alloc*` (5) | an allocation a small-value operation should not make, a leaked block, an operand left half-written by a failed allocation; it replaces `operator new`, so TSan builds leave it out |
| thread races | `uinteger_t_radix_threads*` (3) | a missing happens-before edge in the lazily built conversion tables (TSan reports it), a torn table (a wrong string in any build) |
| GMP differential fuzz | `test/fuzz_gmp.cc`, not in CTest | a disagreement with GMP at sizes CTest does not reach: products of 100 to 5000 and of 1K to 1M limbs, divisions by divisors of 1K to 500K limbs, conversions of up to 64K limbs in bases 3 to 62 |
| mutation | `tools/mutate.py` | a test that passes whatever the code does |
| base-x golden | `base_x_golden` in [base-x](https://github.com/Kronuz/base-x), built against this header | a change in the digits `digits()` writes or `from_digits()` reads for base-x's presets, whose text base-x users store |

The property test runs along every axis the header can be built on. Each build is a CTest test
named `uinteger_t_property` plus a suffix:

| axis | suffixes | what the build forces |
|---|---|---|
| limb width | `32`, `16` | 32- and 16-bit limbs (`DIGIT_T`, `HALF_DIGIT_T`), where narrow types promote to `int` |
| portable primitives | `_portable` | the limb primitives without `unsigned __int128` (`UINTEGER_T_NO_INT128`) |
| multiplication cutoffs | `_tiny`, `_tiny32`, `_tiny_portable`, `_tiny3`, `_mid` | Karatsuba, Toom-3, Toom-4 and the squaring kernels at every size (cutoffs of 2 to 4 limbs; `_tiny3` without Toom-4, `_mid` at 4 to 17 limbs) |
| transform | `_ntt`, `_ntt32`, `_ntt16`, `_ntt_mid`, `_ntt_dc`, `_ntt_dc32` | the transform from 4 limbs with an 8-limb leaf (`_ntt_mid` from 40 limbs, with Karatsuba and Toom-3 below it); in `_ntt_dc` and `_ntt_dc32`, the recursive division's products through it |
| recursive division | `_dc`, `_dc32`, `_tiny_dc`, `_tiny_dc32` | the recursion many levels deep (`DIV_DC_THRESHOLD=4`), over each multiplication path |
| Newton division | `_newton`, `_newton32`, `_newton_ntt` | the reciprocal iteration and the blocks on small operands |
| quotient-only division | `_trunc`, `_trunc32` | `k_div_sb_trunc` for every quotient-only division up to twice the divisor's length |
| conversion | `_tree`, `_tree32`, `_tree_portable`, `_tree_tiny`, `_unroll0`, `_unroll8`, `_split0`, `_radix14k`, `_radix14k32` | the conversion tree above two limbs, no unrolled kernel or all eight, the general digit splitter for every base, the kernels' limit of Apple's arm64 cores (14336 bits) on every platform |
| limb container | `_inline1`, `_inline3`, `_inline2_32`, `_poison`, `_poison1` | the inline/heap boundary at 1 to 3 limbs; limbs sized without zero fill filled with a pattern (`UINTEGER_T_POISON_LIMBS`), which exposes a limb a kernel leaves unwritten |

The other tests are built along several of the same axes (limb width, portable primitives, tiny
thresholds, buffer size); `CMakeLists.txt` sets each target's macros. A platform branch or macro
that no target builds is untested, so a new one gets a target.

Two checks go further by hand. `UINTEGER_T_BIG_TESTS=1` in the environment adds the 512 MiB shift to
`uinteger_t_wide_shift32`. `uinteger_t_divq_model exh B dn qn G guard` and `uinteger_t_divq_model rnd
B dn qn G guard iters [seed]` run the model at other bases and sizes (`rnd 16 7 3 2 1 2000`, for
one); the usage is at the top of `test/divq_model.cc`.

## Build types and sanitizers

`.github/workflows/ci.yml` calls the shared workflow
[`cmake.yml`](https://github.com/Kronuz/cpp-libraries/blob/main/.github/workflows/cmake.yml) of
cpp-libraries on every push to `master`, every pull request, and nightly. Each job builds every
target and runs CTest:

| job | runner, compiler | build type and flags | counts |
|---|---|---|---|
| `linux-x86_64-gcc`, `linux-x86_64-clang`, `linux-arm64-gcc` | Ubuntu 24.04, GCC or Clang | RelWithDebInfo, `-O2 -g -D_GLIBCXX_ASSERTIONS` | full |
| `macos-arm64`, `macos-x86_64` | macOS 15, Apple clang | RelWithDebInfo, `-O2 -g -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE` | full |
| `freebsd-x86_64` | FreeBSD 14.3 in a VM | RelWithDebInfo, `-O2 -g` | full |
| `linux-asan-ubsan` | Ubuntu 24.04, Clang | Debug, `-fsanitize=address,undefined -fno-sanitize-recover=undefined -D_GLIBCXX_ASSERTIONS` | light |
| `linux-tsan` | Ubuntu 24.04, Clang | RelWithDebInfo, `-O1 -g -fsanitize=thread -fno-omit-frame-pointer` | full, without `alloc` |

The RelWithDebInfo flags replace CMake's `-O2 -g -DNDEBUG`, so `assert` stays on in every job: in
`test/test.cc` and in the header itself (the one in `k_add_at` catches an untrimmed Toom-4
coefficient). No job builds MSVC.

**Light mode.** A Debug build passes the heavy tests smaller arguments, through variables on their
`add_test` lines; every other build type runs the full counts. AGENTS.md ("Build & run the test")
says what light mode samples and how to check the variables after a merge.

| variable | tests | Debug | other build types |
|---|---|---|---|
| `UINTEGER_T_PROPERTY_ARGS` | the property builds except `_tiny_portable` | `100 600 1` | none: 300 iterations, 1200 words |
| `UINTEGER_T_TREE_ARGS` | `uinteger_t_radix_tree` and its `32`, `_tiny`, `_tiny32` builds | `1 80 1` | none: 1 round, 150 limbs |
| `UINTEGER_T_PORTABLE_TREE_ARGS` | `uinteger_t_radix_tree_portable` | `1 50 1` | `1 50` |
| `UINTEGER_T_NTT_ARGS` | `uinteger_t_ntt_kernel*` | `1 1` | none: 1 round |
| `UINTEGER_T_NEWTON_ARGS` | `uinteger_t_newton_kernel*` | `60 150` | none: 300 iterations, 400 limbs |
| `UINTEGER_T_DIVQ_ARGS` | `uinteger_t_divq_kernel*` | `1 24` | none: 3 rounds, 40 limbs |
| `UINTEGER_T_RECIP_ARGS` | `uinteger_t_recip16` | `256 1009` | none: every divisor, stride 251 for the division step |
| `UINTEGER_T_DIVQ_MODEL_ARGS` | `uinteger_t_divq_model` | `light` | none: the full sweep |

`uinteger_t_property_tiny_portable` runs `40 300` in every build type.

**On macOS.** With Apple clang 17 on macOS 26 (Apple M4 Pro), an empty program built with
`-fsanitize=address` hangs at startup and one built with `-fsanitize=thread` crashes, so ASan and
TSan run in CI. Locally, run UBSan with libc++ hardening in Debug (light counts), run Release (full
counts; `test/test.cc` checks nothing there), and build and link every target with
`-fsanitize=thread`, which CI's TSan job needs:

```sh
cmake -S . -B build-ubsan -DCMAKE_BUILD_TYPE=Debug \
  "-DCMAKE_CXX_FLAGS=-O1 -g -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover=undefined -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE" \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=undefined
cmake --build build-ubsan --parallel 4 && ctest --test-dir build-ubsan --output-on-failure --parallel 4

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel 4 && ctest --test-dir build-release --output-on-failure --parallel 4

cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O1 -g -fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-tsan --parallel 4      # link only; ctest -N lists 90 tests
```

**On Linux.** The CI jobs run as they are. With `CC=clang CXX=clang++`, the sanitizer job is

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -D_GLIBCXX_ASSERTIONS" \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build build-asan --parallel 4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 ctest --test-dir build-asan --output-on-failure --parallel 2
```

and the TSan job is the `build-tsan` tree above, run with `TSAN_OPTIONS="halt_on_error=1
second_deadlock_stack=1" ctest --test-dir build-tsan --output-on-failure`.

## Trying a change

In this order, each step on the change as it will land:

1. **Build without warnings** under `-Wall -Wextra`, with Clang and GCC (the `build-ubsan` tree above
   has them on).
2. **CTest** in the Debug UBSan tree and in Release, and the TSan link. A new branch, macro or
   kernel gets its CTest builds (and its light-mode argument if it is heavy).
3. **The GMP fuzz**, at 64-bit limbs only (it builds values from 64-bit words). `PREFIX` holds GMP's
   `include/` and `lib/` (`/opt/homebrew` for Homebrew on Apple Silicon):

   ```sh
   c++ -std=c++17 -O2 -I. -I"$PREFIX/include" test/fuzz_gmp.cc -L"$PREFIX/lib" -lgmp -o fuzz_gmp
   ./fuzz_gmp 3000 && ./fuzz_gmp 3000 7
   ```

   The arguments are iterations (default 2000), seed (12345), large rounds (6), mid rounds (600) and
   large division rounds (8); a run ends with `fuzz_gmp OK (N iterations)`, and a failure prints the
   operation and the sizes. For a change that only runs above a cutoff, build it again with the
   macros of the CTest build that forces the path, under UBSan, so the fuzz reaches the path at
   small sizes:

   ```sh
   c++ -std=c++17 -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined -I. -I"$PREFIX/include" \
     -DUINTEGER_T_MUL_KARATSUBA_THRESHOLD=2 -DUINTEGER_T_MUL_TOOM3_THRESHOLD=3 -DUINTEGER_T_MUL_TOOM4_THRESHOLD=4 \
     -DUINTEGER_T_SQR_KARATSUBA_THRESHOLD=2 -DUINTEGER_T_SQR_TOOM3_THRESHOLD=3 -DDIV_DC_THRESHOLD=4 \
     test/fuzz_gmp.cc -L"$PREFIX/lib" -lgmp -o fuzz_gmp_tiny
   ```

4. **The mutant set** of each kernel group touched, `nice -n 19 tools/mutate.py SET`
   ([MUTATION.md](MUTATION.md) lists the sets). Every mutant not argued equivalent is killed; a new
   branch, bound or carry gets a mutant and the test that kills it.
5. **The identity of the outputs.** The property tests and the fuzz check every value; the fuzz checks
   `str` against `mpz_get_str` and `digits()` against it up to 64K limbs. A change that can alter text
   (`str`, the parsers, `digits`, `from_digits`) is also compared byte for byte with the previous
   header (AGENTS.md, Rules of thumb), and base-x's golden test runs on the new header, from a
   base-x checkout:

   ```sh
   cmake -S . -B build-ut -DFETCHCONTENT_SOURCE_DIR_UINTEGER_T=/path/to/uinteger_t
   cmake --build build-ut --target base_x_golden base_x_golden_scalar
   ctest --test-dir build-ut -R golden
   ```

6. **The speed**, for a change meant to be faster: an A/A run, then an interleaved A/B against
   `master` on a quiet machine ([BENCHMARKING.md](BENCHMARKING.md)), at the sizes users hit and around
   every cutoff the change moves, and base-x's `bench/bench.cc` too. The change counts if it is faster
   by about 3% or more on the rows it targets and by more than the A/A spread; 10% counts without
   argument. A timing of a 1- to 8-limb operation needs a compiler barrier on each result
   ([limb-container.md](design/limb-container.md), Timing small values).

## Reproducing the experiments

**The README table and [docs/perf/gmp-boost.md](perf/gmp-boost.md)** come from
`bench/results/2026-11-01-apple-m4-pro/` and `bench/results/2026-11-01-intel-i9-9980hk/`: two
`bench/run.sh` runs per machine, unedited, with `host.txt` and a `README.md` holding the command.
The tables print from the committed files:

```sh
cd bench/results/2026-11-01-apple-m4-pro
python3 ../../summarize.py --readme 'M4 Pro=.' 'i9=../2026-11-01-intel-i9-9980hk'
python3 ../../summarize.py --table .
```

A new run is `bench/run.sh -p /opt/homebrew -n 2 -o out/m4 2 4 8 16 64 256 1024 4096 16384` on each
machine ([BENCHMARKING.md](BENCHMARKING.md), Reproducing the README table). The `2026-10-31`
directories hold the Apple M1 Pro and Intel runs that `docs/design/` and OPEN-WORK.md cite, each with
its commands. `bench/results/2026-11-01-str10-crossover/` holds the runs between 4,096 and 16,384 bits
behind the README's statement of where decimal printing stops beating GMP on each machine.

**The cutoffs** come from the tune tools, built from the repository root and run on a quiet machine.
Their raw output is not committed: the tables in the design notes are the record, each with its
machine.

| cutoffs | build | sweep | recorded in |
|---|---|---|---|
| Karatsuba, Toom-3, squaring | `c++ -std=c++17 -O2 -I. bench/tune.cc -o tune`; for 32-bit limbs add `-DDIGIT_T=std::uint32_t -DHALF_DIGIT_T=std::uint16_t` | `./tune 8 400 8`, `./tune -r 300` | [multiplication.md](design/multiplication.md), The cutoffs as measured |
| Toom-4, transform | the same | `./tune -t 128 192 256 320 384 448 512`, `./tune -r 640`, `./tune -r 1024`, `./tune -t 1152 1280 1344 1408 1536`, `./tune -l 1000` | the same |
| Newton division | the same for `bench/tune_div.cc`, with `-DUINTEGER_T_DIV_NEWTON_THRESHOLD=100000000` | `./tune_div 1 32 256 1024 2048 4096 8192 16384`, `./tune_div 8 32 512 1280 1536 2048 4096`, `./tune_div 0.5 2048 8192 16384`, `./tune_div 0.25 4096 8192 16384` | [division.md](design/division.md), The Newton division |
| quotient-only division | the same for `bench/tune_divq.cc`, once with `-DUINTEGER_T_DIV_TRUNC_THRESHOLD=0` and once with the candidate (64, 192, 512) | `./tune_divq n 16 32 64 128` and `./tune_divq xn 16 32 64 128`; `r1`, `r0.5`, `r2` and `q4` for the random-operand figures | division.md, The quotient-only kernel's cutoff |
| conversion tree | the same for `bench/tune_radix.cc`, with `-DUINTEGER_T_RADIX_MAX_BITS=40960`, at 64- and 32-bit limbs | `./tune_radix 8704 10240 12288 14336 16384 18432 20480 24576 28672 32768 40960` on each machine | [base-conversion.md](design/base-conversion.md), The tree |

The small-value timings in [limb-container.md](design/limb-container.md) and CHANGELOG.md come from
a harness outside the repository; `bench/bench.cc` has no copy, move, shift or comparison rows.

**The mutant counts** in MUTATION.md are `nice -n 19 tools/mutate.py SET` for each of `divq`, `toom`,
`toom4`, `ntt`, `newton`, `kernels` and `repr`, in the tool's default Debug `-O1` build on an Apple M4
Pro; `tools/mutate.py --check SET` confirms that every edit still applies after a header change.

**The fuzz counts** are the arguments of `fuzz_gmp`: `./fuzz_gmp 3000` is 3000 iterations at seed
12345, then 600 mid rounds, 6 large rounds and 8 large division rounds.

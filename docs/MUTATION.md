# Mutation testing

A test proves a property only if breaking the code it guards makes it fail. `tools/mutate.py` checks
that: it breaks the header one edit at a time, runs the tests that should notice, and reports the
edits no test catches.
[TESTING.md](TESTING.md) lists the tests and the other checks a change passes.

```sh
nice -n 19 tools/mutate.py divq                 # every mutant of tools/mutants/divq.txt
nice -n 19 tools/mutate.py divq noF1 G1         # only those named
tools/mutate.py --list divq                     # the mutants and the tests that should kill each
tools/mutate.py --check divq                    # only check that every edit applies; no build
```

It needs Python 3 (standard library only), CMake, a C++17 compiler and, when present, Ninja. It runs at
most `-j` mutants at a time (default 4), each building one test at a time, so `nice -n 19` keeps a
shared machine usable. On an Apple Silicon Mac it builds for arm64 even when Python itself is a translated
x86_64 build (a translated Python would otherwise hand CMake the x86_64 architecture).

## How a mutant is run

A mutant is one or more textual edits to `uinteger_t.hh`. The tool copies `CMakeLists.txt`, `test/`,
`examples/` and the headers to a temporary directory, replaces the header with the edited one,
configures a fresh CMake tree there and, for each CTest test the mutant names, builds that test's
executable and runs it through `ctest`. The tests are the ones CI runs, with the flags and arguments
`CMakeLists.txt` gives them. A mutant

- is **killed** when one of its tests fails: a nonzero exit, a failed check, a sanitizer report, a
  crash or a timeout (the first failing test stops the mutant);
- **survives** when all of its tests pass: no test guards what the edit broke, or the edit does not
  change what the code computes (a mutant marked `equivalent:` carries that argument, and its survival
  is expected);
- **does not apply** when a search string does not match the header the expected number of times;
- **does not build** when the edited header or a test fails to compile; the mutant is invalid.

Before the first mutant the tool builds and runs the named tests on the unmodified header, so a kill
means the edit broke something. The exit status is 0 when every mutant not marked `equivalent:` was
killed.

The build is Debug with `-O1`, which runs the tests in their light mode. `--build-type Release` runs
them in full, `--cxx-flags` replaces the flags (for a sanitizer: `--cxx-flags '-O1
-fsanitize=undefined -fno-sanitize-recover=undefined'`), `--define VAR=VALUE` passes another `-D` to
CMake, `--timeout` bounds a test (600 s) and `--keep --work DIR` keeps the trees to look at a build.

## The mutants files

`tools/mutants/SET.txt` has one file per kernel group:

| set | what the mutants break | mutants | killed | survive |
|---|---|---:|---:|---|
| `divq` | the quotient-only division: its repair, window, flags, guard and fallback | 42 | 32 | 10 equivalent |
| `toom` | Karatsuba and Toom-3: evaluation, interpolation, signs, recombination, scratch, dispatch | 35 | 34 | 1 equivalent |
| `toom4` | Toom-4: evaluation at 1, -1, 2, -2 and 1/2, interpolation, scratch, dispatch | 77 | 76 | 1 equivalent |
| `ntt` | the transform multiplication: primes, word primitives, the transform, the size plan, the scratch | 119 | 119 | |
| `newton` | the Newton division: the reciprocal's iteration, the blocks' corrections, the size plan | 45 | 43 | 2 equivalent |
| `kernels` | the 3-by-2 division step and its reciprocal, lopsided and odd-split products, the base cases | 22 | 19 | 3 equivalent |
| `repr` | the limb container's operators: one-limb fast paths, shift and subtraction edges, aliasing | 13 | 13 | |

The counts are from Debug builds with `-O1` (the default) on an Apple M4 Pro. The `divq` mutants that
survive are argued equivalent: a larger window, flagging more digits, a stale limb nothing reads, a
bound with one limb of slack, and the repair's own safety net, which the proof makes unreachable.

A file is lines of `key: value`; blank lines and lines starting with `#` are ignored. Before the first
mutant, `header:` names the file the edits apply to and `tests:` the CTest tests every mutant runs
(its default). Then, per mutant:

```
mutant: noF1
what: flag 1 (the window's top limbs equal the divisor's) is never raised
find: if (w[dn] == d1 && w[dn - 1] == d0) {\n\t\t\t\tflag = 1;
repl: if (false) {\n\t\t\t\tflag = 1;
```

| key | meaning |
|---|---|
| `mutant:` | the name, letters, digits and `_ . + -`; it starts a mutant |
| `what:` | one line on what the edit breaks |
| `find:` | an exact string; it must occur exactly `count` times (default 1) |
| `regex:` | a Python regular expression instead of `find:`, matched on the whole file |
| `count:` | how many matches the preceding `find:` or `regex:` must have |
| `repl:` | the text that replaces every match; a literal, with no group references |
| `tests:` | the CTest tests that should kill it, in the order to run them, instead of the file's |
| `equivalent:` | why it survives, for an edit that does not change what the code computes |

A mutant has one or more `find:` (or `regex:`) and `repl:` pairs, applied in order. `find:` and `repl:`
take `\n`, `\t`, `\\` and `\xHH`; the value is stripped, so write leading or trailing spaces as `\x20`.
Put the cheap tests that kill most mutants first, since the first failure ends the mutant.

## Keeping the sets current

After editing the header, run `--check` on the sets of the kernels you touched: a search string that
no longer matches once is reported as not applying, and the entry is updated to the new code. After
changing a kernel, run its set: a survivor is either a test to strengthen or an edit to argue
equivalent. Add a mutant for each branch, bound and carry you add, with the test that should fail.

# The limb container

ARCHITECTURE.md (Internal representation, Growth strategy) and docs/API.md (Limb storage) describe
the layout: a data pointer `_p`, a size, a capacity `_cap` and a buffer `_local` of `inline_capacity`
limbs. This note gives the invariants a change to the container or to an operator must keep, the
failure-safety rules, and the measurements behind the layout. Timings are on an Apple M4 Pro (Apple
clang 17, `-O2`, the best of 5 runs of 300,000 iterations) unless a line names another machine.

## Invariants

- `_p` points at `_local` exactly when `_cap == inline_capacity`, and at a heap block of `_cap` limbs
  exactly when `_cap > inline_capacity`. Both facts hold together; each test reads the one that keeps it
  cheap and quiet:
  - `data()` returns `_p`. Choosing between `_local` and the block by `_cap` compiles to a select that
    loads the block pointer, which overlaps `_local[0]`, so every operation's addresses wait for the
    previous operation's store to that limb.
  - `release()` frees when `_p != _local`. The capacity test is the same condition, but g++ 16 at `-O1`
    and `-O3` then reports `-Wfree-nonheap-object` where it inlines the destructor of an inline value,
    which breaks a `-Wall -Werror` build.
- The value is trimmed: a nonzero top limb, or no limbs for zero. `compare`, `bits()`, `operator bool`
  and the zero-divisor check read the size as the value's length, so every constructor and operation
  trims. An untrimmed zero passes the zero-divisor check and the kernel divides by a zero limb
  (undefined behavior: 0 on arm64, `SIGFPE` on x86-64); the base-256 parser skips leading zero bytes for
  this reason.
- A `uinteger_t` is its container and nothing else: no offset into the limbs, no view, no deferred
  carry. State outside the limbs goes stale: with an offset, a self copy-assignment after an in-place
  right shift (`x >>= 64; x = x`, or `x *= 1`) gives the shifted-out limbs back, and a moved-from value
  reports `size()` as 2^64 - 1. Moving the limbs down on an in-place right shift measures faster than
  moving an offset: `x <<= 67; x >>= 67` takes 11 ns at 1 limb and 28 ns at 32 limbs, against 160 ns
  for the offset form with its temporaries.
- The default constructor, the move constructor and move assignment are `noexcept`, so `std::vector`
  relocates values by move. A move of a heap value takes the block and points the source back at its
  buffer, zero; a move of an inline value copies the limbs into whatever storage the target has (its
  buffer, or its heap block, which it keeps). A self-move (`x = std::move(x)`) keeps the value.

## Failure safety

A failed allocation leaves every operand a valid value with its old limbs:

- Every operation that grows a value sizes the result before its first store. `test/alloc.cc` fails
  each allocation of every growing operation in turn and checks the operands.
- A sum makes room for its carry limb before it writes, only when the top limbs can carry (their sum
  wraps or is all ones) and the capacity has no spare limb. Reserving whenever the sum fills the
  capacity sends sums that cannot carry to the heap; reserving only past the buffer leaves a
  half-written operand when the carry's allocation fails after the store (`x += y`). The test costs an
  8-limb `a + b` 4.3 to 5.0 ns, and 0.2 to 0.3 ns at 1 to 4 limbs.
- Copy assignment allocates before it releases the old storage.
- Multiplication allocates its scratch before it sizes the result.
- The four-argument `divmod(q, r, a, b)` writes the quotient before it allocates the remainder, so a
  failed allocation there leaves `q` written: a quotient that aliases the dividend loses the dividend.
  `alloc.cc` injects no failure into a division.

## The measurements behind the layout

`data()` as a pointer member against a choice by capacity, `a + b`:

| limbs | choice by capacity | pointer member |
|---:|---:|---:|
| 2 | 8 ns | 2.5 ns |
| 3 | 11 ns | 2.8 ns |
| 4 | 13 ns | 3.1 ns |

The pointer costs one word of the object.

`inline_capacity` from a sweep of 4, 6, 8, 9, 12 and 16 limbs (`sizeof` 56, 72, 88, 96, 120 and 152 bytes
on LP64, 24 plus 8 per limb):

- From 8 limbs a copy, `+`, `-`, `<<` and `>>` of values up to 8 limbs stay inline: an 8-limb `a + b`
  takes 18.6 ns at 4 limbs of buffer and 4.4 at 8.
- 4 and 6 limbs spill the product of two 4-limb values (27 ns against 15).
- 9, 12 and 16 limbs differ from 8 only on the product of two 8-limb values (49 ns at 8, 40 at 16), and
  pushing into a `std::vector<uinteger_t>` costs 3 to 5 ns more at 16.

So the default is 64 bytes' worth: 8 limbs at 64-bit limbs, 16 at 32, 32 at 16, and 88 bytes of object
at the defaults. The base-58 decoder sizes its result to the limbs the value has (`horner_stack_chunks`),
so a 32-byte key (4 limbs) fits.

## Timing small values

A timing of a 1- to 8-limb operation needs a compiler barrier on each result's data pointer (as
`keep()` in `test/alloc.cc` does). Sinking only `x.size()` lets the compiler elide a copy, which then
reads as about 0.2 ns. `bench/bench.cc` times sums, products, divisions and conversions from 2 limbs
and has no copy, move, shift or comparison rows; the inline-buffer numbers in CHANGELOG.md come from a
harness with that barrier.

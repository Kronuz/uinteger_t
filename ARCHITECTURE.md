# Architecture

`uinteger_t` is one header, `uinteger_t.hh`, holding a single class
`uinteger_t`. This document covers how the value is stored, the arithmetic
algorithms, their complexity, and the design choices that shape the code.

## Internal representation

A value is a sequence of 64-bit limbs ("digits") held in a
`std::vector<uint64_t>`, little-endian: the limb at index 0 is the least
significant. The limb type is `DIGIT_T` (default `std::uint64_t`) with a
`HALF_DIGIT_T` (default `std::uint32_t`) used where the code needs to split a
limb in half during multiplication and base conversion. A `static_assert`
enforces that the half digit is exactly half the width of the digit
(`uinteger_t.hh:149`).

The object does not store the vector directly. It carries five members
(`uinteger_t.hh:153`): `_begin` and `_end` offsets, an owned `_value_instance`
container, a reference `_value` that usually points at `_value_instance`, and a
`_carry` flag. The `[_begin, _end)` window lets the multiplication routines
build a slice that views a sub-range of another number's limbs without copying
them, via the private window-view constructor (`uinteger_t.hh:1931`). When a
`uinteger_t` is a normal owned value, `_value` aliases its own
`_value_instance`; when it is a slice, `_value` aliases the source's container
and the window narrows it. `_carry` records a pending most-significant carry
limb so an operation can defer materializing it.

Leading-zero limbs are stripped by `trim()` (`uinteger_t.hh:191`) so the limb
count tracks the significant magnitude and `size()` reflects it. The value zero
is the empty limb sequence, which is why `operator bool()` is just
`size() != 0`.

## Growth strategy

The vector grows in both directions with a geometric `growth_factor` of 1.5
(`uinteger_t.hh:152`). `grow(n)` reserves `n * 1.5` when it needs more room
(`uinteger_t.hh:167`). Appending to the most significant end is the common case;
prepending (growing backward, toward lower significance) is handled by reserving
extra capacity and shifting within the existing buffer so shifts and aligned
operations don't reallocate on every limb (`uinteger_t.hh:208` onward). The 1.5
factor is the usual compromise: amortized O(1) append without the 2x factor's
tendency to leave large unreusable holes.

## Addition and subtraction

Addition and subtraction walk the limbs from least to most significant doing
64-bit add/sub with a carry/borrow propagated limb to limb. Where the compiler
exposes them, the code uses intrinsics (`__addcarry_u64` / `__subborrow_u64` on
MSVC, `__builtin_addcll` / `__builtin_subcll` on clang, `__int128` widening on
GCC) selected by the `HAVE_*` macros at the top of the header
(`uinteger_t.hh:357` onward for the limb primitives). The fallback path
synthesizes carry from the wrapped-sum comparison. Cost is linear in the number
of limbs.

Subtraction is unsigned: a smaller-minus-larger result wraps rather than going
negative, matching how a fixed-width unsigned type would behave for the operand
width involved.

## Multiplication

Multiplication dispatches on operand size (`uinteger_t.hh:1523` onward):

- `single_mult` handles the case where one operand is a single limb
  (`uinteger_t.hh:1523`).
- `long_mult` is schoolbook O(n*m) limb multiplication, used for small operands
  (`uinteger_t.hh:1553`).
- `karatsuba_mult` is the recursive Karatsuba algorithm, used once operands
  exceed `karatsuba_cutoff` limbs, defined as `1024 / digit_bits` so the
  threshold is "about 1024 bits" (`uinteger_t.hh:151`, `uinteger_t.hh:1645`).
  Below the cutoff each recursion falls back to `long_mult`.
- `karatsuba_lopsided_mult` handles the case where the two operands differ
  greatly in size, slicing the larger operand into chunks the size of the
  smaller one and combining the partial products (`uinteger_t.hh:1616`).

The Karatsuba path is where the window-view representation earns its keep: the
high and low halves of an operand are passed as slices (`[_begin, _end)` windows
into the same vector) instead of fresh allocations, so the recursion does not
copy limbs at every level. Karatsuba turns the classic O(n^2) cost into
O(n^1.585).

## Division and modulo

Division uses long division, Knuth's Algorithm D (`knuth_divmod`,
`uinteger_t.hh:1776`), with a fast `single_divmod` path when the divisor is a
single limb (`uinteger_t.hh:1749`). The public entry point is `divmod`
(`uinteger_t.hh:1871` / `uinteger_t.hh:1909`), which returns quotient and
remainder together; `operator/` and `operator%` are thin wrappers over it, so
asking for both at once is cheaper than computing each separately. Division or
modulo by zero throws `std::domain_error`. Cost is O(n*m) in the limb counts of
dividend and divisor.

## Base conversion (`str`)

`str(base)` renders the value in any base from 2 to 36 (`uinteger_t.hh:2255`).
For power-of-two bases it takes a fast bit-slicing path: `base_bits(base)`
returns the bits per digit and the routine walks the half-limbs pulling out
`base_bits` at a time, no division required (`uinteger_t.hh:745`,
`uinteger_t.hh:2263`). For non-power-of-two bases it falls back to repeated
`divmod(base)`, collecting remainders as digits (`uinteger_t.hh:2289`). Output
buffers are pre-reserved from `base_size(base)` so the common case does not
reallocate mid-render. Digits above 9 are lowercase letters via `chr`
(`uinteger_t.hh:710`).

## Complexity summary

- Addition, subtraction: O(n) in limbs.
- Multiplication: O(n*m) long, O(n^1.585) Karatsuba above ~1024 bits.
- Division, modulo: O(n*m), Algorithm D.
- `str` for power-of-two bases: O(bits); other bases: O(n^2)-ish from repeated
  division.
- `bits()`: O(1) given the limb count plus a leading-limb bit scan.

## Design decisions

- Limbs in a `std::vector<uint64_t>`, little-endian, trimmed of leading zeros.
  The empty vector is zero, which keeps `operator bool()` and emptiness checks
  trivial.
- A window view (`[_begin, _end)` plus a reference into another value's
  container) lets multiplication recurse on slices without copying, which is
  what makes the Karatsuba path pay off.
- Intrinsic carry/borrow/multiply primitives are selected at compile time
  through `HAVE_*` macros, with portable fallbacks, so the hot limb loops use
  hardware add-with-carry and 128-bit multiply where available.
- The bidirectional 1.5x growth keeps both append and prepend amortized cheap,
  which matters for shifts that grow the low end.
- `divmod` is the primitive; `/` and `%` are conveniences over it.

## Limitations

- Unsigned only. No negative values; subtraction wraps.
- Native-integer conversion operators are explicit and truncate to the low limb.
- The `namespace std` trait specializations (`is_arithmetic`, `is_integral`,
  `is_unsigned`) are a deliberate convenience and are technically undefined
  behavior; the header says so at the point it adds them.
- No signed type, rationals, or modular-arithmetic helpers; this is a plain
  arbitrary-precision unsigned integer.

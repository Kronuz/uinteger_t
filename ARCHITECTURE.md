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
enforces that the half digit is exactly half the width of the digit.

The object does not store the vector directly. It carries five members: `_begin` and `_end` offsets, an owned `_value_instance`
container, a reference `_value` that usually points at `_value_instance`, and a
`_carry` flag. The `[_begin, _end)` window lets the multiplication routines
build a slice that views a sub-range of another number's limbs without copying
them, via the private window-view constructor. When a
`uinteger_t` is a normal owned value, `_value` aliases its own
`_value_instance`; when it is a slice, `_value` aliases the source's container
and the window narrows it. `_carry` records a pending most-significant carry
limb so an operation can defer materializing it.

Leading-zero limbs are stripped by `trim()` so the limb
count tracks the significant magnitude and `size()` reflects it. The value zero
is the empty limb sequence, which is why `operator bool()` is just
`size() != 0`.

## Growth strategy

The vector grows in both directions with a geometric `growth_factor` of 1.5. `grow(n)` reserves `n * 1.5` when it needs more room. Appending to the most significant end is the common case;
prepending (growing backward, toward lower significance) is handled by reserving
extra capacity and shifting within the existing buffer so shifts and aligned
operations don't reallocate on every limb. The 1.5
factor is the usual compromise: amortized O(1) append without the 2x factor's
tendency to leave large unreusable holes.

## Addition and subtraction

Addition and subtraction walk the limbs from least to most significant doing
64-bit add/sub with a carry/borrow propagated limb to limb. Where the compiler
exposes them, the code uses intrinsics (`__addcarry_u64` / `__subborrow_u64` on
MSVC, `__builtin_addcll` / `__builtin_subcll` on clang, `__int128` widening on
GCC) selected by the `HAVE_*` macros at the top of the header
(see the limb primitives). The fallback path
synthesizes carry from the wrapped-sum comparison. Cost is linear in the number
of limbs.

Subtraction is unsigned: a smaller-minus-larger result wraps rather than going
negative, matching how a fixed-width unsigned type would behave for the operand
width involved.

## Limb kernels

The arithmetic runs on raw limb arrays (`digit*` plus an explicit length), in the
`k_*` kernels: add/sub with carry, multiply-by-limb (`k_mul_1`, `k_addmul_1`,
`k_submul_1`), shifts, and the multiplication and division routines below. The
`uinteger_t` operators size and trim the result and call a kernel once, so the
inner loops never touch a `std::vector` or allocate.

## Multiplication

`k_mul` dispatches on the smaller operand's size:

- Below `mul_karatsuba_threshold` limbs (`2048 / digit_bits`, 32 at 64-bit
  limbs) it runs `k_mul_basecase`, schoolbook multiplication that takes two
  limbs of the multiplier per pass over the multiplicand. Both rows' products
  and carries stay in registers, which is about 1.6x faster than one row per
  pass.
- At or above the threshold it runs Karatsuba: split at `h = ceil(n/2)`,
  compute `a0*b0` and `a1*b1` straight into the result, then
  `(a0+a1)(b0+b1) - a0*b0 - a1*b1` in scratch and add it in at limb `h`.
  O(n^1.585).
- When one operand has at least twice the limbs of the other, it multiplies
  the shorter one by successive equal-size chunks of the longer one and adds
  the partial products, so Karatsuba always sees balanced operands.

One scratch buffer, sized up front by `k_mul_scratch_size`, serves the whole
recursion; each level carves its temporaries from it and passes the rest down.

## Division and modulo

`divmod` is the entry point; `operator/` and `operator%` are thin wrappers over
it, so asking for both at once is cheaper than computing each separately.
Division or modulo by zero throws `std::domain_error`.

- A single-limb divisor takes `divmod_small`: one pass from the top limb, each
  step a 2-by-1 division by a precomputed reciprocal (Moller and Granlund,
  "Improved division by invariant integers", 2011), so the loop multiplies
  instead of dividing.
- Larger divisors take schoolbook long division (Knuth's Algorithm D) in
  `k_div_sb`: normalize so the divisor's top bit is set, then estimate each
  quotient limb with the same paper's 3-by-2 division by the top two divisor
  limbs and their reciprocal. The estimate is exact or one too large; one
  multiply-subtract and at most one add-back fix it. O(n*m).

## Base conversion (`str` and parsing)

`str(base)` renders the value in any base from 2 to 36.
For power-of-two bases it takes a fast bit-slicing path: `base_bits(base)`
returns the bits per digit and the routine walks the half-limbs pulling out
`base_bits` at a time, no division required. Other bases work in chunks:
`base_chunk(base)` is the largest power of the base that fits a limb (10^19 at
64-bit limbs), and each `divmod_small` pass by it yields that many digits at
once, extracted from the one-limb remainder. Parsing mirrors it: it accumulates
a chunk of digits in one limb and folds it in with one `mul_add_small` pass.
Output buffers are pre-reserved from `base_size(base)`. Digits above 9 are
lowercase letters via `chr`.

## Complexity summary

- Addition, subtraction: O(n) in limbs.
- Multiplication: O(n*m) schoolbook, O(n^1.585) Karatsuba from 32 limbs (2,048 bits).
- Division, modulo: O(n*m), Algorithm D.
- `str` and parsing for power-of-two bases: O(bits); other bases: O(n^2), one
  limb-chunk pass per 19 decimal digits.
- `bits()`: O(1) given the limb count plus a leading-limb bit scan.

## Design decisions

- Limbs in a `std::vector<uint64_t>`, little-endian, trimmed of leading zeros.
  The empty vector is zero, which keeps `operator bool()` and emptiness checks
  trivial.
- The arithmetic lives in raw-limb kernels that take pointers and lengths, so
  recursion slices operands by pointer and shares one scratch buffer instead of
  building `uinteger_t` temporaries.
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

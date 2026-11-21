# Source layout

`uinteger_t.hh` is the header users include. It holds the license, the standard includes, the
numeric trait specializations and the class's opening and closing, and includes the pieces below.
`test/property.cc` is the property test every `uinteger_t_property*` CTest target builds, and
includes its pieces the same way.

`uinteger_t/config.hh` and `uinteger_t/free_operators.hh` are headers: each includes what it uses
and compiles alone. The rest of `uinteger_t/` is the body of the one class, in `*.inc` files: each
is included inside `class uinteger_t { ... }` at one place in `uinteger_t.hh`, in the order below,
which is the order the code had in the single file. A member function's body cannot move to its
own header without being rewritten as an out-of-class definition, so they are not headers and
cannot compile alone. Include only `uinteger_t.hh`.

## `uinteger_t.hh`

Order of inclusion; every piece ends before the next begins.

| File | Holds |
|---|---|
| `uinteger_t/config.hh` (header) | compiler and platform feature macros (`HAVE_*`), `UINTEGER_T_NOINLINE`, `DIGIT_T`, `HALF_DIGIT_T` and `UINTEGER_T_INLINE_LIMBS` defaults (outside the class) |
| `uinteger_t/container.inc` | the limb container (inline or heap vector of limbs), the result trait, and the value's limb access and iterators |
| `uinteger_t/primitives.inc` | word primitives: bit length, widening multiply, divide, add with carry, subtract with borrow |
| `uinteger_t/kernels.inc` | raw limb kernels: add, subtract, multiply by a limb, shifts, basecase product and square, exact division by a limb |
| `uinteger_t/mul_toom.inc` | multiplication cutoffs and scratch sizes, Toom-3, Toom-4, Karatsuba squaring and the squaring dispatch |
| `uinteger_t/ntt_transform.inc` | the three-prime transform: primes and roots, word primitives, modular products, the transform and its inverse |
| `uinteger_t/ntt_mul.inc` | the transform multiplication (size plan, load, pointwise product, Chinese remainder), Karatsuba and the multiplication dispatch |
| `uinteger_t/div_schoolbook.inc` | division by invariant integers: reciprocals, the 3-by-2 step, schoolbook division and its quotient-only form |
| `uinteger_t/div_recursive.inc` | the recursive (Burnikel-Ziegler) and Newton division kernels, their cutoffs and scratch sizes |
| `uinteger_t/helpers_bitops.inc` | `trim`, digit characters, base tables, and the bitwise and shift operations |
| `uinteger_t/arithmetic.inc` | add, subtract, multiply and divide on values, the division dispatch and the small-divisor path |
| `uinteger_t/radix_tables.inc` | base conversion: digit counts, the radix and power tables and their lazy publication |
| `uinteger_t/radix_encode.inc` | base conversion: digit emission, the splitters and the encoding kernels |
| `uinteger_t/radix_decode.inc` | base conversion: byte stores, digit output and the Horner decoder |
| `uinteger_t/radix_tree.inc` | base conversion: the subquadratic tree and the from-digits decoder |
| `uinteger_t/construct_operators.inc` | constructors, conversions and the member operators |
| `uinteger_t/digits_str.inc` | value access, `bits`, `digits`, `from_digits`, parsing and the string forms |
| `uinteger_t/free_operators.hh` (header) | the operators with an integral left operand, the stream operator and the `std` overloads (after the class) |

## `test/property.cc`

`test/property.cc` keeps `main` and includes the pieces. The pieces hold the static checks, in the
order they were written; each is a header that includes the pieces it uses (and `common.hh` the
standard headers, the `UINTEGER_T_PUBLIC_IMPLEMENTATION` define and `uinteger_t.hh`) and compiles
alone. They are part of the one translation unit, so every CTest target keeps building
`test/property.cc` and `ctest -N` lists the same tests.

| File | Holds |
|---|---|
| `test/property/common.hh` | the generator, the failure counter and `CHECK`, 32-bit word helpers, the reference multiply and the guard-limb buffers |
| `test/property/kernels.hh` | multiplication kernels on dirty buffers with guard limbs, Toom-4 patterns and size classes |
| `test/property/digits_reference.hh` | division identities, `digits()` against repeated division, chunk checks and the radix tables |
| `test/property/radix_kernels.hh` | the radix encoding kernel and every digit splitter against the general one |
| `test/property/digits_decode.hh` | the Horner decoder bounds, digit errors and values, size guards and random digit round trips |
| `test/property/shapes.hh` | the transform, recursive and Newton division kernels at the shapes that reach each path |
| `test/property/operators.hh` | shifts, the public operator forms and aliasing |
| `test/property/repr.hh` | the limb container representation, leading-zero bytes and the byte forms |

# Source layout

`uinteger_t.hh` is the header users include. It holds the license, the standard includes, the
numeric trait specializations and the class's opening and closing, and includes the pieces below.
`test/property.cc` is the property test every `uinteger_t_property*` CTest target builds, and
includes its pieces the same way.

The pieces of the class body (`uinteger_t/*.hh`) are not headers of their own: each is included
inside `class uinteger_t { ... }` at one place in `uinteger_t.hh`, in the order below, which is the
order they had in the single file. Include only `uinteger_t.hh`. A piece that needs a name defined
before it relies on an earlier piece in that order.

## `uinteger_t.hh`

Order of inclusion; every piece ends before the next begins.

| File | Holds |
|---|---|
| `uinteger_t/config.hh` | compiler and platform feature macros (`HAVE_*`), `UINTEGER_T_NOINLINE`, `DIGIT_T`, `HALF_DIGIT_T` and `UINTEGER_T_INLINE_LIMBS` defaults (outside the class) |
| `uinteger_t/container.hh` | the limb container (inline or heap vector of limbs), the result trait, and the value's limb access and iterators |
| `uinteger_t/primitives.hh` | word primitives: bit length, widening multiply, divide, add with carry, subtract with borrow |
| `uinteger_t/kernels.hh` | raw limb kernels: add, subtract, multiply by a limb, shifts, basecase product and square, exact division by a limb |
| `uinteger_t/mul_toom.hh` | multiplication cutoffs and scratch sizes, Toom-3, Toom-4, Karatsuba squaring and the squaring dispatch |
| `uinteger_t/ntt_transform.hh` | the three-prime transform: primes and roots, word primitives, modular products, the transform and its inverse |
| `uinteger_t/ntt_mul.hh` | the transform multiplication (size plan, load, pointwise product, Chinese remainder), Karatsuba and the multiplication dispatch |
| `uinteger_t/div_schoolbook.hh` | division by invariant integers: reciprocals, the 3-by-2 step, schoolbook division and its quotient-only form |
| `uinteger_t/div_recursive.hh` | the recursive (Burnikel-Ziegler) and Newton division kernels, their cutoffs and scratch sizes |
| `uinteger_t/helpers_bitops.hh` | `trim`, digit characters, base tables, and the bitwise and shift operations |
| `uinteger_t/arithmetic.hh` | add, subtract, multiply and divide on values, the division dispatch and the small-divisor path |
| `uinteger_t/radix_tables.hh` | base conversion: digit counts, the radix and power tables and their lazy publication |
| `uinteger_t/radix_encode.hh` | base conversion: digit emission, the splitters and the encoding kernels |
| `uinteger_t/radix_decode.hh` | base conversion: byte stores, digit output and the Horner decoder |
| `uinteger_t/radix_tree.hh` | base conversion: the subquadratic tree and the from-digits decoder |
| `uinteger_t/construct_operators.hh` | constructors, conversions and the member operators |
| `uinteger_t/digits_str.hh` | value access, `bits`, `digits`, `from_digits`, parsing and the string forms |
| `uinteger_t/free_operators.hh` | the operators with an integral left operand, the stream operator and the `std` overloads (after the class) |

## `test/property.cc`

`test/property.cc` keeps the standard includes, the `UINTEGER_T_PUBLIC_IMPLEMENTATION` define and
`main`. The pieces hold the static checks, in the order they were written; they are part of that
one translation unit, so every CTest target keeps building `test/property.cc` and `ctest -N` lists
the same tests.

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

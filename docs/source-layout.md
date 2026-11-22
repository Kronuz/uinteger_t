# Source layout

## `uinteger_t.hh` and `uinteger_t/`

The library is `uinteger_t.hh` and the headers in `uinteger_t/`. Users include `uinteger_t.hh`
only; it holds the platform macros, the class `uinteger_t` and, after the class, an include of each
family header. The class body holds the data members, the nested types and a declaration of every
member function, each with its comment. Every member function is defined out of the class, `inline`,
in the header of its algorithm family. Each family header includes `uinteger_t.hh` (whose include
guard makes that a no-op inside the umbrella) and compiles alone.

| File | Holds |
|---|---|
| `uinteger_t.hh` | platform and feature macros, the standard-trait specializations, the class with its data members, nested types, cutoffs and member declarations, and the includes below |
| `uinteger_t/container.hh` | the limb container (`uinteger_t::container`) and the value's access to its limbs: storage, growth, iteration, `trim`, `value`, `operator[]` and `bits` |
| `uinteger_t/word.hh` | word primitives: bit length, widening multiply, two-by-one divide, add with carry and subtract with borrow |
| `uinteger_t/kernels.hh` | limb kernels over `digit*` arrays: add, subtract, multiply by a limb, shifts, the basecase products and exact division |
| `uinteger_t/mul.hh` | the multiplication dispatch: algorithm choice, scratch sizes, Karatsuba and the squaring dispatch |
| `uinteger_t/mul_toom.hh` | Toom-3 and Toom-4: evaluation, interpolation and the products |
| `uinteger_t/ntt_transform.hh` | the three-prime number-theoretic transform: primes, 64-bit word products, modular arithmetic and the transforms |
| `uinteger_t/ntt_mul.hh` | multiplication through the transform: size plan, load, pointwise products and the Chinese remainder step |
| `uinteger_t/div_schoolbook.hh` | schoolbook division by invariant integers: reciprocals, the 3-by-2 step and the schoolbook kernels |
| `uinteger_t/div_recursive.hh` | recursive (Burnikel-Ziegler) and Newton division |
| `uinteger_t/bitwise.hh` | bitwise operations and shifts on values |
| `uinteger_t/arithmetic.hh` | add, subtract, multiply and divide on values, Algorithm D, and the small-divisor paths |
| `uinteger_t/radix_tables.hh` | base conversion tables: digit characters, base sizes, divisor magic, radix contexts and their lazy publication |
| `uinteger_t/radix_encode.hh` | base conversion out: digit emission, chunk splitters and the encoding kernels |
| `uinteger_t/radix_decode.hh` | base conversion in and the byte forms: byte stores, digit output and the Horner decoder |
| `uinteger_t/radix_tree.hh` | subquadratic base conversion: the power tree, the tree encoder and the from-digits decoder |
| `uinteger_t/digits.hh` | the digit and string forms: `digits`, `from_digits`, `str`, parsing, `bin`, `oct`, `hex` and `raw` |
| `uinteger_t/operators.hh` | constructors, conversions and the member operators, and the free operators with an integral left operand, `std::to_string` and the stream operator |

The family headers are included in this order, and a definition's family is its algorithm, not
its caller: `k_mul_basecase` is a kernel, while `k_mul` is the multiplication dispatch. Pieces
under about 100 lines join a neighbor: Karatsuba joins the dispatch, and recursive division joins
Newton's.

## `test/property.cc`

`test/property.cc` keeps `main` and includes the pieces below. The pieces hold the static checks, in
the order they were written; each is a header that includes the pieces it uses (and `common.hh` the
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

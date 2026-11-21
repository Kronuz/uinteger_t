# Source layout

## `uinteger_t.hh`: kept whole (about 6,000 lines)

`uinteger_t.hh` is the whole library: one class, `uinteger_t`, header-only, and the contract users
rely on is that one file. The class body is one unit (the limb container, the word and limb
kernels, the multiplication and division algorithms, base conversion and the operators share
private members and constants, and every member function sits inside the class), so it cannot be
cut without fragments that neither read nor compile alone. Its outside-the-class parts (the
platform macros, 77 lines, and the operators with an integral left operand, 160 lines) are too
small and too tied to the class to be files a maintainer opens alone. Rewriting the members out of
line is a separate decision.

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

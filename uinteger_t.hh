/*
uinteger_t.hh
An arbitrary precision unsigned integer type for C++

Copyright (c) 2017,2019 German Mendez Bravo (Kronuz) @ german dot mb at gmail.com
Copyright (c) 2013 - 2017 Jason Lee @ calccrypto at gmail.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

With much help from Auston Sterling

Thanks to Stefan Deigmüller for finding
a bug in operator*.

Thanks to François Dessenne for convincing me
to do a general rewrite of this class.

Germán Mández Bravo (Kronuz) converted Jason Lee's uint128_t
to header-only and extended to arbitrary bit length.
*/

#ifndef __uint_t__
#define __uint_t__

#include <cassert>
#include <cmath>
#include <atomic>
#include <limits>
#include <memory>
#include <vector>
#include <iterator>
#include <string>
#include <utility>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <functional>
#include <type_traits>

#include "uinteger_t/config.hh"

class uinteger_t;

// uinteger_t is meant to drop into generic numeric code, so it deliberately
// specializes the standard numeric traits to advertise itself as an unsigned
// integer (so a `template <class T, enable_if_t<is_integral_v<T>>>` accepts it).
// The standard reserves these templates, and a recent libc++ marks them
// [[no_specializations]], making the specialization ill-formed (surfaced as
// -Winvalid-specialization). This is a knowing exception, so suppress that one
// diagnostic -- but ONLY where the compiler defines it: guarding on __has_warning
// avoids a -Wunknown-warning-option warning on toolchains (e.g. Apple clang 17)
// that accept the specialization and never had that flag.
#if defined(__clang__)
#  if __has_warning("-Winvalid-specialization")
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Winvalid-specialization"
#    define UINTEGER_T_RESTORE_DIAG
#  endif
#endif
namespace std {
	template <> struct is_arithmetic <uinteger_t> : std::true_type {};
	template <> struct is_integral   <uinteger_t> : std::true_type {};
	template <> struct is_unsigned   <uinteger_t> : std::true_type {};
}
#ifdef UINTEGER_T_RESTORE_DIAG
#  pragma clang diagnostic pop
#  undef UINTEGER_T_RESTORE_DIAG
#endif

// A test defines UINTEGER_T_PUBLIC_IMPLEMENTATION (UINT_T_PUBLIC_IMPLEMENTATION is the older
// spelling) to reach the private members of the class. UINTEGER_T_PRIVATE stands for `private`
// in the class below and is undefined after it.
#if defined UINTEGER_T_PUBLIC_IMPLEMENTATION || defined UINT_T_PUBLIC_IMPLEMENTATION
#  define UINTEGER_T_PRIVATE public
#else
#  define UINTEGER_T_PRIVATE private
#endif

class uinteger_t {
public:
	using digit = DIGIT_T;
	using half_digit = HALF_DIGIT_T;

	static constexpr std::size_t digit_octets = sizeof(digit);             // number of octets per digit
	static constexpr std::size_t digit_bits = digit_octets * 8;            // number of bits per digit
	static constexpr std::size_t half_digit_octets = sizeof(half_digit);   // number of octets per half_digit
	static constexpr std::size_t half_digit_bits = half_digit_octets * 8;  // number of bits per half_digit

#include "uinteger_t/container.hh"
#include "uinteger_t/primitives.hh"
#include "uinteger_t/kernels.hh"
#include "uinteger_t/mul_toom.hh"
#include "uinteger_t/ntt_transform.hh"
#include "uinteger_t/ntt_mul.hh"
#include "uinteger_t/div_schoolbook.hh"
#include "uinteger_t/div_recursive.hh"
#include "uinteger_t/helpers_bitops.hh"
#include "uinteger_t/arithmetic.hh"
#include "uinteger_t/radix_tables.hh"
#include "uinteger_t/radix_encode.hh"
#include "uinteger_t/radix_decode.hh"
#include "uinteger_t/radix_tree.hh"
#include "uinteger_t/construct_operators.hh"
#include "uinteger_t/digits_str.hh"
};

#undef UINTEGER_T_PRIVATE

#include "uinteger_t/free_operators.hh"

#endif

// Compiler and platform feature detection, and the limb type and inline capacity defaults. Included by uinteger_t.hh.

// Compatibility inlines
#ifndef __has_builtin         // Optional of course
#define __has_builtin(x) 0    // Compatibility with non-clang compilers
#endif

#if defined _MSC_VER
#  include <intrin.h>
#  if defined _M_X64
#    define HAVE___ADDCARRY_U64
#    define HAVE___SUBBORROW_U64
#    define HAVE___UMUL128
#    if _MSC_VER >= 1920
#      define HAVE___UDIV128
#    endif
#  elif defined _M_ARM64
#    define HAVE___UMULH
#  endif
#endif

#if (defined(__clang__) && __has_builtin(__builtin_clzll)) || (defined(__GNUC__ ) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)))
#  define HAVE____BUILTIN_CLZLL
#endif
#if (defined(__clang__) && __has_builtin(__builtin_clzl)) || (defined(__GNUC__ ) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)))
#  define HAVE____BUILTIN_CLZL
#endif
#if (defined(__clang__) && __has_builtin(__builtin_clz)) || (defined(__GNUC__ ) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)))
#  define HAVE____BUILTIN_CLZ
#endif
#if (defined(__clang__) && __has_builtin(__builtin_addcll))
#  define HAVE____BUILTIN_ADDCLL
#endif
#if (defined(__clang__) && __has_builtin(__builtin_addcl))
#  define HAVE____BUILTIN_ADDCL
#endif
#if (defined(__clang__) && __has_builtin(__builtin_addc))
#  define HAVE____BUILTIN_ADDC
#endif
#if (defined(__clang__) && __has_builtin(__builtin_subcll))
#  define HAVE____BUILTIN_SUBCLL
#endif
#if (defined(__clang__) && __has_builtin(__builtin_subcl))
#  define HAVE____BUILTIN_SUBCL
#endif
#if (defined(__clang__) && __has_builtin(__builtin_subc))
#  define HAVE____BUILTIN_SUBC
#endif

// Keeps the allocating paths of the limb container out of line, so its fast paths inline.
#if defined __GNUC__ || defined __clang__
#define UINTEGER_T_NOINLINE __attribute__((noinline))
#elif defined _MSC_VER
#define UINTEGER_T_NOINLINE __declspec(noinline)
#else
#define UINTEGER_T_NOINLINE
#endif

// Define UINTEGER_T_NO_INT128 to test the portable 64-bit limb paths.
#if defined __SIZEOF_INT128__ && !defined UINTEGER_T_NO_INT128
#define HAVE____INT128_T
#endif


#ifndef DIGIT_T
#define DIGIT_T        std::uint64_t
#endif

#ifndef HALF_DIGIT_T
#define HALF_DIGIT_T   std::uint32_t
#endif

// A value of up to this many limbs lives inside the object, with no heap block; the default holds
// the same number of bytes at every limb width.
#ifndef UINTEGER_T_INLINE_LIMBS
#define UINTEGER_T_INLINE_LIMBS   (64 / sizeof(DIGIT_T))
#endif

#pragma once

//#ifndef _DEBUG
//#   define NDEBUG
//#endif

#ifdef _MSC_BUILD
#   if !(defined __GNUC__ || defined __clang__)
//      Suppressing some MSVS warnings
#       pragma warning(disable : 4116) // "Unnamed type definition in parentheses"
#       pragma warning(disable : 4200) // "Zero-sized array in structure/union"
#       pragma warning(disable : 4201) // "Nameless structure/union"
#       pragma warning(disable : 4204) // "Non-constant aggregate initializer"
#       pragma warning(disable : 4221) // "Initialization by using the address of automatic variable"
//#       pragma warning(disable : 4706) // "Assignment within conditional expression"
#       define restrict __restrict
#       define inline __inline
#       define alignof __alignof
#       define _Static_assert static_assert
#   endif
#endif

#if defined __GNUC__ || defined __clang__
#   include <stdalign.h>
#endif



#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MAX(a, b) \
    ((a) >= (b) ? (a) : (b))
#define MIN(a, b) \
    ((a) <= (b) ? (a) : (b))
#define DIST(a, b) \
    ((a) >= (b) ? (a) - (b) : (b) - (a))
#define ABS(a) \
    ((a) >= 0 ? (a) : -(a))
#define CLAMP(x, a, b) \
    ((x) >= (a) ? (x) <= (b) ? (x) : (b) : (a))
#define SIGN(x, y) \
    ((x) > (y) ? 1 : (x) == (y) ? 0 : -1)

// Number of elements of array
#define countof(ARR) \
    (sizeof(ARR) / sizeof((ARR)[0]))

// Length of string literal (without null-terminator)
#define strlenof(STR) \
    (countof((STR)) - 1)

// Helper macros evaluating and inserting the count of arguments
#define INIT(T, ...) \
    { __VA_ARGS__ }, countof(((T []) { __VA_ARGS__ }))
#define ARG(T, ...) \
    ((T []) { __VA_ARGS__ }), countof(((T []) { __VA_ARGS__ }))
#define ARG_SIZE(...) \
    ARG(size_t, __VA_ARGS__)

// Convert value (which is often represented by a macro) to string literal
#define TOSTRING_EXPAND(Z) #Z
#define TOSTRING(Z) TOSTRING_EXPAND(Z)

#define STRI(STR) { STR, strlenof(STR) }
#define ARRI(ARR) { ARR, countof(ARR) }

// In the case of compound literal extra parentheses should be added
#define CLII(...) ARRI((__VA_ARGS__))

// Common value for sizes of temporary string and buffer used for formatting messages.
// The size should be adequate to handle all format string appearing in the program. 
// If this size is too small, some messages may become truncated, but no buffer overflows will occur.
// Warning! 'TEMP_STR' should be an explicit number in order to be used with 'TOSTRING' macro.
#define TEMP_STR 255
#define TEMP_BUFF (TEMP_STR + 1)
#define TEMP_BUFF_LARGE (TEMP_BUFF << 1)

_Static_assert(TEMP_BUFF > TEMP_STR, "'TEMP_BUFF' must be greater than 'TEMP_STR'!");

// Common value for the size of temporary buffer used for file writing
#define BLOCK_WRITE 4096

// Common value for the size of temporary buffer used for file reading
// Warning! Some routines make special assumption about this value!
#define BLOCK_READ 4096

// Constants for linear congruential generator
#if defined _M_X64 || defined __x86_64__
// These constants are suggested by D. E. Knuth
#   define LCG_MUL 6364136223846793005llu
#   define LCG_INC 1442695040888963407llu
#elif defined _M_IX86 || defined __i386__
#   define LCG_MUL 22695477u
#   define LCG_INC 1u
#endif

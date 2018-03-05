#pragma once

#ifndef _DEBUG
#   define NDEBUG
#endif

#ifdef _MSC_VER

#   define _CRT_SECURE_NO_WARNINGS

// Suppressing some MSVS warnings
#   pragma warning(disable : 4116) // "Unnamed type definition in parentheses"
#   pragma warning(disable : 4200) // "Zero-sized array in structure/union"
#   pragma warning(disable : 4201) // "Nameless structure/union"
#   pragma warning(disable : 4204) // "Non-constant aggregate initializer"
#   pragma warning(disable : 4221) // "Initialization by using the address of automatic variable"

#   define restrict __restrict
#   define inline __inline
#   define forceinline __forceinline 
#   define alignof __alignof

#elif defined __GNUC__ || defined __clang__

#   define max(a, b) (((a) > (b)) ? (a) : (b))
#   define min(a, b) (((a) < (b)) ? (a) : (b))

// Required for some POSIX only functions (see "Wrappers.h")
#   define _POSIX_C_SOURCE 200112L
#   define _DARWIN_C_LEVEL 200112L

// Required for the 'fseeko' and 'ftello' functions
#   define _FILE_OFFSET_BITS 64

#   define forceinline inline __attribute__((always_inline))
#   define static_assert _Static_assert

#   include <stdalign.h>

#endif

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SIZE_C(X) ((size_t) (X ## ull))

// Error label layout common for the all program (empty argument is also valid)
#define ERR(...) _error ## __VA_ARGS__

// Number of elements of array
#define countof(ARR) (sizeof (ARR) / sizeof (ARR)[0])

// Length of string literal (without null-terminator)
#define strlenof(STR) (countof(STR) - 1)

// Accessing structure member by offset. Requires type casting
#define memberof(STRUCT, OFFSET) ((char *) (STRUCT) + (OFFSET))

// Increment pointer by bytes 
#define ptrinc(PTR, INC) (*(char **) &(PTR) += (INC))

// Convert value (which is often represented by a macro) to string literal
#define TOSTRING_HLP(Z) #Z
#define TOSTRING(Z) TOSTRING_HLP(Z)

#define STRI(STR) { STR, strlenof(STR) }
#define ARRI(ARR) { ARR, countof(ARR) }

// In the case of compound literal extra parentheses should be added
#define CLII(...) ARRI((__VA_ARGS__))

// Counting number of bytes needed to store the number of bits 
// Warning! This is not portable under machines with exotic number of bits in the byte
static_assert(CHAR_BIT == 8, "The 'char' type should contain exactly 8 bits!");

#define BYTE_CNT(BIT) (((BIT) + 7) >> 3)

// All NaN's produced by the program have only one possible representation. On arrays 'memset' can be used directly
// Warning! This is not portable under the machines with an exotic 'double' type
static_assert(sizeof (double) == sizeof (uint64_t), "The size of 'double' type should be equal to the size of 'uint64_t'!");

#define NaN (((union { double val; uint64_t bin; }) { .bin = UINT64_MAX }).val)

// Memory bitwise set and reset operations
// <string.h> is required
#define MEMORY_SET(ARR, CNT) ((void) memset((ARR), (UINT8_MAX), (CNT) * sizeof (ARR)[0]))
#define MEMORY_RESET(ARR, CNT) ((void) memset((ARR), 0, (CNT) * sizeof (ARR)[0]))
#define CLEAR(X) MEMORY_RESET((X), 1)

// Common value for sizes of temporary string and buffer used for formatting messages.
// The size should be adequate to handle all format string appearing in the program. 
// If this size is too small, some messages may become truncated, but no buffer overflows will occur.
// Warning! 'TEMP_STR' should be an explicit number in order to be used with 'TOSTRING' macro.
#define TEMP_STR 255
#define TEMP_BUFF (TEMP_STR + 1)
#define TEMP_BUFF_LARGE (TEMP_BUFF << 1)

static_assert(TEMP_BUFF > TEMP_STR, "'TEMP_BUFF' must be greater than 'TEMP_STR'!");

// Common value for the size of temporary buffer used for file writing
#define BLOCK_WRITE 4096

// Common value for the size of temporary buffer used for file reading
// Warning! Some routines make special assumption about this value!
#define BLOCK_READ 4096

// Applying expression to multiple objects
#define APPLY(EXPR, TYPE, ...) \
    do { for (size_t i = 0; i < countof(((TYPE[]) { __VA_ARGS__ })); i++) { EXPR((TYPE[]) { __VA_ARGS__ }[i]); } } while (0)

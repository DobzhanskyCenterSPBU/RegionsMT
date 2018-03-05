#pragma once

// Wrappers for the non-standard types and library functions used in the program 

#if defined _MSC_VER

// Wrappers for <stdio.h>
#   define fseek64(F, OFFSET, ORIGIN) (_fseeki64((F), (int64_t) (OFFSET), (ORIGIN)))
#   define ftell64(F) ((size_t) _ftelli64((F)))

#elif defined __GNUC__ || defined __clang__

// Wrappers for <stdio.h>
// Warning: "Common.h" should be included prior to the <stdio.h>!
#   define fseek64(F, OFFSET, ORIGIN) (fseeko((F), (int64_t) (OFFSET), (ORIGIN)))
#   define ftell64(F) ((size_t) ftello((F)))

// Wrappers for <string.h>
#   define strerror_s(BUFF, SZ, ERRNO) strerror_r((ERRNO), (BUFF), (SZ))
#   define errno_t int 

// Wrappers for <time.h>
#   define localtime_s(TM, T) localtime_r((T), (TM))

#endif

#pragma once

///////////////////////////////////////////////////////////////////////////////
//
//  Wrappers for the non-portable features of the C standard library
//  Warning! This file should be included prior to any standard library header!
//

#if defined _MSC_BUILD
#   define _CRT_SECURE_NO_WARNINGS

#   define _CRTDBG_MAP_ALLOC
#   include <crtdbg.h>

#   include <malloc.h>
#   define Alloca(SIZE) (_alloca((SIZE)))

#   include <errno.h>
typedef errno_t Errno_t;

#elif defined __GNUC__ || defined __clang__
// Required for some POSIX only functions
#   define _POSIX_C_SOURCE 200112L
#   define _DARWIN_C_SOURCE

// Required for the 'fseeko' and 'ftello' functions
#   define _FILE_OFFSET_BITS 64

#   include <alloca.h>
#   define Alloca(SIZE) (alloca((SIZE)))

#   include <errno.h>
typedef int Errno_t;

#endif

#include "common.h"

#include <stdio.h>
#include <time.h>

// Aligned memory allocation/deallocation
void *Aligned_alloc(size_t, size_t);
void Aligned_free(void *);

// File operations
int Fclose(FILE *); // Tolerant to the 'NULL' and 'std*' streams
int Fseeki64(FILE *, int64_t, int);
int64_t Ftelli64(FILE *);

// Error and time
Errno_t Strerror_s(char *, size_t, Errno_t);
Errno_t Localtime_s(struct tm *result, const time_t *time);

// Ordinary string processing functions
int Stricmp(const char *, const char *);
int Strnicmp(const char *, const char *, size_t);
size_t Strnlen(const char *, size_t);
void *Memrchr(void const *, int, size_t);

int64_t file_get_size(FILE *);
size_t get_processor_count(void);
size_t get_page_size(void);
size_t get_process_id(void);
uint64_t get_time(void);

#pragma once

#include "Common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

inline int strncmpci(const char *a, const char *b, size_t len)
{
    int res = 0;
    extern const char lowermap[];
    for (; !res && len && *a && *b; res = lowermap[(unsigned char) *a] - lowermap[(unsigned char) *b], len--, a++, b++);
    return res;
}

inline int strcmpci(const char *a, const char *b)
{
    int res = 0;
    extern const char lowermap[];
    for (; !res && *a && *b; res = lowermap[(unsigned char) *a] - lowermap[(unsigned char) *b], a++, b++);
    return res;
}

#define strtouint32(str, trm, radix) ((uint32_t) strtoul((str), (trm), (radix)))
#define strtouint64(str, trm, radix) ((uint64_t) strtoull((str), (trm), (radix)))
#define strtosz(str, trm, radix) ((size_t) strtouint64((str), (trm), (radix)))

typedef bool (*readHandlerCallback)(const char *, size_t, void *, void *);
typedef bool (*writeHandlerCallback)(char **, size_t *, size_t, void *, void *);

typedef union { readHandlerCallback read; writeHandlerCallback write; } handlerCallback;

typedef struct
{
    ptrdiff_t set;
    size_t pos;
} handlerContext;

typedef struct
{
    char **strtbl;
    size_t *strtblcnt, *strtblcap;
} strTableHandlerContext;

bool strPtrHandler(const char *, size_t, const char **, void *);
bool strHandler(const char *, size_t, char **, void *);
bool strTableHandler(const char *, size_t, ptrdiff_t *, strTableHandlerContext *);
bool boolHandler(const char *, size_t, void *, handlerContext *);
bool uint16Handler(const char *, size_t, uint16_t *ptr, handlerContext *context);
bool uint32Handler(const char *, size_t, uint32_t *ptr, handlerContext *context);
bool float64Handler(const char *, size_t, double *, handlerContext *);
bool emptyHandler(const char *, size_t, void *, handlerContext *);

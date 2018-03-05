#pragma once

#include "Common.h"
#include "Debug.h"
#include "x86_64/Tools.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _MSC_VER
#   include <malloc.h>
#elif defined __GNUC__ || defined __clang__
#   include <alloca.h>
#endif

#define SAFE_FREE(X) \
    do { free(*(X)); *(X) = NULL; } while (0)

#define SAFE_FREE_LIST(...) \
    APPLY(SAFE_FREE, void **, __VA_ARGS__)

inline bool arrayInit(void **parr, size_t cap, size_t sz)
{
    size_t tot = cap;

    if (sizeFusedMul(&tot, sz)) // Test for overflow
    {
        *parr = malloc(tot);
        if (!tot || *parr) return 1;
    }
    else errno = ENOMEM;
    
    return 0;
}

inline bool arrayShrink(void **parr, size_t cap, size_t sz)
{
    size_t tot = cap * sz;
    void *arr = realloc(*parr, tot);

    if (!tot || arr)
    {
        *parr = arr;
        return 1;
    }

    return 0;
}

inline bool flexArrayInit(void **parr, size_t cap, size_t sz, size_t structsz)
{
    size_t tot = cap;
    
    if (sizeFusedMulAdd(&tot, sz, structsz)) // Test for overflow
    {
        *parr = malloc(tot);
        if (!tot || *parr) return 1;
    }
    else errno = ENOMEM;
    
    return 0;
}

inline bool flexArrayInitClear(void **parr, size_t cap, size_t sz, size_t structsz)
{
    size_t tot = cap;
    
    if (sizeFusedMulAdd(&tot, sz, structsz)) // Test for overflow
    {
        *parr = calloc(1, tot);
        if (!tot || *parr) return 1;
    }
    else errno = ENOMEM;
    
    return 0;
}

inline bool arrayInitClear(void **parr, size_t cap, size_t sz)
{
    *parr = calloc(cap, sz);
    if (!cap || !sz || *parr) return 1;
    
    return 0;
}

// Procedure for dynamic array management. The array growth model is exponential.
// Handling of 'size_t' overflow is included.
// <stdlib.h> and <errno.h> are required
inline bool dynamicArrayTest(void **parr, size_t *pcap, size_t sz, size_t cnt)
{
    if (cnt > *pcap)
    {
        size_t cap = *pcap ? *pcap : 1;
        while (cap && cap < cnt) cap <<= 1;
        
        if (cap)
        {
            size_t tot = cap;

            if (sizeFusedMul(&tot, sz)) // Test for overflow
            {
                void *arr = realloc(*parr, tot);

                if (arr)
                {
                    *parr = arr;
                    *pcap = cap;

                    return 1;
                }
            }
            else errno = ENOMEM;
        }
        else errno = ENOMEM;

        return 0;
    }

    return 1;
}

// Procedure used to explicitly set the length of array. Zero length is handled properly.
inline bool dynamicArrayFinalize(void **parr, size_t *pcap, size_t sz, size_t cnt)
{
    if (*pcap > cnt)
    {
        size_t tot = cnt * sz;
        void *arr = realloc(*parr, tot);
        
        if (!tot || arr)
        {
            *parr = arr;
            *pcap = cnt;

            return 1;
        }

        return 0;
    }

    return 1;
}

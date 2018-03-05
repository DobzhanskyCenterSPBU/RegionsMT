#include "Common.h"
#include "Debug.h"
#include "Memory.h"
#include "Sort.h"
#include "x86_64/Tools.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    compareCallbackStable comp;
    void *context;
} transition;

static bool genericComp(const uintptr_t *a, const uintptr_t *b, transition *trans)
{
    int8_t res = trans->comp((const void *) *a, (const void *) *b, trans->context);
    if (res > 0 || (!res && *a > *b)) return 1;
    return 0;
}

uintptr_t *ordersStable(void *arr, size_t cnt, size_t sz, compareCallbackStable comp, void *context)
{
    uintptr_t *restrict res = NULL;
    if (!arrayInit((void **) &res, cnt, sizeof *res)) return NULL;

    for (size_t i = 0; i < cnt; res[i] = (uintptr_t) arr + i * sz, i++);
    quickSort(res, cnt, sizeof *res, (compareCallback) genericComp, &(transition) { .comp = comp, .context = context });
    for (size_t i = 0; i < cnt; res[i] = (res[i] - (uintptr_t) arr) / sz, i++);

    return res;
}

uintptr_t *ordersStableUnique(void *arr, size_t cnt, size_t sz, compareCallbackStable comp, void *context, size_t *pucnt)
{
    uintptr_t *restrict res = NULL;
    if (!arrayInit((void **) &res, cnt, sizeof *res)) return NULL;
    
    for (size_t i = 0; i < cnt; res[i] = (uintptr_t) arr + i * sz, i++);
    quickSort(res, cnt, sizeof *res, (compareCallback) genericComp, &(transition) { .comp = comp, .context = context });

    uintptr_t tmp = 0;
    size_t ucnt = 0;
    
    if (cnt) tmp = res[0], res[ucnt++] = (tmp - (uintptr_t) arr) / sz;
    
    for (size_t i = 1; i < cnt; i++)
        if (comp((const void *) tmp, (const void *) res[i], context)) tmp = res[i], res[ucnt++] = (tmp - (uintptr_t) arr) / sz;
    
    if (ucnt < cnt && !arrayShrink((void **) &res, ucnt, sizeof *res)) goto ERR();
    
    *pucnt = ucnt;
    return res;
    
ERR():
    free(res);
    *pucnt = 0;
    return NULL;
}


uintptr_t *ordersInvert(const uintptr_t *restrict arr, size_t cnt)
{
    uintptr_t *restrict res = NULL;
    if (!arrayInit((void **) &res, cnt, sizeof *res)) return NULL;
    
    for (size_t i = 0; i < cnt; res[arr[i]] = i, i++);
    return res;
}

bool createRanks(uintptr_t *restrict arr, uintptr_t base, size_t cnt, size_t sz)
{
    uint8_t *restrict bits = NULL;
    if (!arrayInitClear((void **) &bits, BYTE_CNT(cnt), sizeof *bits)) return 0;

    for (size_t i = 0; i < cnt; i++)
    {
        size_t j = i;
        uintptr_t k = (arr[i] - base) / sz;

        while (!bitTest(bits, j))
        {
            uintptr_t l = (arr[k] - base) / sz;

            bitSet(bits, j);
            arr[k] = j;
            j = k;
            k = l;
        }
    }

    free(bits);
    return 1;
}

uintptr_t *ranksStable(void *arr, size_t cnt, size_t sz, compareCallbackStable comp, void *context)
{
    uintptr_t *restrict res = NULL;
    if (!arrayInit((void **) &res, cnt, sizeof *res)) return NULL;
    
    for (size_t i = 0; i < cnt; res[i] = (uintptr_t) arr + i * sz, i++);
    quickSort(res, cnt, sizeof *res, (compareCallback) genericComp, &(transition) { .comp = comp, .context = context });
    if (createRanks(res, (uintptr_t) arr, cnt, sz)) return res;
    
    free(res);
    return NULL;
}

#define STACKSZ 96
#define CUTOFF 50

// These constants for linear congruential generator are suggested by D. E. Knuth
#define RAND_MUL 6364136223846793005llu
#define RAND_INC 1442695040888963407llu

void quickSort(void *restrict arr, size_t cnt, size_t sz, compareCallback comp, void *context)
{
    uint8_t frm = 0;
    size_t stck[STACKSZ]; // This is a sufficient with 99.99% probability stack space
    size_t left = 0, tot = cnt * sz;
    size_t rnd = (size_t) arr; // Random seed is just the array initial address

    char *restrict swp = alloca(sz); // Place for swap allocated on stack

    for (;;)
    {
        for (; left + sz < tot; tot += sz)
        {
            if (tot < CUTOFF * sz + left) // Insertion sort for small ranges. Cutoff estimation is purely empirical
            {
                for (size_t i = left + sz; i < tot; i += sz)
                {
                    size_t j = i;
                    memcpy(swp, (char *) arr + j, sz);

                    for (; j > left && comp((char *) arr + j - sz, swp, context); j -= sz)
                        memcpy((char *) arr + j, (char *) arr + j - sz, sz);

                    memcpy((char *) arr + j, swp, sz);
                }

                break;
            }

            if (frm == countof(stck)) frm = 0, tot = stck[0]; // Practically unfeasible case of stack overflow
            stck[frm++] = tot;

            rnd = rnd * RAND_MUL + RAND_INC;
            size_t pvt = left + (rnd % ((tot - left) / sz)) * sz, tmp = left - sz;

            for (;;) // Partitioning
            {
                while (tmp += sz, comp((char *) arr + pvt, (char *) arr + tmp, context));
                while (tot -= sz, comp((char *) arr + tot, (char *) arr + pvt, context));

                if (tmp >= tot) break;

                memcpy(swp, (char *) arr + tmp, sz);
                memcpy((char *) arr + tmp, (char *) arr + tot, sz);
                memcpy((char *) arr + tot, swp, sz);

                if (tmp == pvt) pvt = tot;
                else if (tot == pvt) pvt = tmp;
            }
        }

        if (!frm) break;

        left = tot;
        tot = stck[--frm];
    }
}

size_t binarySearch(const void *restrict key, const void *restrict list, size_t sz, size_t cnt, compareCallbackStable comp, void *context)
{
    size_t left = 0;
    while (left + 1 < cnt)
    {
        size_t mid = left + ((cnt - left) >> 1);
        if (comp(key, (char *) list + sz * mid, context) >= 0) left = mid;
        else cnt = mid;
    }

    if (left + 1 == cnt && !comp(key, (char *) list + sz * left, context)) return left;
    return SIZE_MAX;
}

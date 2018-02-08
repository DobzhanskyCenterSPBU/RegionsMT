#include "np.h"
#include "ll.h"
#include "memory.h"
#include "sort.h"

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

struct thunk {
    stable_cmp_callback cmp;
    void *context;
};

static bool generic_cmp(const void *a, const void *b, void *Thunk)
{
    struct thunk *restrict thunk = Thunk;
    int res = thunk->cmp(*(const void **) a, *(const void **) b, thunk->context);
    if (res > 0 || (!res && *(const uintptr_t *) a > *(const uintptr_t *) b)) return 1;
    return 0;
}

uintptr_t *orders_stable(void *arr, size_t cnt, size_t sz, stable_cmp_callback cmp, void *context)
{
    uintptr_t *res = NULL;
    if (!array_init_strict(&res, cnt, sizeof(*res), 0, 0)) return NULL;

    for (size_t i = 0; i < cnt; res[i] = (uintptr_t) arr + i * sz, i++);
    quick_sort(res, cnt, sizeof(*res), generic_cmp, &(struct thunk) { .cmp = cmp, .context = context });
    for (size_t i = 0; i < cnt; res[i] = (res[i] - (uintptr_t) arr) / sz, i++);

    return res;
}

uintptr_t *orders_stable_unique(void *arr, size_t *p_cnt, size_t sz, stable_cmp_callback cmp, void *context)
{
    size_t cnt = *p_cnt;
    uintptr_t *res = NULL;
    if (!array_init_strict(&res, cnt, sizeof(*res), 0, 0)) return NULL;
    
    for (size_t i = 0; i < cnt; res[i] = (uintptr_t) arr + i * sz, i++);
    quick_sort(res, cnt, sizeof(*res), generic_cmp, &(struct thunk) { .cmp = cmp, .context = context });

    uintptr_t tmp = 0;
    size_t ucnt = 0;
    
    if (cnt) tmp = res[0], res[ucnt++] = (tmp - (uintptr_t) arr) / sz;
    
    for (size_t i = 1; i < cnt; i++)
        if (cmp((const void *) tmp, (const void *) res[i], context)) tmp = res[i], res[ucnt++] = (tmp - (uintptr_t) arr) / sz;
    
    if (array_resize_strict(&res, cnt, sizeof(*res), 0, ARRAY_RESIZE_REDUCE_ONLY, ARG_S(ucnt)))
    {
        *p_cnt = ucnt;
        return res;
    }

    free(res);
    *p_cnt = 0;
    return NULL;
}

uintptr_t *ranks_from_orders(const uintptr_t *restrict arr, size_t cnt)
{
    uintptr_t *res = NULL;
    if (!array_init_strict(&res, cnt, sizeof(*res), 0, 0)) return NULL;
    
    for (size_t i = 0; i < cnt; res[arr[i]] = i, i++);
    return res;
}

bool ranks_from_orders_inplace(uintptr_t *restrict arr, uintptr_t base, size_t cnt, size_t sz)
{
    uint8_t *bits = NULL;
    if (!array_init_strict(&bits, BYTE_CNT(cnt), sizeof(*bits), 0, 1)) return 0;

    for (size_t i = 0; i < cnt; i++)
    {
        size_t j = i;
        uintptr_t k = (arr[i] - base) / sz;
        while (!bit_test(bits, j))
        {
            uintptr_t l = (arr[k] - base) / sz;
            bit_set(bits, j);
            arr[k] = j;
            j = k;
            k = l;
        }
    }

    free(bits);
    return 1;
}

uintptr_t *ranks_stable(void *arr, size_t cnt, size_t sz, stable_cmp_callback cmp, void *context)
{
    uintptr_t *res = NULL;
    if (!array_init_strict(&res, cnt, sizeof(*res), 0, 0)) return NULL;
    
    for (size_t i = 0; i < cnt; res[i] = (uintptr_t) arr + i * sz, i++);
    quick_sort(res, cnt, sizeof(*res), generic_cmp, &(struct thunk) { .cmp = cmp, .context = context });
    if (ranks_from_orders_inplace(res, (uintptr_t) arr, cnt, sz)) return res;
    
    free(res);
    return NULL;
}

#define QUICK_SORT_STACK_SZ 96
#define QUICK_SORT_CUTOFF 5

typedef void (*cpy_callback)(void *, const void *);
typedef void (*swp_callback)(void *, void *, void *);

void quick_sort(void *restrict arr, size_t cnt, size_t sz, cmp_callback cmp, void *context)
{
    uint8_t frm = 0;
    size_t stk[QUICK_SORT_STACK_SZ]; // This is a sufficient with 99.99% probability stack space
    size_t left = 0, tot = cnt * sz;
    size_t rnd = (size_t) arr; // Random seed is just the array initial address
    char *restrict swp = Alloca(sz); // Place for swap is allocated on the stack

    for (;;)
    {
        for (; left + sz < tot; tot += sz)
        {
            if (tot < QUICK_SORT_CUTOFF * sz + left) // Insertion sort for small ranges. Cutoff estimation is purely empirical
            {
                for (size_t i = left + sz; i < tot; i += sz)
                {
                    size_t j = i;
                    memcpy(swp, (char *) arr + j, sz);

                    for (; j > left && cmp((char *) arr + j - sz, swp, context); j -= sz)
                        memcpy((char *) arr + j, (char *) arr + j - sz, sz);

                    memcpy((char *) arr + j, swp, sz);
                }
                break;
            }

            if (frm == countof(stk)) frm = 0, tot = stk[0]; // Practically unfeasible case of stack overflow
            stk[frm++] = tot;

            rnd = rnd * (size_t) LCG_MUL + (size_t) LCG_INC;
            size_t pvt = left + (rnd % ((tot - left) / sz)) * sz, tmp = left - sz;

            for (;;) // Partitioning
            {
                while (tmp += sz, cmp((char *) arr + pvt, (char *) arr + tmp, context));
                while (tot -= sz, cmp((char *) arr + tot, (char *) arr + pvt, context));

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
        tot = stk[--frm];
    }
}

size_t binary_search(const void *restrict key, const void *restrict list, size_t sz, size_t cnt, stable_cmp_callback cmp, void *context)
{
    size_t left = 0;
    while (left + 1 < cnt)
    {
        size_t mid = left + ((cnt - left) >> 1);
        if (cmp(key, (char *) list + sz * mid, context) >= 0) left = mid;
        else cnt = mid;
    }
    if (left + 1 == cnt && !cmp(key, (char *) list + sz * left, context)) return left;
    return SIZE_MAX;
}

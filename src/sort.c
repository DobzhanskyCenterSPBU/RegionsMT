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

static bool generic_cmp(const void *A, const void *B, void *Thunk)
{
    struct thunk *restrict thunk = Thunk;
    const void **restrict a = (void *) A, **restrict b = (void *) B;
    int res = thunk->cmp(*a, *b, thunk->context);
    if (res > 0 || (!res && *a > *b)) return 1;
    return 0;
}

uintptr_t *orders_stable(const void *arr, size_t cnt, size_t sz, stable_cmp_callback cmp, void *context)
{
    uintptr_t *res = NULL;
    if (!array_init(&res, NULL, cnt, sizeof(*res), 0, ARRAY_STRICT)) return NULL;

    for (size_t i = 0; i < cnt; res[i] = (uintptr_t) arr + i * sz, i++);
    quick_sort(res, cnt, sizeof(*res), generic_cmp, &(struct thunk) { .cmp = cmp, .context = context });
    for (size_t i = 0; i < cnt; res[i] = (res[i] - (uintptr_t) arr) / sz, i++);
    return res;
}

uintptr_t *orders_stable_unique(const void *arr, size_t *p_cnt, size_t sz, stable_cmp_callback cmp, void *context)
{
    size_t cnt = *p_cnt;
    uintptr_t *res = NULL;
    if (!array_init(&res, p_cnt, cnt, sizeof(*res), 0, ARRAY_STRICT)) return NULL;
    
    for (size_t i = 0; i < cnt; res[i] = (uintptr_t) arr + i * sz, i++);
    quick_sort(res, cnt, sizeof(*res), generic_cmp, &(struct thunk) { .cmp = cmp, .context = context });

    uintptr_t tmp = 0;
    size_t ucnt = 0;    
    if (cnt) tmp = res[0], res[ucnt++] = (tmp - (uintptr_t) arr) / sz;    
    for (size_t i = 1; i < cnt; i++)
        if (cmp((const void *) tmp, (const void *) res[i], context)) tmp = res[i], res[ucnt++] = (tmp - (uintptr_t) arr) / sz;
    
    if (array_test(&res, p_cnt, sizeof(*res), 0, ARRAY_REDUCE, ARG_SIZE(ucnt)))
        return res;

    free(res);
    *p_cnt = 0;
    return NULL;
}

uintptr_t *ranks_from_orders(const uintptr_t *restrict arr, size_t cnt)
{
    uintptr_t *res = NULL;
    if (!array_init(&res, NULL, cnt, sizeof(*res), 0, ARRAY_STRICT)) return NULL;    
    for (size_t i = 0; i < cnt; res[arr[i]] = i, i++);
    return res;
}

bool ranks_from_pointers_inplace(uintptr_t *restrict arr, uintptr_t base, size_t cnt, size_t sz)
{
    uint8_t *bits = NULL;
    if (!array_init(&bits, NULL, BYTE_CNT(cnt), sizeof(*bits), 0, ARRAY_STRICT | ARRAY_CLEAR)) return 0;
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

bool ranks_from_orders_inplace(uintptr_t *restrict arr, size_t cnt)
{
    return ranks_from_pointers_inplace(arr, 0, cnt, 1);
}

uintptr_t *ranks_stable(const void *arr, size_t cnt, size_t sz, stable_cmp_callback cmp, void *context)
{
    uintptr_t *res = NULL;
    if (!array_init(&res, NULL, cnt, sizeof(*res), 0, ARRAY_STRICT)) return NULL;
    
    for (size_t i = 0; i < cnt; res[i] = (uintptr_t) arr + i * sz, i++);
    quick_sort(res, cnt, sizeof(*res), generic_cmp, &(struct thunk) { .cmp = cmp, .context = context });
    if (ranks_from_pointers_inplace(res, (uintptr_t) arr, cnt, sz)) return res;
    
    free(res);
    return NULL;
}

// This procedure applies orders to the array. Orders are not assumed to be a surjective map. 
bool orders_apply(uintptr_t *restrict ord, size_t cnt, size_t sz, void *restrict arr)
{
    uint8_t *bits = NULL;
    if (!array_init(&bits, NULL, BYTE_CNT(cnt), sizeof(*bits), 0, ARRAY_STRICT | ARRAY_CLEAR)) return 0;
    void *restrict swp = Alloca(sz);

    for (size_t i = 0; i < cnt; i++)
    {
        if (bit_test(bits, i)) continue;
        size_t k = ord[i];
        if (k == i) continue;
        bit_set(bits, i);
        memcpy(swp, (char *) arr + i * sz, sz);
        for (size_t j = i;;)
        {
            while (k < cnt && bit_test(bits, k)) k = ord[k];
            memcpy((char *) arr + j * sz, (char *) arr + k * sz, sz);
            if (k >= cnt)
            {
                memcpy((char *) arr + k * sz, swp, sz);
                break;
            }
            j = k;
            k = ord[j];
            bit_set(bits, j);
            if (k == i)
            {
                memcpy((char *) arr + j * sz, swp, sz);
                break;
            }
        }
    }
    free(bits);
    return 1;
}

static void swap(void *a, void *b, void *swp, size_t sz)
{
    memcpy(swp, a, sz);
    memcpy(a, b, sz);
    memcpy(b, swp, sz);
}

static void insertion_sort_impl(void *restrict arr, size_t tot, size_t sz, cmp_callback cmp, void *context, void *swp, size_t cutoff)
{
    size_t min = 0;
    for (size_t i = sz; i < cutoff; i += sz) if (cmp((char *) arr + min, (char *) arr + i, context)) min = i;
    if (min) swap((char *) arr + min, (char *) arr, swp, sz);
    for (size_t i = sz + sz; i < tot; i += sz)
    {
        size_t j = i;
        if (cmp((char *) arr + j - sz, (char *) arr + j, context)) // First iteration is unrolled
        {
            j -= sz;
            memcpy(swp, (char *) arr + j, sz);
            for (; j > sz && cmp((char *) arr + j - sz, (char *) arr + i, context); j -= sz) memcpy((char *) arr + j, (char *) arr + j - sz, sz);
            memcpy((char *) arr + j, (char *) arr + i, sz);
            memcpy((char *) arr + i, swp, sz);
        }
    }
}

static void quick_sort_impl(void *restrict arr, size_t tot, size_t sz, cmp_callback cmp, void *context, void *swp, size_t cutoff)
{
    uint8_t frm = 0;
    struct { size_t a, b; } stk[SIZE_BIT];
    size_t a = 0, b = tot - sz;
    for (;;)
    {
        size_t left = a, right = b, pvt = a + ((b - a) / sz >> 1) * sz;
        if (cmp((char *) arr + left, (char *) arr + pvt, context)) swap((char *) arr + left, (char *) arr + pvt, swp, sz);
        if (cmp((char *) arr + pvt, (char *) arr + right, context))
        {
            swap((char *) arr + pvt, (char *) arr + right, swp, sz);
            if (cmp((char *) arr + left, (char *) arr + pvt, context)) swap((char *) arr + left, (char *) arr + pvt, swp, sz);
        }
        left += sz;
        right -= sz;
        do {
            while (cmp((char *) arr + pvt, (char *) arr + left, context)) left += sz;
            while (cmp((char *) arr + right, (char *) arr + pvt, context)) right -= sz;
            if (left == right)
            {
                left += sz;
                right -= sz;
                break;
            }
            else if (left > right) break;
            swap((char *) arr + left, (char *) arr + right, swp, sz);
            if (left == pvt) pvt = right;
            else if (pvt == right) pvt = left;
            left += sz;
            right -= sz;
        } while (left <= right);
        if (right - a < cutoff)
        {
            if (b - left < cutoff)
            {
                if (!frm--) break;
                a = stk[frm].a;
                b = stk[frm].b;
            }
            else a = left;
        }
        else if (b - left < cutoff) b = right;
        else
        {
            if (right - a > b - left)
            {
                stk[frm].a = a;
                stk[frm].b = right;
                a = left;
            }
            else
            {
                stk[frm].a = left;
                stk[frm].b = b;
                b = right;
            }
            frm++;
        }
    }
}

void quick_sort(void *restrict arr, size_t cnt, size_t sz, cmp_callback cmp, void *context)
{
    if (cnt < 2) return;
    void *restrict swp = Alloca(sz);
    const size_t cutoff = QUICK_SORT_CUTOFF * sz, tot = cnt * sz;
    if (tot > cutoff)
    {
        quick_sort_impl(arr, tot, sz, cmp, context, swp, cutoff);
        insertion_sort_impl(arr, tot, sz, cmp, context, swp, cutoff);
    }
    else insertion_sort_impl(arr, tot, sz, cmp, context, swp, tot);
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

#include "np.h"
#include "memory.h"
#include "sort.h"
#include "test_sort.h"

#include <string.h> 
#include <stdlib.h> 

DECLARE_PATH

bool test_sort_generator_a(void *dst, size_t *p_context, struct log *log)
{
    size_t context = *p_context, cnt = (size_t) 1 << context;
    double *arr;
    if (!array_init(&arr, NULL, cnt, sizeof(*arr), 0, ARRAY_STRICT)) log_message(log, &MESSAGE_ERROR_CRT(errno).base);
    else
    {
        for (size_t i = 0; i < cnt; i++) arr[i] = (double) (QUICK_SORT_CUTOFF * (i / QUICK_SORT_CUTOFF)) - (double) (i % QUICK_SORT_CUTOFF);
        *(struct test_sort *) dst = (struct test_sort) { .arr = arr, .cnt = cnt, .sz = sizeof(*arr) };
        if (context < 24) ++*p_context;
        else *p_context = 0;
        return 1;
    }    
    return 0;
}

bool test_sort_generator_b(void *dst, size_t *p_context, struct log *log)
{
    double *arr;
    const size_t cnt = ((QUICK_SORT_CUTOFF + 1) << 1) + 1, hcnt = cnt >> 1;
    if (!array_init(&arr, NULL, cnt, sizeof(*arr), 0, ARRAY_STRICT)) log_message(log, &MESSAGE_ERROR_CRT(errno).base);
    else
    {
        for (size_t i = 0; i < hcnt; i++) arr[i] = -(double) (i + 1);
        arr[hcnt] = 0;
        for (size_t i = 0; i < hcnt; i++) arr[i + 22] = (double) (hcnt - i + 1);
        *(struct test_sort *) dst = (struct test_sort) { .arr = arr, .cnt = cnt, .sz = sizeof(*arr) };
        return 1;
    }
    return 0;
}

void test_sort_disposer(void *In)
{
    struct test_sort *in = In;
    free(in->arr);
}

static bool flt64_cmp_asc(const void *a, const void *b, const *context)
{
    (void) context;
    return *(double *) a > *(double *) b;
}

bool test_sort_a(void *In, struct log *log)
{
    struct test_sort *in = In;
    double *arr;
    if (!array_init(&arr, NULL, in->cnt, in->sz, 0, ARRAY_STRICT)) log_message(log, &MESSAGE_ERROR_CRT(errno).base);
    else
    {
        memcpy(arr, in->arr, in->cnt * in->sz);
        quick_sort(arr, in->cnt, in->sz, flt64_cmp_asc, NULL);
        size_t ind = 1;
        for (; ind < in->cnt; ind++) if (arr[ind - 1] > arr[ind]) break;
        free(arr);
        return ind == in->cnt;
    }
    return 0;
}
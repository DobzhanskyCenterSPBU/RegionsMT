#include "np.h"
#include "ll.h"
#include "memory.h"
#include "sort.h"
#include "test_sort.h"

#include <string.h> 
#include <stdlib.h> 

DECLARE_PATH

bool test_sort_generator_a_1(void *dst, size_t *p_context, struct log *log)
{
    size_t context = *p_context, cnt = ((size_t) 1 << context) - 1;
    double *arr;
    if (!array_init(&arr, NULL, cnt, sizeof(*arr), 0, ARRAY_STRICT)) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
    else
    {
        for (size_t i = 0; i < cnt; i++) arr[i] = (double) (QUICK_SORT_CUTOFF * (i / QUICK_SORT_CUTOFF)) - (double) (i % QUICK_SORT_CUTOFF);
        *(struct test_sort_a *) dst = (struct test_sort_a) { .arr = arr, .cnt = cnt, .sz = sizeof(*arr) };
        if (context <= TEST_SORT_EXP) ++*p_context;
        else *p_context = 0;
        return 1;
    }    
    return 0;
}

bool test_sort_generator_a_2(void *dst, size_t *p_context, struct log *log)
{
    (void) p_context;
    size_t cnt = ((QUICK_SORT_CUTOFF + 1) << 1) + 1, hcnt = cnt >> 1;
    double *arr;
    if (!array_init(&arr, NULL, cnt, sizeof(*arr), 0, ARRAY_STRICT)) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
    else
    {
        for (size_t i = 0; i < hcnt; i++) arr[i] = -(double) (i + 1);
        arr[hcnt] = 0;
        for (size_t i = 0; i < hcnt; i++) arr[i + hcnt + 1] = (double) (hcnt - i + 1);
        *(struct test_sort_a *) dst = (struct test_sort_a) { .arr = arr, .cnt = cnt, .sz = sizeof(*arr) };
        return 1;
    }
    return 0;
}

bool test_sort_generator_b_1(void *dst, size_t *p_context, struct log *log)
{
    size_t context = *p_context, ucnt = context * context + 13, cnt = ((size_t) 1 << context) + ucnt;
    double *arr;
    if (!array_init(&arr, NULL, cnt, sizeof(*arr), 0, ARRAY_STRICT)) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
    else
    {
        for (size_t i = 0; i < cnt; i++) arr[i] = (double) ((i + 13) % ucnt);
        *(struct test_sort_b *) dst = (struct test_sort_b) { .arr = arr, .cnt = cnt, .ucnt = ucnt };
        if (context <= TEST_SORT_EXP) ++*p_context;
        else *p_context = 0;
        return 1;
    }
    return 0;
}


bool test_sort_generator_c_1(void *dst, size_t *p_context, struct log *log)
{
    size_t context = *p_context, cnt = ((size_t) 1 << context) - 1;
    size_t *arr;
    if (!array_init(&arr, NULL, cnt, sizeof(*arr), 0, ARRAY_STRICT)) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
    else
    {
        for (size_t i = 0, j = 0; i < cnt; i++)
        {
            arr[i] = j;
            if (i >= ((size_t) 1 << j)) j++;
        }
        *(struct test_sort_c *) dst = (struct test_sort_c) { .arr = arr, .cnt = cnt };
        if (context <= TEST_SORT_EXP) ++*p_context;
        else *p_context = 0;
        return 1;
    }
    return 0;
}

void test_sort_disposer_a(void *In)
{
    struct test_sort_a *in = In;
    free(in->arr);
}

void test_sort_disposer_b(void *In)
{
    struct test_sort_b *in = In;
    free(in->arr);
}

void test_sort_disposer_c(void *In)
{
    struct test_sort_c *in = In;
    free(in->arr);
}

struct flt64_cmp_asc_test {
    void *a, *b;
    bool succ;
};

static bool flt64_cmp_asc_test(const void *a, const void *b, void *Context)
{
    struct flt64_cmp_asc_test *context = Context;
    if (a < context->a || b < context->a || context->b < a || context->b < b) context->succ = 0;
    return *(double *) a > *(double *) b;
}

bool test_sort_a(void *In, struct log *log)
{
    bool succ = 0;
    struct test_sort_a *in = In;
    double *arr;
    if (!array_init(&arr, NULL, in->cnt, in->sz, 0, ARRAY_STRICT)) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
    else
    {
        memcpy(arr, in->arr, in->cnt * in->sz);
        struct flt64_cmp_asc_test context = { .a = arr, .b = arr + in->cnt * in->sz - in->sz, .succ = 1 };
        quick_sort(arr, in->cnt, in->sz, flt64_cmp_asc_test, &context);
        size_t ind = 1;
        for (; ind < in->cnt; ind++) if (arr[ind - 1] > arr[ind]) break;
        succ = (!in->cnt || ind == in->cnt) && context.succ;
        free(arr);
    }
    return succ;
}

bool test_sort_b_1(void *In, struct log *log)
{
    bool succ = 0;
    struct test_sort_b *in = In;
    size_t ucnt = in->cnt;
    uintptr_t *ord = orders_stable_unique(in->arr, &ucnt, sizeof(*in->arr), flt64_stable_cmp_dsc, NULL);
    if (!ord) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
    else
    {
        if (ucnt == in->ucnt)
        {
            size_t ind = 1;
            for (; ind < ucnt; ind++) if (in->arr[ord[ind - 1]] <= in->arr[ord[ind]]) break;
            succ = !ucnt || ind == ucnt;
        }
        free(ord);
    }
    return succ;
}

bool test_sort_b_2(void *In, struct log *log)
{
    bool succ = 0;
    struct test_sort_b *in = In;
    size_t ucnt = in->cnt;
    uintptr_t *ord = orders_stable_unique(in->arr, &ucnt, sizeof(*in->arr), flt64_stable_cmp_dsc, NULL);
    if (!ord) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
    else
    {
        double *arr;
        if (!array_init(&arr, NULL, ucnt, sizeof(*arr), 0, ARRAY_STRICT)) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
        else
        {
            memcpy(arr, in->arr, in->ucnt * sizeof(*in->arr));
            if (!orders_apply(ord, ucnt, sizeof(*arr), arr)) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
            else
            {
                size_t ind = 1;
                for (; ind < ucnt; ind++) if (in->arr[ord[ind]] != arr[ind]) break;
                succ = ind == ucnt;
            }
            free(arr);
        }
        free(ord);
    }
    return succ;
}

bool test_sort_c_1(void *In, struct log *log)
{
    (void) log;
    struct test_sort_c *in = In;
    if (!in->cnt) return 1;
    for (size_t i = 1, j = 0; i < in->cnt; i++)
    {
        size_t res;
        if (!binary_search(&res, in->arr + j, in->arr, in->cnt, sizeof(*in->arr), size_cmp_stable_asc, NULL, BINARY_SEARCH_CRITICAL) || res != j) return 0;
        if (!binary_search(&res, in->arr + j, in->arr, in->cnt, sizeof(*in->arr), size_cmp_stable_asc, NULL, 0) || in->arr[res] != in->arr[j]) return 0;
        if (in->arr[j] != in->arr[i]) j = i;
    }
    return 1;
}

bool test_sort_c_2(void *In, struct log *log)
{
    (void) log;
    struct test_sort_c *in = In;
    if (!in->cnt) return 1;
    for (size_t i = in->cnt, j = in->cnt - 1; --i;)
    {
        size_t res;
        if (!binary_search(&res, in->arr + j, in->arr, in->cnt, sizeof(*in->arr), size_cmp_stable_asc, NULL, BINARY_SEARCH_CRITICAL | BINARY_SEARCH_RIGHTMOST) || res != j) return 0;
        if (!binary_search(&res, in->arr + j, in->arr, in->cnt, sizeof(*in->arr), size_cmp_stable_asc, NULL, BINARY_SEARCH_RIGHTMOST) || in->arr[res] != in->arr[j]) return 0;
        if (in->arr[j] != in->arr[i]) j = i;
    }
    return 1;
}
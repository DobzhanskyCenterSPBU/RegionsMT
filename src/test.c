#include "np.h"
#include "ll.h"
#include "log.h"
#include "memory.h"
#include "utf8.h"
#include "test.h"

#include "test_utf8.h"
#include "test_sort.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

DECLARE_PATH

size_t message_error_test(char *buff, size_t buff_cap, struct message_error_test *context)
{
    int tmp = snprintf(buff, buff_cap, "Test failed on input data no. %zu!\n", context->ind);
    if (tmp > 0 && (size_t) tmp < buff_cap) return tmp;
    return 0;
}

size_t message_info_test(char *buff, size_t buff_cap, struct message *context)
{
    (void) context;
    int tmp = snprintf(buff, buff_cap, "Test succeeded!\n");
    if (tmp > 0 && (size_t) tmp < buff_cap) return tmp;
    return 0;
}
/*
bool test_ll_1()
{
    struct { double a, b; int res; } in[] = {
        { NaN, 0., 0 },
        { 0., NaN, 0 },
        { NaN, NaN, 0 },
        { 1. , -1, 0 },
        { -1., 1, 0 },
        { 1., 0., -1 },
        { 0., 1., 1 },
        { 0., 0., 0 },
        { -1., 0., -1 },
        { 1., 1., 0 },
        { 0., -1., 1 },
        { -1., -1., 0 },        
        { NaN, nan("zzz"), 0 },
        { nan("zzz"), NaN, 0 },
        { nan("zzz"), nan("zzz"), 0 },
        { DBL_MIN, DBL_MAX, 1 },
        { DBL_MAX, DBL_MIN, -1 },
        { 0, DBL_EPSILON, 1 },
        { DBL_EPSILON, 0, -1 },
    };

    for (size_t i = 0; i < countof(in); i++)
    {
        int res = flt64_stable_cmp_dsc_abs(&in[i].a, &in[i].b, NULL);
        if (res != in[i].res) return 0;
    }

    return 1;
}

int flt64_stable_cmp_dsc_nan_test(double *p_a, double *p_b, void *context)
{
    (void) context;
    double a = *p_a, b = *p_b;
    return isnan(a) ? (isnan(b) ? 0 : 1) : isnan(b) ? -1 : (a < b ? 1 : b < a ? -1 : 0);
}

bool test_ll_2()
{
    struct { double a, b; int res; } in[] = {
        { 1. , -1, -1 },
        { -1., 1, 1 },
        { 1., 0., -1 },
        { 0., 1., 1 },
        { -1., 0., 1 },
        { 0., -1., -1 },
        { 0., 0., 0 },
        { 1., 1., 0 },
        { -1., -1., 0 },
        { NaN, 0., 1 },
        { 0., NaN, -1 },
        { NaN, NaN, 0 },
        { NaN, nan("zzz"), 0 },
        { NaN, DBL_MAX, 1 },
        { nan("zzz"), NaN, 0 },
        { DBL_MAX, NaN, -1 },
        { nan("zzz"), nan("zzz"), 0 },
        { DBL_MIN, DBL_MAX, 1 },
        { DBL_MAX, DBL_MIN, -1 },
        { 0, DBL_EPSILON, 1 },
        { DBL_EPSILON, 0, -1 },
    };

    for (size_t i = 0; i < countof(in); i++)
    {
        int res = flt64_stable_cmp_dsc_nan(&in[i].a, &in[i].b, NULL);
        if (res != in[i].res || flt64_stable_cmp_dsc_nan_test(&in[i].a, &in[i].b, NULL) != in[i].res) return 0;
    }
    
    return 1;
}

void test_ll_2_perf(struct log *log)
{
    struct { double a, b; int res; } in[] = {
        { 1. , -1, -1 },
        { -1., 1, 1 },
        { 1., 0., -1 },
        { 0., 1., 1 },
        { -1., 0., 1 },
        { 0., -1., -1 },
        { 0., 0., 0 },
        { 1., 1., 0 },
        { -1., -1., 0 },
        { NaN, 0., 1 },
        { 0., NaN, -1 },
        { NaN, NaN, 0 },
        { NaN, nan("zzz"), 0 },
        { NaN, DBL_MAX, 1 },
        { nan("zzz"), NaN, 0 },
        { DBL_MAX, NaN, -1 },
        { nan("zzz"), nan("zzz"), 0 },
        { DBL_MIN, DBL_MAX, 1 },
        { DBL_MAX, DBL_MIN, -1 },
        { 0, DBL_EPSILON, 1 },
        { DBL_EPSILON, 0, -1 },
    };

    int cnt = 0;
    uint64_t start = get_time();
    for (size_t j = 0; j < 4096 * (USHRT_MAX + 1); j++)
    {
        for (size_t i = 0; i < countof(in); i++)
        {
            cnt += flt64_stable_cmp_dsc_nan_test(&in[i].a, &in[i].b, NULL);
        }
    }
    printf("Sum: %d\n", cnt);
    log_message(log, &MESSAGE_INFO_TIME_DIFF(start, get_time(), "Branched comparison").base);

    cnt = 0;
    start = get_time();
    for (size_t j = 0; j < 4096 * (USHRT_MAX + 1); j++)
    {
        for (size_t i = 0; i < countof(in); i++)
        {
            cnt += flt64_stable_cmp_dsc_nan(&in[i].a, &in[i].b, NULL);
        }
    }
    printf("Sum: %d\n", cnt);        
    log_message(log, &MESSAGE_INFO_TIME_DIFF(start, get_time(), "SIMD comparison").base);
}

struct memory_chunk {
    void *ptr;
    size_t sz;
};

void *a(struct memory_chunk *a)
{
    a->ptr = malloc(a->sz);
    printf("Inside %s\n", __func__);
    return a->ptr;
}

void *b(struct memory_chunk *a)
{
    (void) a;
    (void) b;
    printf("Inside %s\n", __func__);
    return a->ptr;
}

bool test_ll_3()
{
    spinlock_handle sp = SPINLOCK_INIT;
    struct memory_chunk chunk = { .sz = 100 };
    void *res;
    for (size_t i = 0; i < 10; i++)
    {
        res = double_lock_execute(&sp, (double_lock_callback) a, (double_lock_callback) b, &chunk, &chunk);
    }
    free(res);
    return 1;
}

bool test_ll_4()
{
    struct { uint32_t a; uint32_t res_bsf, res_bsr; } in[] = {
        { 0b0, UINT32_MAX, UINT32_MAX },
        { 0b1001, 0, 3 },
        { 0b10010, 1, 4 },
        { 0b100100, 2, 5 },
        { 0b1001000, 3, 6 },
        { 0b10010000, 4, 7 },
        { 0b100100000000, 8, 11 },
        { 0b1001000000000000, 12, 15 },
        { 0b1001000000000000000, 15, 18 },
    };
    
    for (size_t i = 0; i < countof(in); i++)
    {
        uint32_t res_bsr = uint32_bit_scan_reverse(in[i].a), res_bsf = uint32_bit_scan_forward(in[i].a);
        //printf("%zu: %zu, %zu\n", in[i].a, res_bsr, res_bsf);
        if (res_bsr != in[i].res_bsr || res_bsf != in[i].res_bsf) return 0;
    }

    return 1;
}

bool perf()
{
    struct log log;
    if (!log_init(&log, NULL, TEMP_BUFF + 1)) return 0;

    test_ll_2_perf(&log);

    log_message(&log, &MESSAGE_INFO_TIME_DIFF(start, get_time(), "Performance tests execution").base);
    log_close(&log);
    return 1;
}

*/

typedef bool (*test_generator_callback)(void *, size_t *, struct log *);
typedef void (*test_disposer_callback)(void *);
typedef bool (*test_callback)(void *, struct log *);

struct test_group {
    test_disposer_callback test_disposer;
    size_t test_sz;
    struct {
        test_generator_callback *test_generator;
        size_t test_generator_cnt;
    };
    struct {
        test_callback *test;
        size_t test_cnt;
    };
};

bool test(struct log *log)
{
    struct test_group group_arr[] = {
        /*{
            test_utf8_generator,
            NULL,
            sizeof(struct test_utf8),
            CLII((test_callback[]) { 
                test_utf8_len,  
                test_utf8_encode,
                test_utf8_decode,
                test_utf16_encode,
                test_utf16_decode
            })
        },*/
        {
            test_sort_disposer,
            sizeof(struct test_sort),
            CLII((test_generator_callback[]) {
                test_sort_generator_a,
                test_sort_generator_b
            }),
            CLII((test_callback[]) {
                test_sort_a,
            })
        }
    };
    
    uint64_t start = get_time();
    size_t succ = 0;    
    size_t test_data_sz = 0;
    for (size_t i = 0; i < countof(group_arr); i++) if (test_data_sz < group_arr[i].test_sz) test_data_sz = group_arr[i].test_sz;    
    void *test_data = NULL;
    if (!array_init(&test_data, NULL, test_data_sz, 1, 0, ARRAY_STRICT)) log_message(log, &MESSAGE_ERROR_CRT(errno).base);
    {
        for (size_t i = 0; i < countof(group_arr); i++)
        {
            struct test_group *group = group_arr + i;
            for (size_t j = 0; j < group->test_generator_cnt; j++)
            {
                size_t context = 0;
                do {
                    size_t ind = context;
                    if (!group->test_generator[j](test_data, &context, log)) log_message(log, &MESSAGE_ERROR_CRT(errno).base);
                    else
                    {
                        for (size_t k = 0; k < group->test_cnt; k++)
                            if (!group->test[k](test_data, log)) log_message_var(log, &MESSAGE_VAR_GENERIC(MESSAGE_TYPE_WARNING), "Test no. %zu of the group no. %zu failed on input instance no. %zu of the generator no. %zu!\n", k + 1, i + 1, ind + 1, j + 1);
                        if (group->test_disposer) group->test_disposer(test_data);
                    }
                } while (context);
            }
        }
        free(test_data);
    }
    log_message(log, &MESSAGE_INFO_TIME_DIFF(start, get_time(), "Tests execution").base);
    return 1;
}
#include "np.h"
#include "ll.h"
#include "log.h"
#include "memory.h"
#include "utf8.h"
#include "test.h"

#include "test_ll.h"
#include "test_sort.h"
#include "test_utf8.h"

#include <stdlib.h>

DECLARE_PATH

bool test(struct log *log)
{
    struct test_group group_arr[] = {
        {
            NULL,
            sizeof(struct test_ll_a),
            CLII((test_generator_callback[]) {
                test_ll_generator_a,
            }),
            CLII((test_callback[]) {
                test_ll_a_1,
                test_ll_a_2,
                test_ll_a_3
            })
        },
        {
            NULL,
            sizeof(struct test_ll_a),
            CLII((test_generator_callback[]) {
                test_ll_generator_b,
            }),
            CLII((test_callback[]) {
                test_ll_b,
            })
        },
#if 0
        {
            test_sort_disposer_a,
            sizeof(struct test_sort_a),
            CLII((test_generator_callback[]) {
                test_sort_generator_a_1,
                test_sort_generator_a_2
            }),
            CLII((test_callback[]) {
                test_sort_a,
            })
        },
#endif
        {
            test_sort_disposer_b,
            sizeof(struct test_sort_b),
            CLII((test_generator_callback[]) {
                test_sort_generator_b_1
            }),
            CLII((test_callback[]) {
                test_sort_b_1,
                test_sort_b_2
            })
        },
        {
            NULL,
            sizeof(struct test_utf8),
            CLII((test_generator_callback[]) {
                test_utf8_generator,
            }),            
            CLII((test_callback[]) { 
                test_utf8_len,  
                test_utf8_encode,
                test_utf8_decode,
                test_utf16_encode,
                test_utf16_decode
            })
        }
    };
    
    uint64_t start = get_time();
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
                            if (!group->test[k](test_data, log)) 
                                log_message_var(log, &MESSAGE_VAR_GENERIC(MESSAGE_TYPE_WARNING), "Test no. %zu of the group no. %zu failed under the input data instance no. %zu of the generator no. %zu!\n", k + 1, i + 1, ind + 1, j + 1);
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

bool perf(struct log *log)
{
    uint64_t start = get_time();

    // Performance tests here

    log_message(log, &MESSAGE_INFO_TIME_DIFF(start, get_time(), "Performance tests execution").base);
    return 1;
}
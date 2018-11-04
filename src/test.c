#include "np.h"
#include "ll.h"
#include "log.h"
#include "memory.h"
#include "utf8.h"
#include "test.h"

#include <stdlib.h>

bool test(const struct test_group *group_arr, size_t cnt, struct log *log)
{
    uint64_t start = get_time();
    size_t test_data_sz = 0;
    for (size_t i = 0; i < cnt; i++) if (test_data_sz < group_arr[i].test_sz) test_data_sz = group_arr[i].test_sz;
    void *test_data = NULL;
    if (!array_init(&test_data, NULL, test_data_sz, 1, 0, ARRAY_STRICT)) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
    {
        for (size_t i = 0; i < cnt; i++)
        {
            uint64_t group_start = get_time();
            const struct test_group *group = group_arr + i;
            for (size_t j = 0; j < group->test_generator_cnt; j++)
            {
                size_t context = 0;
                do {
                    size_t ind = context;
                    if (!group->test_generator[j](test_data, &context, log)) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                    else
                    {
                        for (size_t k = 0; k < group->test_cnt; k++)
                            if (!group->test[k](test_data, log)) log_message_generic(log, CODE_METRIC, MESSAGE_WARNING, "Test no. %s%zu%s of the group no. %s%zu%s failed under the input data instance no. %s%zu%s of the generator no. %s%zu%s!\n", ENV(log->style.num, k + 1), ENV(log->style.num, i + 1), ENV(log->style.num, ind + 1), ENV(log->style.num, j + 1));
                        if (group->test_dispose) group->test_dispose(test_data);
                    }
                } while (context);
            }
            log_message_time_diff(log, CODE_METRIC, MESSAGE_INFO, group_start, get_time(), "Tests execution of the group no. %s%zu%s took ", ENV(log->style.num, i + 1));
        }
        free(test_data);
    }
    log_message_time_diff(log, CODE_METRIC, MESSAGE_INFO, start, get_time(), "Tests execution took ");
    return 1;
}

bool perf(struct log *log)
{
    uint64_t start = get_time();

    // Performance tests here

    log_message_time_diff(log, CODE_METRIC, MESSAGE_INFO, start, get_time(), "Performance tests execution took ");
    return 1;
}

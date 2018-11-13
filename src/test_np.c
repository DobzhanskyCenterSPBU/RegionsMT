#include "np.h"
#include "test.h"
#include "memory.h"

#include "test_np.h"

#include <stdlib.h> 

bool test_np_generator_a(void *dst, size_t *p_context, struct log *log)
{
    size_t context = *p_context, cnt = (size_t) 1 << context;
    char *str;
    if (!array_init(&str, NULL, cnt, sizeof(*str), 0, ARRAY_STRICT)) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
    else
    {
        for (size_t i = 0; i < cnt;) 
            for (char j = 0; j < CHAR_MAX && i < cnt; j++)
                for (char k = 0; k < j && i < cnt; k++) str[i++] = k;
        *(struct test_np_a *) dst = (struct test_np_a) { .str = str, .cnt = cnt };
        if (context < TEST_NP_EXP) ++*p_context;
        else *p_context = 0;
        return 1;
    }
    return 0;
}

void test_np_disposer_a(void *In)
{
    struct test_np_a *in = In;
    free(in->str);
}

static void *Memrchr_test(void const *Str, int ch, size_t cnt)
{
    const char *str = Str;
    while (cnt) if (str[--cnt] == ch) return (void *) (str + cnt);
    return NULL;
}

bool test_np_a(void *In, struct log *log)
{
    (void) log;
    struct test_np_a *in = In;
    for (char i = 0; i < CHAR_MAX; i++)
    {
        for (size_t cnt = in->cnt; cnt;)
        {
            char *a = Memrchr(in->str, i, cnt);
            if (a != Memrchr_test(in->str, i, cnt)) return 0;
            if (!a) break;
            cnt = a - in->str;
        }
    }
    return 1;
}

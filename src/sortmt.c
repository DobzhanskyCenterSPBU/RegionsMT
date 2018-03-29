#include "np.h"
#include "common.h"
#include "memory.h"
#include "sort.h"
#include "sortmt.h"
#include "threadpool.h"

#include <string.h>
#include <stdlib.h>

struct sort_context {
    void *arr, *temp;
    size_t cnt;
    size_t sz;
    cmp_callback cmp;
    void *context;
};

struct sort_args {
    size_t off, len;
};

struct merge_args {
    size_t off, pvt, len;
} ;

struct sort_mt
{
    struct sort_context context;
    struct sort_args *s_args;
    struct merge_args *m_args;
    struct task *tasks;
    uint8_t *sync; // Last two bits in 'res->sync' are not in use!
    size_t *args;
};

static void merge_sorted_arrays(void *restrict arr, void *restrict temp, size_t pvt, size_t cnt, size_t sz, cmp_callback cmp, void *context)
{
    const size_t tot = cnt * sz, mid = pvt * sz;
    size_t i = 0, j = mid;
        
    for (size_t k = 0; k < tot; k += sz)
    {
        if (i < mid && (j == tot || cmp((char *) arr + j, (char *) arr + i, context))) 
            memcpy((char *) temp + k, (char *) arr + i, sz), i += sz;
        else 
            memcpy((char *) temp + k, (char *) arr + j, sz), j += sz;
    }

    memcpy(arr, temp, tot);
}

static bool merge_thread_proc(void *Args, void *Context)
{
    struct merge_args *restrict args = Args;
    struct sort_context *restrict context = Context;
    const size_t off = args->off * context->sz;
    merge_sorted_arrays((char *) context->arr + off, (char *) context->temp + off, args->pvt, args->len, context->sz, context->cmp, context->context);
    return 1;
}

static bool sort_thread_proc(void *Args, void *Context)
{
    struct sort_args *restrict args = Args;
    struct sort_context *restrict context = Context;
    const size_t off = args->off * context->sz;
    quick_sort((char *) context->arr + off, args->len, context->sz, context->cmp, context->context);
    return 1;
}

void sort_mt_dispose(struct sort_mt *arg)
{
    if (!arg) return;    
    free(arg->m_args);
    free(arg->s_args);
    free(arg->tasks);
    free(arg->sync);
    free(arg->args);
    free(arg->context.temp);
    free(arg);
}

struct sort_mt *sort_mt_create(void *arr, size_t cnt, size_t sz, cmp_callback cmp, void *context, struct thread_pool *pool, struct sort_mt_sync *sync)
{
    struct sort_mt_sync *snc = sync ? sync : &(struct sort_mt_sync) { 0 };
    size_t dep = size_bit_scan_reverse(thread_pool_get_count(pool)); // Max value of 'dep' is 63 under x86_64 and 31 under x86 
    const size_t s_cnt = (size_t) 1 << dep, m_cnt = s_cnt - 1;

    struct sort_mt *res;
    if ((res = calloc(1, sizeof(*res))) != NULL &&
        (res->sync = calloc((s_cnt + 3) >> 2, 1)) != NULL && // No overflow happens (cf. previous comment), s_cnt > 0
        array_init(&res->s_args, NULL, s_cnt, sizeof(*res->s_args), 0, ARRAY_STRICT) &&
        array_init(&res->m_args, NULL, m_cnt, sizeof(*res->m_args), 0, ARRAY_STRICT) &&
        array_init(&res->context.temp, NULL, cnt, sz, 0, ARRAY_STRICT))
    {
        res->context = (struct sort_context) { .arr = arr, .cnt = cnt, .sz = sz, .cmp = cmp, .context = context };
        res->s_args[0] = (struct sort_args) { .off = 0, .len = cnt };

        for (size_t i = 0; i < dep; i++)
        {
            const size_t jm = (size_t) 1 << i, jr = (size_t) 1 << (dep - i - 1), jo = s_cnt - (jm << 1), ji = jr << 1;
            for (size_t j = 0, k = 0; j < jm; j++, k += ji)
            {
                const size_t l = res->s_args[k].len, hl = l >> 1;
                res->s_args[k].len = hl;
                res->s_args[k + jr] = (struct sort_args) { res->s_args[k].off + hl, l - hl };
                res->m_args[jo + j] = (struct merge_args) { res->s_args[k].off, hl, l };
            }
        }

        if (m_cnt)
        {
            if (array_init(&res->tasks, NULL, s_cnt + m_cnt, sizeof(*res->tasks), 0, ARRAY_STRICT) &&
                array_init(&res->args, NULL, s_cnt + m_cnt - 1, sizeof(*res->args), 0, ARRAY_STRICT))
            {
                // Assigning sorting tasks
                for (size_t i = 0; i < s_cnt; i++)
                {
                    res->args[i] = i;
                    res->tasks[i] = (struct task) {
                        .callback = sort_thread_proc,
                        .cond = snc->cond,
                        .a_succ = bit_set_interlocked_p,
                        .arg = &res->s_args[i],
                        .context = &res->context,
                        .cond_mem = snc->cond_mem,
                        .a_succ_mem = res->sync,
                        .cond_arg = snc->cond_arg,
                        .a_succ_arg = &res->args[i]
                    };
                }

                // Assigning merging tasks (except the last task)
                for (size_t i = 0, j = s_cnt; i < m_cnt - 1; i++, j++)
                {
                    res->args[j] = j;
                    res->tasks[j] = (struct task) {
                        .callback = merge_thread_proc,
                        .cond = bit_test2_acquire_p,
                        .a_succ = bit_set_interlocked_p,
                        .arg = &res->m_args[i],
                        .context = &res->context,
                        .cond_mem = res->sync,
                        .a_succ_mem = res->sync,
                        .cond_arg = &res->args[i << 1],
                        .a_succ_arg = &res->args[j]
                    };
                }

                // Last merging task requires special handling
                res->tasks[s_cnt + m_cnt - 1] = (struct task) {
                    .callback = merge_thread_proc,
                    .cond = bit_test2_acquire_p,
                    .a_succ = snc->a_succ,
                    .arg = &res->m_args[m_cnt - 1],
                    .context = &res->context,
                    .cond_mem = res->sync,
                    .a_succ_mem = snc->a_succ_mem,
                    .cond_arg = &res->args[(m_cnt - 1) << 1],
                    .a_succ_arg = snc->a_succ_arg
                };

                if (thread_pool_enqueue_tasks(pool, res->tasks, m_cnt + s_cnt, 1)) return res;
            }            
        }
        else
        {
            if ((res->tasks = malloc(sizeof(*res->tasks))) != NULL)
            {
                *res->tasks = (struct task)
                {
                    .callback = sort_thread_proc,
                    .cond = snc->cond,
                    .a_succ = snc->a_succ,
                    .arg = &res->s_args[0],
                    .context = &res->context,
                    .cond_mem = snc->cond_mem,
                    .a_succ_mem = snc->a_succ_mem,
                    .cond_arg = snc->cond_arg,
                    .a_succ_arg = snc->a_succ_arg
                };

                if (thread_pool_enqueue_tasks(pool, res->tasks, 1, 1)) return res;
            }
        }
        sort_mt_dispose(res);
    }   
    return NULL;
}

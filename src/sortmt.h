#pragma once

#include "threadpool.h"
#include "sort.h"

struct sort_mt;

struct sort_mt_sync {
    condition_callback cond;
    aggregator_callback a_succ;
    volatile void *cond_mem, *a_succ_mem;
    void *cond_arg, *a_succ_arg;
};

void sort_mt_dispose(struct sort_mt *);
struct sort_mt *sort_mt_create(void *, size_t, size_t, cmp_callback, void *, struct thread_pool *, struct sort_mt_sync *);

#pragma once

#include "common.h"
#include "ll.h"

typedef bool (*task_callback)(void *, void *);
typedef bool (*condition_callback)(volatile void *, const void *);
typedef void (*aggregator_callback)(volatile void *, const void *);

struct task {
    task_callback callback;
    condition_callback cond;
    aggregator_callback a_succ, a_fail;
    void *arg, *context, *cond_arg, *a_succ_arg, *a_fail_arg;
    volatile void *cond_mem, *a_succ_mem, *a_fail_mem;
};

// Opaque structure with OS-dependent implementation
struct thread_pool;

size_t thread_pool_remove_tasks(struct thread_pool *, struct task *, size_t);
bool thread_pool_enqueue_tasks(struct thread_pool *, struct task *, size_t, bool);
struct thread_pool *thread_pool_create(size_t, size_t, size_t);
size_t thread_pool_dispose(struct thread_pool *, size_t *);
size_t thread_pool_get_count(struct thread_pool *);
size_t thread_pool_get_thread_id(struct thread_pool *);
void *thread_pool_get_thread_data(struct thread_pool *, size_t *, size_t *);

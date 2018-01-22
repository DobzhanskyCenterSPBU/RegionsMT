#include "memory.h"
#include "threadpool.h"
#include "threadsupp.h"

#include <string.h>
#include <stdlib.h>

struct task_queue {
    size_t cap, begin, cnt;
    struct task *tasks[]; // Array with copies of task pointers
};

struct thread_storage {
    size_t id, data_sz;
    char data[]; // Array with thread-local data 
};

struct thread_pool {
    struct task_queue *queue;
    spinlock_handle spinlock, startuplock;
    mutex_handle mutex;
    condition_handle condition;
    tls_handle tls;
    struct thread_storage **storage;
    volatile size_t active, terminate;
    size_t cnt;
    uint8_t *thread_bits;
    thread_handle threads[];
};

static struct task_queue *task_queue_create(size_t cap)
{
    struct task_queue *res;
    if (!array_init((void **) &res, &cap, sizeof(*res->tasks), sizeof(*res), 0)) return NULL;
    
    res->cap = cap;
    res->begin = res->cnt = 0;
    return res;
}

static bool task_queue_test(struct task_queue **p_queue, size_t diff)
{
    struct task_queue *res = *p_queue;
    size_t cap = res->cap;
    if (!array_resize((void **) &res, &cap, sizeof(*res->tasks), sizeof(*res), ARRAY_RESIZE_EXTEND_ONLY, ARG_S(res->cnt, diff))) return 0;
    if (cap == res->cap) return 1; // Queue has already enough space
    
    size_t left = res->begin + res->cnt;
    if (left > res->cap)
    {
        if (left > cap)
        {
            memcpy(res->tasks + res->cap, res->tasks, (cap - res->cap) * sizeof(*res->tasks));
            memmove(res->tasks, res->tasks + cap - res->cap, (left - cap) * sizeof(*res->tasks));
        }
        else memcpy(res->tasks + res->cap, res->tasks, (left - res->cap) * sizeof(*res->tasks));
    }    
    res->cap = cap;
    *p_queue = res;

    return 1;
}

static struct task *task_queue_peek(struct task_queue *restrict queue, size_t offset)
{
    if (queue->begin + offset >= queue->cap) return queue->tasks[queue->begin + offset - queue->cap];
    return queue->tasks[queue->begin + offset];
}

// In the next two functions it is assumed that queue->cap >= queue->cnt + cnt
static void task_queue_enqueue_lo(struct task_queue *restrict queue, struct task *newtasks, size_t cnt)
{
    size_t left = queue->begin + queue->cnt;
    if (left >= queue->cap) left -= queue->cap;
       
    if (left + cnt > queue->cap)
    {
        for (size_t i = left; i < queue->cap; queue->tasks[i++] = newtasks++);
        for (size_t i = 0; i < left + cnt - queue->cap; queue->tasks[i++] = newtasks++);
    }
    else 
        for (size_t i = left; i < left + cnt; queue->tasks[i++] = newtasks++);

    queue->cnt += cnt;
}

static void task_queue_enqueue_hi(struct task_queue *restrict queue, struct task *newtasks, size_t cnt)
{
    size_t left = queue->begin + queue->cap - cnt;
    if (left >= queue->cap) left -= queue->cap;
        
    if (left > queue->begin)
    {
        for (size_t i = left; i < queue->cap; queue->tasks[i++] = newtasks++);
        for (size_t i = 0; i < queue->begin; queue->tasks[i++] = newtasks++);
    }
    else
        for (size_t i = left; i < queue->begin; queue->tasks[i++] = newtasks++);

    queue->begin = left;
    queue->cnt += cnt;    
}

static void task_queue_dequeue(struct task_queue *restrict queue, size_t offset)
{
    if (offset)
    {
        if (queue->begin + offset >= queue->cap) queue->tasks[queue->begin + offset - queue->cap] = queue->tasks[queue->begin];
        else queue->tasks[queue->begin + offset] = queue->tasks[queue->begin];
    }

    queue->cnt--;
    queue->begin++;
    if (queue->begin >= queue->cap) queue->begin -= queue->cap;
}

// Searches and removes tasks from thread pool queue (commonly used by cleanup routines to remove pending tasks which will be never executed)
size_t thread_pool_remove_tasks(struct thread_pool *pool, struct task *tasks, size_t cnt)
{
    size_t fndr = 0;    
    spinlock_acquire(&pool->spinlock);

    for (size_t i = 0; i < cnt; i++)
    {
        for (size_t j = 0; j < pool->queue->cnt;)
        {
            if (task_queue_peek(pool->queue, j) == &tasks[i])
            {
                task_queue_dequeue(pool->queue, j);
                fndr++;
            }
            else j++;
        }        
    }

    spinlock_release(&pool->spinlock);
    return fndr;
}

bool thread_pool_enqueue_tasks(struct thread_pool *pool, struct task *tasks, size_t tasks_cnt, bool hi)
{
    spinlock_acquire(&pool->spinlock);
    
    if (!task_queue_test(&pool->queue, tasks_cnt)) // Queue extension if required
    {
        spinlock_release(&pool->spinlock);
        return 0;
    }
    
    (hi ? task_queue_enqueue_hi : task_queue_enqueue_lo)(pool->queue, tasks, tasks_cnt);

    spinlock_release(&pool->spinlock);
    condition_broadcast(&pool->condition);
        
    return 1;
}

size_t thread_pool_get_count(struct thread_pool *pool)
{
    return pool->cnt;
}

// Determines current thread identifier. Returns '-1' for the main thread.
size_t thread_pool_get_thread_id(struct thread_pool *pool)
{
    struct thread_storage **p_storage = tls_fetch(&pool->tls);
    if (p_storage) return (*p_storage)->id;
    return SIZE_MAX;
}

// Returns pointer to the thread-local data for the current thread. Returns 'NULL' for the main thread.
void *thread_pool_get_thread_data(struct thread_pool *pool, size_t *p_thread_id, size_t *p_thread_data_sz)
{
    struct thread_storage **p_storage = tls_fetch(&pool->tls);
    if (p_storage)
    {
        struct thread_storage *storage = *p_storage;
        if (p_thread_id) *p_thread_id = storage->id;
        if (p_thread_data_sz) *p_thread_data_sz = storage->data_sz;
        return storage->data;
    }
    if (p_thread_id) *p_thread_id = SIZE_MAX;
    if (p_thread_data_sz) *p_thread_data_sz = 0;
    return NULL;
}

static inline size_t threadproc_startup(struct thread_pool *pool)
{
    size_t res = SIZE_MAX;    
    spinlock_acquire(&pool->startuplock);

    for (size_t i = 0; i < BYTE_CNT(pool->cnt); i++)
    {
        uint8_t j = uint8_bit_scan_forward(~pool->thread_bits[i]);
        if ((uint8_t) ~j)
        {
            res = (i << 3) + j;
            tls_assign(&pool->tls, &pool->storage[res]);
            bit_set(pool->thread_bits, res);
            break;
        }
    }

    spinlock_release(&pool->startuplock);
    return res;
}

static inline void threadproc_shutdown(struct thread_pool *pool, size_t id)
{
    spinlock_acquire(&pool->startuplock);
    bit_reset(pool->thread_bits, id);
    spinlock_release(&pool->startuplock);
}

static thread_return thread_callback_convention thread_proc(void *Pool) // General thread routine
{
    struct thread_pool *restrict pool = Pool;
    size_t id = threadproc_startup(pool);
    uintptr_t threadres = 1;
        
    for (;;)
    {
        struct task *tsk = NULL;
        spinlock_acquire(&pool->spinlock);

        for (size_t i = 0; i < pool->queue->cnt; i++)
        {
            struct task *temp = task_queue_peek(pool->queue, i);
            if (temp->cond && !temp->cond(temp->cond_mem, temp->cond_arg)) continue;
            
            tsk = temp;
            task_queue_dequeue(pool->queue, i);
            break;
        }

        spinlock_release(&pool->spinlock);

        if (tsk)
        {
            if (!tsk->callback || (*tsk->callback)(tsk->arg, tsk->context)) // Here we execute the task routine
            {
                if (tsk->a_succ) tsk->a_succ(tsk->a_succ_mem, tsk->a_succ_arg);
                condition_broadcast(&pool->condition);
            }
            else
            {
                if (tsk->a_fail) tsk->a_fail(tsk->a_fail_mem, tsk->a_fail_arg);
                threadres = 0;
            }
        }
        else
        {
            mutex_acquire(&pool->mutex);
            pool->active--;

            // Is it the time to exit the thread?..
            if (pool->terminate && !pool->active)
            {
                pool->terminate--;
                mutex_release(&pool->mutex);
                condition_signal(&pool->condition);
                
                threadproc_shutdown(pool, id);
                return (thread_return) threadres;
            }

            // Time to sleep...
            condition_sleep(&pool->condition, &pool->mutex);

            pool->active++;
            mutex_release(&pool->mutex);
        }
    }
}

struct thread_pool *thread_pool_create(size_t cnt, size_t tasks_cnt, size_t storage_sz)
{
    size_t ind;
    struct thread_pool *pool;
    if (array_init_strict((void **) &pool, cnt, sizeof(*pool->threads), sizeof(*pool), 1))
    {
        pool->cnt = pool->active = cnt;
        if (array_init_strict((void **) &pool->storage, pool->cnt, sizeof(*pool->storage), 0, 1))
        {
            for (ind = 0; ind < pool->cnt; ind++)
            {
                if (array_init_strict((void **) &pool->storage[ind], 1, sizeof(*pool->storage[ind]), storage_sz, 1))
                    *pool->storage[ind] = (struct thread_storage) { .id = ind, .data_sz = storage_sz };
                else break;
            }
            if (ind == pool->cnt)
            {
                if (array_init_strict((void **) &pool->thread_bits, BYTE_CNT(pool->cnt), 1, 0, 1) && (pool->queue = task_queue_create(tasks_cnt)) != NULL)
                {
                    pool->spinlock = pool->startuplock = SPINLOCK_INIT;
                    if (mutex_init(&pool->mutex))
                    {
                        if (condition_init(&pool->condition))
                        {
                            if (tls_init(&pool->tls))
                            {
                                for (ind = 0; ind < pool->cnt && thread_init(&pool->threads[ind], thread_proc, pool); ind++);
                                if (ind == pool->cnt) return pool;                                
                                while (ind--)
                                {
                                    thread_terminate(pool->threads + ind);
                                    thread_close(pool->threads + ind);
                                }
                                ind = pool->cnt;
                                tls_close(&pool->tls);
                            }
                            condition_close(&pool->condition);
                        }
                        mutex_close(&pool->mutex);
                    }
                    free(pool->queue);
                    free(pool->thread_bits);
                }                
            }
            while (ind--) free(pool->storage[ind]);
            free(pool->storage);
        }
        free(pool);
    }    
    return NULL;
}

size_t thread_pool_dispose(struct thread_pool *pool, size_t *p_pend)
{
    if (!pool) return 1;

    size_t res = 0;
    mutex_acquire(&pool->mutex);
    pool->terminate = pool->cnt;
    mutex_release(&pool->mutex);
    condition_signal(&pool->condition);

    for (size_t i = 0; i < pool->cnt; i++)
    {
        uintptr_t temp = 0;
        thread_wait(&pool->threads[i], (thread_return *) &temp);
        thread_close(&pool->threads[i]);

        if ((thread_return) temp) res++;
    }

    tls_close(&pool->tls);
    condition_close(&pool->condition);
    mutex_close(&pool->mutex);
    
    if (p_pend) *p_pend = pool->queue->cnt;

    free(pool->queue);
    free(pool->thread_bits);
    for (size_t i = 0; i < pool->cnt; free(pool->storage[i++]));
    free(pool->storage);
    free(pool);
    return res;
}
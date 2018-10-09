#include "Common.h"
#include "Debug.h"
#include "Semaphore.h"
#include "Threading.h"
#include "ThreadingSupp.h"
#include "x86_64/Spinlock.h"
#include "x86_64/Tools.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
    size_t capacity, begin, count;
    task *tasks[]; // Array with copies of task pointers
} taskq;

struct threadPool
{
    taskq *queue;
    task *volatile *temp;
    volatile size_t active, terminate;
    spinlockHandle spinqueue;
    mutexHandle mutexthread;
    conditionHandle condthread;
    semaphoreHandle semsched;
    size_t count;
    threadHandle handlers[];
};

static taskq *taskqCreate(size_t inisize)
{
    taskq *res = malloc(sizeof *res + inisize * sizeof *res->tasks);
    if (!res) return NULL;

    res->capacity = inisize;
    res->begin = res->count = 0;

    return res;
}

static bool taskqUpgrade(taskq **pqueue, size_t newcapacity)
{
    if (newcapacity <= (*pqueue)->capacity) return 1; // No need to shrink the queue
    
    taskq *res = realloc(*pqueue, sizeof *res + newcapacity * sizeof *res->tasks);
    if (!res) return 0;

    size_t left = res->begin + res->count;

    if (left > res->capacity)
    {
        if (left > newcapacity)
        {
            memcpy(res->tasks + res->capacity, res->tasks, (newcapacity - res->capacity) * sizeof *res->tasks);
            memmove(res->tasks, res->tasks + newcapacity - res->capacity, (left - newcapacity) * sizeof *res->tasks);
        }
        else memcpy(res->tasks + res->capacity, res->tasks, (left - res->capacity) * sizeof *res->tasks);
    }
    
    res->capacity = newcapacity;
    *pqueue = res;

    return 1;
}

static task *taskqPeek(taskq *restrict queue, size_t offset)
{
    if (queue->begin + offset >= queue->capacity) return queue->tasks[queue->begin + offset - queue->capacity];
    return queue->tasks[queue->begin + offset];
}

// In the next two functions it is assumed that queue->capacity >= queue->count + count
static void taskqEnqueueLow(taskq *restrict queue, task *newtasks, size_t count)
{
    size_t left = queue->begin + queue->count;
    if (left >= queue->capacity) left -= queue->capacity;
        
    char *restrict k = (char *) newtasks;

    if (left + count > queue->capacity)
    {
        for (size_t i = left; i < queue->capacity; queue->tasks[i++] = (task *) k, k += sizeof(task) + ((task *) k)->diff);
        for (size_t i = 0; i < left + count - queue->capacity; queue->tasks[i++] = (task *) k, k += sizeof(task) + ((task *) k)->diff);
    }
    else 
        for (size_t i = left; i < left + count; queue->tasks[i++] = (task *) k, k += sizeof(task) + ((task *) k)->diff);

    queue->count += count;
}

static void taskqEnqueueHigh(taskq *restrict queue, task *newtasks, size_t count)
{
    char *restrict k = (char *) newtasks;
    
    if (count > queue->begin)
    {
        for (size_t i = queue->begin; i; queue->tasks[--i] = (task *) k, k += sizeof(task) + ((task *) k)->diff);
        queue->begin += queue->capacity - count;
        for (size_t i = queue->capacity; i > queue->begin; queue->tasks[--i] = (task *) k, k += sizeof(task) + ((task *) k)->diff);
    }
    else
    {
        for (size_t i = queue->begin; i > queue->begin - count; queue->tasks[--i] = (task *) k, k += sizeof(task) + ((task *) k)->diff);
        queue->begin -= count;
    }

    queue->count += count;    
}

static void taskqDequeue(taskq *restrict queue, size_t offset)
{
    if (offset)
    {
        if (queue->begin + offset >= queue->capacity) queue->tasks[queue->begin + offset - queue->capacity] = queue->tasks[queue->begin];
        else queue->tasks[queue->begin + offset] = queue->tasks[queue->begin];
    }

    queue->count--;
    queue->begin++;
    if (queue->begin >= queue->capacity) queue->begin -= queue->capacity;
}

bool threadPoolEnqueueTasks(threadPool *restrict pool, task *newtasks, size_t count, bool high)
{
    spinlockAcquire(&pool->spinqueue);
    
    if (!taskqUpgrade(&pool->queue, pool->queue->count + count)) // Queue extension if required
    {
        spinlockRelease(&pool->spinqueue);
        return 0;
    }
    
    (high ? taskqEnqueueHigh : taskqEnqueueLow)(pool->queue, newtasks, count);

    spinlockRelease(&pool->spinqueue);
    return 1;
}

size_t threadPoolGetCount(threadPool *restrict pool)
{
    return pool->count;
}

///////////////////////////////////////////////////////////////////////////////

threadReturn threadProc(threadPool *restrict pool) // General thread routine
{
    uintptr_t threadres = 1;

    for (;;)
    {
        task *tsk = NULL;
        for (size_t i = 0; !tsk && i < pool->count; tsk = (task *) uint64ExchangeInterlocked((uint64_t *) &pool->temp[i++], (uint64_t) tsk));
        
        if (tsk)
        {
            if ((*tsk->callback)(tsk->args, tsk->context)) // Here we execute the task routine
                if (tsk->aggregator) tsk->aggregator(tsk->amem, tsk->sync + tsk->offset); else;
            else threadres = 0;
        }
        else
        {
            mutexAcquire(&pool->mutexthread);
            pool->active--;
            semaphoreSignal(&pool->semsched);

            if (pool->terminate && !pool->active)
            {
                pool->terminate--;
                mutexRelease(&pool->mutexthread);                         
                conditionSignal(&pool->condthread);
                return (threadReturn) threadres;
            }

            conditionSleep(&pool->condthread, &pool->mutexthread);

            pool->active++;
            mutexRelease(&pool->mutexthread);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

threadPool *threadPoolCreate(size_t count, size_t initaskcount)
{
    size_t ind = 0;
    threadPool *restrict pool = calloc(1, sizeof *pool + count * sizeof *pool->handlers);
    if (!pool) return NULL;
        
    pool->count = pool->active = count;
    pool->terminate = 0;

    pool->temp = calloc(pool->count, sizeof *pool->temp);
    if (!pool->temp) goto error;

    pool->queue = taskqCreate(initaskcount);
    if (!pool->queue) goto error;
    
    pool->spinqueue = SPINLOCK_INIT;
    if (!mutexInit(&pool->mutexthread)) goto error;
    if (!conditionInit(&pool->condthread)) goto error;
    if (!semaphoreInit(&pool->semsched, 0)) goto error;
    
    for (; ind < pool->count && threadInit(pool->handlers + ind, (threadCallback) threadProc, (void *) pool); ind++);
    if (ind < pool->count) goto error;

    return pool;

error:
    while (ind--)
    {
        threadTerminate(pool->handlers + ind);
        threadClose(pool->handlers + ind);
    }

    mutexClose(&pool->mutexthread);
    conditionClose(&pool->condthread);
    semaphoreClose(&pool->semsched);
    free(pool->queue);
    free(pool->temp);
    free(pool);

    return NULL;
}

void threadPoolSchedule(threadPool *pool)
{
    for (bool fail = 0;;)
    {
        semaphoreSleep(&pool->semsched, 0, 1);
        if (!pool->active)
        {
            fail = 1;
        }
        
        for (size_t ind = 0;;)
        {
            for (; ind < pool->count && pool->temp[ind]; ind++);

            if (ind < pool->count)
            {
                bool brk = 0;
                size_t seek = 0;
                spinlockAcquire(&pool->spinqueue);

                for (; seek < pool->queue->count; seek++)
                {
                    task *temp = taskqPeek(pool->queue, seek);
                    if (temp->condition && !temp->condition(temp->cmem, temp->sync)) continue;

                    taskqDequeue(pool->queue, seek);
                    pool->temp[ind] = temp;
                    
                    conditionSignal(&pool->condthread);
                    break;
                }

                if (seek < pool->queue->count) fail = 0;
                else brk = 1;
                spinlockRelease(&pool->spinqueue);

                if (brk) break;
            }
            else
            {
                fail = 0;
                break;
            }
        }

        if (fail)
        {
            size_t ind = 0;
            for (; ind < pool->count && !pool->temp[ind]; ind++);
            
            if (ind < pool->count) 
            {
                conditionBroadcast(&pool->condthread);
                fail = 0;
            }
            else break;
        }
    }
}

bool threadPoolDispose(threadPool *pool)
{
    if (!pool) return 1;

    uintptr_t res = 1;

    mutexAcquire(&pool->mutexthread);
    pool->terminate = pool->count;
    mutexRelease(&pool->mutexthread);
    conditionBroadcast(&pool->condthread);
    
    for (size_t i = 0; i < pool->count; i++)
    {
        uintptr_t temp = 0;
        threadWait(pool->handlers + i, (threadReturn *) &temp);
        threadClose(pool->handlers + i);

        res &= temp;
    }

    mutexClose(&pool->mutexthread);
    conditionClose(&pool->condthread);
    semaphoreClose(&pool->semsched);
    free(pool->queue);
    free(pool->temp);
    free(pool);

    return !!res;
}
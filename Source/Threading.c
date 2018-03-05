#include "Debug.h"
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

typedef struct
{
    size_t id, datasz;
    char data[]; // Array with thread-local data 
} storageLayout;

struct threadPool
{
    taskq *queue;
    spinlockHandle spinlock, startuplock;
    mutexHandle mutex;
    conditionHandle condition;
    tlsHandle tls;
    storageLayout **storage;
    volatile size_t active, terminate;
    size_t count;
    uint8_t *threadbits;
    threadHandle handles[];
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
    
    size_t sz = newcapacity;
    if (!sizeFusedMulAdd(&sz, sizeof (task), sizeof (taskq))) return 0;

    taskq *res = realloc(*pqueue, sz);
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
       
    if (left + count > queue->capacity)
    {
        for (size_t i = left; i < queue->capacity; queue->tasks[i++] = newtasks++);
        for (size_t i = 0; i < left + count - queue->capacity; queue->tasks[i++] = newtasks++);
    }
    else 
        for (size_t i = left; i < left + count; queue->tasks[i++] = newtasks++);

    queue->count += count;
}

static void taskqEnqueueHigh(taskq *restrict queue, task *newtasks, size_t count)
{
    size_t left = queue->begin + queue->capacity - count;
    if (left >= queue->capacity) left -= queue->capacity;
        
    if (left > queue->begin)
    {
        for (size_t i = left; i < queue->capacity; queue->tasks[i++] = newtasks++);
        for (size_t i = 0; i < queue->begin; queue->tasks[i++] = newtasks++);
    }
    else
        for (size_t i = left; i < queue->begin; queue->tasks[i++] = newtasks++);

    queue->begin = left;
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

// Searches and removes tasks from thread pool queue (commonly used by cleanup routines to remove pending tasks which will be never executed)
size_t threadPoolRemoveTasks(threadPool *pool, task *tasks, size_t count)
{
    size_t fndr = 0;
    
    spinlockAcquire(&pool->spinlock);

    for (size_t i = 0; i < count; i++)
    {
        for (size_t j = 0; j < pool->queue->count;)
        {
            if (taskqPeek(pool->queue, j) == &tasks[i])
            {
                taskqDequeue(pool->queue, j);
                fndr++;
            }
            else j++;
        }        
    }

    spinlockRelease(&pool->spinlock);

    return fndr;
}

bool threadPoolEnqueueTasks(threadPool *pool, task *newtasks, size_t count, bool high)
{
    spinlockAcquire(&pool->spinlock);
    
    if (!taskqUpgrade(&pool->queue, pool->queue->count + count)) // Queue extension if required
    {
        spinlockRelease(&pool->spinlock);
        return 0;
    }
    
    (high ? taskqEnqueueHigh : taskqEnqueueLow)(pool->queue, newtasks, count);

    spinlockRelease(&pool->spinlock);
    conditionBroadcast(&pool->condition);
        
    return 1;
}

size_t threadPoolGetCount(threadPool *pool)
{
    return pool->count;
}

// Determines current thread identifier. Returns '-1' for the main thread.
size_t threadPoolFetchThredId(threadPool *pool)
{
    storageLayout **storage = tlsFetch(&pool->tls);
    if (storage) return (*storage)->id;
    return SIZE_MAX;
}

// Returns pointer to the thread-local data for the current thread. Returns 'NULL' for the main thread.
void *threadPoolFetchThreadData(threadPool *pool, size_t *ptid)
{
    storageLayout **storage = tlsFetch(&pool->tls);

    if (storage)
    {
        if (ptid) *ptid = (*storage)->id;
        return (*storage)->data;
    }

    if (ptid) *ptid = SIZE_MAX;
    return NULL;
}

///////////////////////////////////////////////////////////////////////////////

static inline size_t threadProcStartup(threadPool *pool)
{
    size_t res = SIZE_MAX;    
    spinlockAcquire(&pool->startuplock);

    for (size_t i = 0; i < BYTE_CNT(pool->count); i++)
    {
        uint8_t j = uint8BitScanForward(~pool->threadbits[i]);

        if ((uint8_t) ~j)
        {
            res = (i << 3) + j;
            tlsAssign(&pool->tls, &pool->storage[res]);
            bitSet(pool->threadbits, res);
            break;
        }        
    }

    spinlockRelease(&pool->startuplock);
    return res;
}

static inline void threadProcShutdown(threadPool *pool, size_t id)
{
    spinlockAcquire(&pool->startuplock);
    bitReset(pool->threadbits, id);
    spinlockRelease(&pool->startuplock);
}

static threadReturn threadProc(threadPool *pool) // General thread routine
{
    size_t id = threadProcStartup(pool);
    uintptr_t threadres = 1;
        
    for (;;)
    {
        task *tsk = NULL;
        spinlockAcquire(&pool->spinlock);

        for (size_t i = 0; i < pool->queue->count; i++)
        {
            task *temp = taskqPeek(pool->queue, i);
            if (temp->cond && !temp->cond(temp->condmem, temp->condarg)) continue;
            
            tsk = temp;
            taskqDequeue(pool->queue, i);
            break;
        }

        spinlockRelease(&pool->spinlock);

        if (tsk)
        {
            if (!tsk->callback || (*tsk->callback)(tsk->arg, tsk->context)) // Here we execute the task routine
            {
                if (tsk->asucc) tsk->asucc(tsk->asuccmem, tsk->asuccarg);
                conditionBroadcast(&pool->condition);
            }
            else
            {
                if (tsk->afail) tsk->afail(tsk->afailmem, tsk->afailarg);
                threadres = 0;
            }
        }
        else
        {
            mutexAcquire(&pool->mutex);
            pool->active--;

            // Is it the time to exit the thread?..
            if (pool->terminate && !pool->active)
            {
                pool->terminate--;
                mutexRelease(&pool->mutex);
                conditionSignal(&pool->condition);
                
                threadProcShutdown(pool, id);
                return (threadReturn) threadres;
            }

            // Time to sleep...
            conditionSleep(&pool->condition, &pool->mutex);

            pool->active++;
            mutexRelease(&pool->mutex);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

threadPool *threadPoolCreate(size_t count, size_t initaskcnt, size_t storagesz)
{
    size_t ind = 0;
    threadPool *pool = calloc(1, sizeof *pool + count * sizeof *pool->handles);
    if (!pool) return NULL;
    
    pool->count = pool->active = count;
    pool->terminate = 0;

    pool->storage = calloc(pool->count, sizeof *pool->storage);
    if (!pool->storage)  goto ERR(0);

    for (size_t i = 0; i < pool->count; i++)
    {
        pool->storage[i] = calloc(1, sizeof **pool->storage + storagesz);
        if (!pool->storage[i]) goto ERR(0);

        *pool->storage[i] = (storageLayout) { .id = i, .datasz = storagesz };
    }

    pool->threadbits = calloc(BYTE_CNT(pool->count), 1);
    if (!pool->threadbits) goto ERR(0);
    
    pool->queue = taskqCreate(initaskcnt);
    if (!pool->queue) goto ERR(0);
    
    pool->spinlock = pool->startuplock = SPINLOCK_INIT;
    if (!mutexInit(&pool->mutex)) goto ERR(0);
    if (!conditionInit(&pool->condition)) goto ERR(1);
    if (!tlsInit(&pool->tls)) goto ERR(2);
        
    for (; ind < pool->count && threadInit(&pool->handles[ind], (threadCallback) threadProc, (void *) pool); ind++);
    if (ind < pool->count) goto ERR(3);
    
    return pool;

ERR(3):
    while (ind--)
    {
        threadTerminate(pool->handles + ind);
        threadClose(pool->handles + ind);
    }

    tlsClose(&pool->tls);
    
ERR(2):
    conditionClose(&pool->condition);

ERR(1):
    mutexClose(&pool->mutex);

ERR(0):
    free(pool->queue);
    free(pool->threadbits);
    
    if (pool->storage) 
    { 
        for (size_t i = 0; i < pool->count; free(pool->storage[i++])); 
        free(pool->storage);
    }

    free(pool);

    return NULL;
}

size_t threadPoolDispose(threadPool *pool, size_t *ppend)
{
    if (!pool) return 1;

    size_t res = 0;

    mutexAcquire(&pool->mutex);
    pool->terminate = pool->count;
    mutexRelease(&pool->mutex);
    conditionSignal(&pool->condition);

    for (size_t i = 0; i < pool->count; i++)
    {
        uintptr_t temp = 0;
        threadWait(&pool->handles[i], (threadReturn *) &temp);
        threadClose(&pool->handles[i]);

        if ((threadReturn) temp) res++;
    }

    tlsClose(&pool->tls);
    conditionClose(&pool->condition);
    mutexClose(&pool->mutex);
    
    if (ppend) *ppend = pool->queue->count;

    free(pool->queue);
    free(pool->threadbits);

    for (size_t i = 0; i < pool->count; free(pool->storage[i++]));
    free(pool->storage);
    
    free(pool);

    return res;
}
#pragma once

#include "Common.h"

#include <stdbool.h>
#include <stddef.h>

typedef bool (*taskCallback)(void *, void *);
typedef bool (*conditionCallback)(volatile void *, const void *);
typedef void (*aggregatorCallback)(volatile void *, const void *);

typedef struct
{
    taskCallback callback;
    conditionCallback cond;
    aggregatorCallback asucc, afail;
    void *arg, *context, *condarg, *asuccarg, *afailarg;
    volatile void *condmem, *asuccmem, *afailmem;
} task;

// Opaque structure with OS-dependent implementation
typedef struct threadPool threadPool, *threadPoolPtr;

size_t threadPoolRemoveTasks(threadPool *, task *, size_t);
bool threadPoolEnqueueTasks(threadPool *, task *, size_t, bool);
threadPool *threadPoolCreate(size_t, size_t, size_t);
size_t threadPoolDispose(threadPool *, size_t *);
size_t threadPoolGetCount(threadPool *);
size_t threadPoolFetchThredId(threadPool *pool);
void *threadPoolFetchThreadData(threadPool *pool, size_t *ptid);

#include "Debug.h"
#include "Semaphore.h"

#include <stdlib.h>

bool semaphoreInit(semaphoreHandle *psemaphore, size_t value)
{
    if (mutexInit(&psemaphore->mutex))
    {
        if (conditionInit(&psemaphore->condition))
        {
            psemaphore->value = value;
            return 1;
        }

        mutexClose(&psemaphore->mutex);
    }

    return 0;
}

void semaphoreSignal(semaphoreHandle *psemaphore)
{
    mutexAcquire(&psemaphore->mutex);
    psemaphore->value++;
    mutexRelease(&psemaphore->mutex);
    conditionSignal(&psemaphore->condition);
}

void semaphoreSleep(semaphoreHandle *psemaphore, size_t threshold, size_t count)
{
    mutexAcquire(&psemaphore->mutex);
    while (psemaphore->value < threshold + count) conditionSleep(&psemaphore->condition, &psemaphore->mutex);
    psemaphore->value -= count;
    mutexRelease(&psemaphore->mutex);
}

size_t semaphorePeek(semaphoreHandle *psemaphore)
{
    return psemaphore->value;
}

void semaphoreClose(semaphoreHandle *psemaphore)
{
    if (psemaphore)
    {
        mutexClose(&psemaphore->mutex);
        conditionClose(&psemaphore->condition);
    }
}
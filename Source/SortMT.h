#pragma once

#include "Threading.h"
#include "Sort.h"

typedef struct sortMT sortMT;

typedef struct
{
    conditionCallback cond;
    aggregatorCallback asucc;
    volatile void *condmem, *asuccmem;
    void *condarg, *asuccarg;
} sortMTSync;

void sortMTDispose(sortMT *);

sortMT *sortMTCreate(void *, size_t, size_t, compareCallback, void *, threadPool *, sortMTSync *);

#pragma once

#include "Threading.h"

typedef struct {
    conditionCallback cond;
    aggregatorCallback asucc, afail;
    volatile void *condmem, *asuccmem, *afailmem;
    void *condarg, *asuccarg, *afailarg;
} loopMTSync;

typedef struct {
    size_t offset, length;
} loopMTArg;

typedef bool (*loopMTCallback)(loopMTArg *, void *);
typedef struct loopMT loopMT;

loopMT *loopMTCreate(loopMTCallback, size_t, size_t, void *, threadPool *, loopMTSync *);
void loopMTDispose(loopMT *);

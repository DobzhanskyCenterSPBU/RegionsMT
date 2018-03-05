#pragma once

#include "Common.h"
#include "Threading.h"
#include "x86_64/Tools.h"

forceinline task taskBit1(taskCallback callback, conditionCallback cond, void *arg, void *context, volatile void *cmem, volatile void *amem, void *aarg)
{
    return (task)
    {
        .callback = callback,
        .cond = cond,
        .asucc = (aggregatorCallback) bitSet2InterlockedMem,
        .afail = (aggregatorCallback) bitSetInterlockedMem,
        .arg = arg,
        .context = context,
        .condmem = cmem,
        .asuccmem = amem,
        .afailmem = amem,
        .asuccarg = aarg,
        .afailarg = aarg
    };
}

#define TASK_BIT_1_INIT(CALLBACK, COND, ARG, CONTEXT, CMEM, AMEM, AARG) \
    taskBit1((taskCallback) (CALLBACK), (conditionCallback) (COND), (ARG), (CONTEXT), (CMEM), (AMEM), (AARG))

forceinline task taskBit2(taskCallback callback, conditionCallback cond, void *arg, void *context, volatile void *cmem, volatile void *amem, void *carg, void *aarg)
{
    return (task)
    {
        .callback = callback,
        .cond = cond,
        .asucc = (aggregatorCallback) bitSet2InterlockedMem,
        .afail = (aggregatorCallback) bitSetInterlockedMem,
        .arg = arg,
        .context = context,
        .condmem = cmem,
        .asuccmem = amem,
        .afailmem = amem,
        .condarg = carg,
        .asuccarg = aarg,
        .afailarg = aarg
    };
}

#define TASK_BIT_2_INIT(CALLBACK, COND, ARG, CONTEXT, CMEM, AMEM, CARG, AARG) \
    taskBit2((taskCallback) (CALLBACK), (conditionCallback) (COND), (ARG), (CONTEXT), (CMEM), (AMEM), (CARG), (AARG))

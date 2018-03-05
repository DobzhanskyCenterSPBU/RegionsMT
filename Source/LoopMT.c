#include "Common.h"
#include "LoopMT.h"
#include "Memory.h"
#include "Threading.h"

#include <stdarg.h>
#include <stdlib.h>

struct loopMT
{
    volatile size_t succ, fail;
    size_t thres;
    task *tasks;
    loopMTArg *args;
};

void loopMTDispose(loopMT *arg)
{
    if (!arg) return;

    free(arg->tasks);
    free(arg->args);
    free(arg);
}

static bool loopMTTransitionCondition(loopMT *supp, void *arg)
{
    (void) arg;
    return supp->succ + supp->fail == supp->thres;
}

static bool loopMTTransition(loopMT *arg, void *context)
{
    (void) context;
    return !arg->fail;
}

loopMT *loopMTCreate(loopMTCallback callback, size_t offset, size_t length, void *context, threadPool *pool, loopMTSync *sync)
{
    loopMTSync *snc = sync ? sync : &(loopMTSync) { 0 };
    size_t thrdcnt = threadPoolGetCount(pool), taskscnt = 0;
    
    loopMT *res = calloc(1, sizeof *res);
    
    if (!(
        res &&
        arrayInit((void **) &res->tasks, thrdcnt + 1, sizeof *res->tasks) &&
        arrayInit((void **) &res->args, thrdcnt, sizeof *res->args)
        )) goto ERR();
    
    for (size_t i = thrdcnt; length && i; i--)
    {
        size_t temp = length / i;

        res->args[taskscnt] = (loopMTArg) { .offset = offset, .length = temp };
        
        res->tasks[taskscnt] = (task)
        {
            .callback = (taskCallback) callback,
            .cond = snc->cond,
            .asucc = (aggregatorCallback) sizeIncInterlocked,
            .afail = (aggregatorCallback) sizeIncInterlocked,
            .arg = &res->args[taskscnt],
            .context = context,
            .condmem = snc->condmem,
            .asuccmem = &res->succ,
            .afailmem = &res->fail,
            .condarg = snc->condarg,
        };

        offset += temp;
        length -= temp;
        taskscnt++;
    }

    res->thres = taskscnt;
    res->tasks[taskscnt++] = (task)
    {
        .callback = (taskCallback) loopMTTransition,
        .cond = (conditionCallback) loopMTTransitionCondition,
        .asucc = snc->asucc,
        .afail = snc->afail,
        .arg = res,
        .condmem = res,
        .asuccmem = snc->asuccmem,
        .asuccarg = snc->asuccarg,
        .afailmem = snc->afailmem,
        .afailarg = snc->afailarg
    };
        
    if (!threadPoolEnqueueTasks(pool, res->tasks, taskscnt, 1)) goto ERR();
        
    return res;

ERR():
    loopMTDispose(res);
    return NULL;
}

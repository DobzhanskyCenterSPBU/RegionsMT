#include "Common.h"
#include "Debug.h"
#include "Memory.h"
#include "Sort.h"
#include "SortMT.h"
#include "Threading.h"
#include "x86_64/Tools.h"

#include <string.h>
#include <stdlib.h>

typedef struct
{
    void *arr, *temp;
    size_t cnt;
    size_t sz;
    compareCallback comp;
    void *context;
} sortContext;

typedef struct
{
    size_t off, len;
} sortArgs;

typedef struct
{
    size_t off, pvt, len;
} mergeArgs;

struct sortMT
{
    sortContext context;
    sortArgs *sargs;
    mergeArgs *margs;
    task *tasks;
    uint8_t *sync; // Last two bits in 'res->sync' are not in use!
    size_t *args;
};

static void mergeSortedArrays(void *restrict arr, void *restrict temp, size_t pvt, size_t cnt, size_t sz, compareCallback comp, void *context)
{
    const size_t tot = cnt * sz, mid = pvt * sz;
    size_t i = 0, j = mid;
        
    for (size_t k = 0; k < tot; k += sz)
    {
        if (i < mid && (j == tot || comp((char *) arr + j, (char *) arr + i, context))) 
            memcpy((char *) temp + k, (char *) arr + i, sz), i += sz;
        else 
            memcpy((char *) temp + k, (char *) arr + j, sz), j += sz;
    }

    memcpy(arr, temp, tot);
}

static bool mergeThreadProc(mergeArgs *args, sortContext *context)
{
    const size_t off = args->off * context->sz;
    mergeSortedArrays((char *) context->arr + off, (char *) context->temp + off, args->pvt, args->len, context->sz, context->comp, context->context);
    return 1;
}

static bool sortThreadProc(sortArgs *args, sortContext *context)
{
    const size_t off = args->off * context->sz;
    quickSort((char *) context->arr + off, args->len, context->sz, context->comp, context->context);
    return 1;
}

void sortMTDispose(sortMT *arg)
{
    if (!arg) return;
    
    free(arg->margs);
    free(arg->sargs);
    free(arg->tasks);
    free(arg->sync);
    free(arg->args);
    free(arg->context.temp);
    free(arg);
}

sortMT *sortMTCreate(void *arr, size_t cnt, size_t sz, compareCallback comp, void *context, threadPool *pool, sortMTSync *sync)
{
    sortMTSync *snc = sync ? sync : &(sortMTSync) { 0 };
    
    size_t dep = sizeBitScanReverse(threadPoolGetCount(pool));
    if (dep + 1 > (sizeof (size_t) << 3)) dep = (sizeof (size_t) << 3) - 1;
    
    const size_t scnt = SIZE_C(1) << dep, mcnt = scnt - 1;

    sortMT *res = calloc(1, sizeof *res);
    if (!res) goto ERR();

    res->context = (sortContext) { .arr = arr, .cnt = cnt, .sz = sz, .comp = comp, .context = context };
    
    if (!(
        arrayInit((void **) &res->sargs, scnt, sizeof *res->sargs) &&
        arrayInit((void **) &res->margs, mcnt, sizeof *res->margs) &&
        arrayInitClear((void **) &res->sync, BYTE_CNT(scnt << 1), 1) &&
        arrayInit((void **) &res->context.temp, cnt, sz)
        )) goto ERR();

    res->sargs[0] = (sortArgs) { 0, cnt };
    
    for (uint8_t i = 0; i < dep; i++)
    {
        const size_t jm = SIZE_C(1) << i, jr = SIZE_C(1) << (dep - i - 1), jo = scnt - (jm << 1), ji = jr << 1;
        for (size_t j = 0, k = 0; j < jm; j++, k += ji)
        {
            const size_t l = res->sargs[k].len, hl = l >> 1;
            res->sargs[k].len = hl;
            res->sargs[k + jr] = (sortArgs) { res->sargs[k].off + hl, l - hl };
            res->margs[jo + j] = (mergeArgs) { res->sargs[k].off, hl, l };
        }
    }
           
    // Assigning merging tasks (except the last task)
    if (mcnt)
    {
        res->tasks = malloc((scnt + mcnt) * sizeof *res->tasks);
        if (!res->tasks) goto ERR();
        
        res->args = malloc((scnt + mcnt - 1) * sizeof *res->args);
        if (!res->args) goto ERR();
        
        for (size_t i = 0; i < scnt; i++)
        {
            res->args[i] = i;
            
            res->tasks[i] = (task)
            {
                .callback = (taskCallback) sortThreadProc,
                .cond = snc->cond,
                .asucc = (aggregatorCallback) bitSetInterlockedMem,
                .arg = &res->sargs[i],
                .context = &res->context,
                .condmem = snc->condmem,
                .asuccmem = res->sync,
                .condarg = snc->condarg,
                .asuccarg = &res->args[i]
            };
        }
                
        for (size_t i = 0, j = scnt; i < mcnt - 1; i++, j++)
        {
            res->args[j] = j;
            
            res->tasks[j] = (task)
            {
                .callback = (taskCallback) mergeThreadProc,
                .cond = (conditionCallback) bitTest2Mem,
                .asucc = (aggregatorCallback) bitSetInterlockedMem,
                .arg = &res->margs[i],
                .context = &res->context,
                .condmem = res->sync,
                .asuccmem = res->sync,
                .condarg = &res->args[i << 1],
                .asuccarg = &res->args[j]
            };
        }

        // Last merging task requires special handling
        res->tasks[scnt + mcnt - 1] = (task)
        {
            .callback = (taskCallback) mergeThreadProc,
            .cond = (conditionCallback) bitTest2Mem,
            .asucc = snc->asucc,
            .arg = &res->margs[mcnt - 1],
            .context = &res->context,
            .condmem = res->sync,
            .asuccmem = snc->asuccmem,
            .condarg = &res->args[(mcnt - 1) << 1],
            .asuccarg = snc->asuccarg
        };
    }
    else
    {
        res->tasks = malloc(sizeof *res->tasks);
        if (!res->tasks) goto ERR();
        
        *res->tasks = (task)
        {
            .callback = (taskCallback) sortThreadProc,
            .cond = snc->cond,
            .asucc = snc->asucc,
            .arg = &res->sargs[0],
            .context = &res->context,
            .condmem = snc->condmem,
            .asuccmem = snc->asuccmem,
            .condarg = snc->condarg,
            .asuccarg = snc->asuccarg
        };
    }
    
    if (!threadPoolEnqueueTasks(pool, res->tasks, mcnt + scnt, 1)) goto ERR();
    return res;

ERR():
    sortMTDispose(res);
    return NULL;
}

#include "LoopMT.h"
#include "Memory.h"
#include "Serialize.h"
#include "Wrappers.h"

#include <stdio.h>

typedef struct {
    size_t id, sz, off, len;
} bdfCatalogEntry; // BDF is acronym for binary data file 

typedef struct {
    size_t cnt;
    bdfCatalogEntry entries[];
} bdfCatalog;

typedef struct {
    size_t off, len;
} bdfEnd;

static bdfCatalog *bdfCatalogRead(const char *input)
{
    bdfCatalog *res = NULL;
    
    FILE *f = fopen(input, "rb");
    if (!f) goto ERR();

    fseek64(f, 0, SEEK_END);
    size_t sz = ftell64(f);

    if (sz <= sizeof (bdfEnd) || fseek64(f, sz - sizeof(bdfEnd), SEEK_SET)) goto ERR();

    bdfEnd end;
    if (fread(&end, sizeof end, 1, f) != sizeof end) goto ERR();
    
    size_t cnt = end.len / sizeof (bdfCatalogEntry);
    
    if (
        !flexArrayInit((void **) &res, cnt, sizeof *res->entries, sizeof *res) ||
        fseek64(f, end.off, SEEK_SET) ||
        fread(res->entries, sizeof *res->entries, cnt, f) != cnt
        ) goto ERR();
    
    fclose(f);
    res->cnt = cnt;
    return res;

ERR():
    free(res);
    if (f) fclose(f);
    return NULL;
}

typedef struct {
    const char *file;
    void *base;
} partialProcContext;

static bool bdfPartialReadProc(loopMTArg *arg, partialProcContext *context)
{
    FILE *f = fopen(context->file, "rb");
    if (!f) return 0;
    
    bool succ = !fseek64(f, arg->offset, SEEK_SET) && fread(context->base, 1, arg->length, f) == arg->length;
    
    fclose(f);
    return succ;
}

static bool bdfPartialWriteProc(loopMTArg *arg, partialProcContext *context)
{
    FILE *f = fopen(context->file, "wb");
    if (!f) return 0;

    bool succ = !fseek64(f, arg->offset, SEEK_SET) && fwrite(context->base, 1, arg->length, f) == arg->length;

    fclose(f);
    return succ;
}

typedef struct {
    ptrdiff_t off, offcnt;
    size_t sz;
} bdfsectionsch;

typedef struct {
    bdfsectionsch *sectionsch;
    size_t cnt;
} bdfsch;

typedef struct {
    volatile size_t succ, fail;
    size_t lmtcnt, thresh;
    task tsk;
    loopMTSync sync;
    partialProcContext context;
    loopMT *lmt[];
} bdfResult;

static bool bdfTransition(bdfResult *res, void *context)
{
    (void *) context;
    return !res->fail;
}

static bool bdfCondition(bdfResult *res, void *arg)
{
    (void *) arg;
    return res->fail + res->succ == res->thresh;
}

static bdfResult *bdfReadCreate(bdfsch *sch, const char *input, void *base, threadPool *pool, loopMTSync *sync)
{
    size_t ind = 0;
    bdfCatalog *cat = bdfCatalogRead(input);
    if (!cat) return 0;
    
    bdfResult *res = NULL;
    
    // Computing required space
    for (size_t i = 0; i < cat->cnt; i++)
    {
        size_t id = cat->entries[i].id, sz = cat->entries[i].sz;
        
        if (id < sch->cnt && sz == sch->sectionsch[id].sz) *(size_t *) memberof(base, sch->sectionsch[id].offcnt) += cat->entries[i].len / sz;
        else goto ERR();
    }
    
    // Allocating space
    for (; ind < sch->cnt; ind++)
    {
        size_t cnt = *(size_t *) memberof(base, sch->sectionsch[ind].offcnt);
        
        void *ptr = malloc(cnt * sch->sectionsch[ind].sz);
        if (!ptr) goto ERR();
        
        *(void **) memberof(base, sch->sectionsch[ind].off) = ptr;
    }
    
    if (!flexArrayInitClear((void **) &res, cat->cnt, sizeof *res->lmt, sizeof *res)) goto ERR();
    
    res->context = (partialProcContext) { .file = input, .base = base };
    res->sync = (loopMTSync) {
        .cond = sync->cond,
        .condmem = sync->condmem,
        .condarg = sync->condarg,
        .asucc = (aggregatorCallback) sizeIncInterlocked,
        .afail = (aggregatorCallback) sizeIncInterlocked,
        .asuccmem = &res->succ,
        .afailmem = &res->fail
    };
    
    res->thresh = cat->cnt;
    
    res->tsk = (task) {
        .callback = (taskCallback) bdfTransition,
        .cond = (conditionCallback) bdfCondition,
        .asucc = sync->asucc,
        .afail = sync->afail,
        .arg = res,
        .condmem = res,
        .asuccmem = sync->asuccmem,
        .afailmem = sync->afailmem,
        .asuccarg = sync->asuccarg,
        .afailarg = sync->afailarg
    };

    if (!threadPoolEnqueueTasks(pool, &res->tsk, 1, 1)) goto ERR();

    // Assigning handlers
    for (size_t i = 0; i < cat->cnt; i++)
    {
        res->lmt[i] = loopMTCreate((loopMTCallback) bdfPartialReadProc, cat->entries[i].off, cat->entries[i].len, &res->context, pool, &res->sync);
        if (!res->lmt[i]) sizeIncInterlocked(&res->fail, NULL);
    }   
        
    return res;
    
ERR():
    free(cat);
    free(res);
    while (ind--) free(*(void **) memberof(base, sch->sectionsch[ind].off));
    
    return NULL;
}

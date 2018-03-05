#include "DataLayout.h"
#include "Debug.h"

#include <stdlib.h>

extern inline size_t findBound(size_t, size_t *restrict, size_t);

void testDataClose(testData *data)
{
    free(data->testname);
    free(data->testnamestr);

    free(data->chrlen);
    free(data->chrname);
    free(data->chroff);
    
    free(data->snpname);
    free(data->genename);
    free(data->pos);
    
    free(data->chrnamestr);
    free(data->snpnamestr);
    free(data->genenamestr);
    
    free(data->tmaf);

    free(data->nlpv);
    free(data->qas);
    free(data->maf);
    free(data->allele);
    free(data->rnlpv);
    free(data->rqas);
}

typedef struct {
    size_t a;
} serializeIn;

typedef struct {
    size_t a;
} serializeOut;

typedef struct {
    size_t a;
} serializeContext;

/*
bool serializePrologue(serializeIn *in, serializeOut **pout, serializeContext *context)
{
    loadDataOut *out = *pout = malloc(sizeof *out);
    if (!out) goto ERR();
    
    *out = (loadDataOut) { .meta = LOADDATA_META_INIT(in, out, context) };
    if (!outInfoSetNext(FRAMEWORK_META(in)->out, out)) goto ERR();
    
    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();
    
    if (!pnumTest(FRAMEWORK_META(out)->pnum, LOADDATASUPP_STAT_BIT_CNT)) goto ERR();
    *tsk = TASK_BIT_1_INIT(loadDataThreadPrologue, NULL, out, context, NULL, out->supp.stat, pnumGet(FRAMEWORK_META(out)->pnum, LOADDATASUPP_STAT_BIT_POS_INIT_COMP));
    return 1;
    
ERR():
    logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);
    return 0;
}
*/

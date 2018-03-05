#include "AdjustPValue.h"
#include "TaskMacros.h"

static bool adjustPValueThreadPrologue(adjustPValueOut *args, adjustPValueContext *context)
{

}


void adjustPValueContextDispose(adjustPValueContext *context)
{
    if (context)
    {
        free(context->filename );
    }
}
bool adjustPValuePrologue(adjustPValueIn *in, adjustPValueOut **pout, adjustPValueContext *context)
{
    adjustPValueOut *out = *pout = malloc(sizeof (*out));
    if (!out) goto ERR();

    *out = (adjustPValueOut) { .meta = ADJUSTPVALUE_META_INIT(in, out, context) };
    if (!outInfoSetNext(FRAMEWORK_META(in)->out, out)) goto ERR();

    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();
    
    if (!pnumTest(FRAMEWORK_META(out)->pnum, GENOTYPES_STAT_BIT_CNT)) goto ERR();
    *tsk = (task)
    {
        .callback = adjustPValueThreadPrologue,
        .cond = bitTest2Mem,
        //.asucc = (aggregatorCallback) bitSet2InterlockedMem,
        //.afail = (aggregatorCallback) bitSetInterlockedMem,
        .arg = out,
        .context = context,
        .condmem = in->stat,
        //.asuccmem = amem,
        //.afailmem = amem,
        .condarg = pnumGet(FRAMEWORK_META(out)->pnum, GENOTYPES_STAT_BIT_POS_INIT_COMP),
        //.asuccarg = aarg,
        //.afailarg = aarg
    };
        
        
    //TASK_BIT_2_INIT(genotypesThreadPrologue, NULL, out, context, NULL, out->supp.stat, pnumGet(FRAMEWORK_META(out)->pnum, GENOTYPES_STAT_BIT_POS_INIT_COMP));
    return 1;

    ERR() :
        logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);
    return 0;
}

bool adjustPValueEpilogue(adjustPValueIn *in, adjustPValueOut *out, void *context)
{
    return 1;
}

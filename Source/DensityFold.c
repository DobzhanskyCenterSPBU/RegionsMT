#include "Compare.h"
#include "Debug.h"
#include "DensityFold.h"
#include "Memory.h"
#include "Sort.h"
#include "StringTools.h"
#include "TaskMacros.h"
#include "UnicodeSupp.h"
#include "x86_64/Tools.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool densityFoldHandler(const char *str, size_t len, densityFoldType *ptr, void *context)
{
    (void) context;
    (void) len;

    if (!strcmpci(str, "average")) *ptr = DENSITYFOLD_TYPE_AVERAGE;
    else if (!strcmpci(str, "maximum")) *ptr = DENSITYFOLD_TYPE_MAXIMUM;
    else return 0;

    return 1;
}

static bool densityFoldThreadProc(densityFoldOut *args, densityFoldContext *context)
{
    uint8_t *tbits = NULL;
    
    loadDataRes *restrict ldres = LOADDATA_META(args)->res;
    densityRes *restrict densityres = DENSITY_META(args)->res;
    densityFoldRes *restrict res = &args->res;
    
    if (!(
        arrayInit((void **) &res->fdns, ldres->snpcnt, sizeof *res->fdns) &&
        arrayInitClear((void **) &tbits, BYTE_CNT(ldres->testcnt), sizeof *tbits) &&
        arrayInit((void **) &res->tmap, ldres->testcnt, sizeof *res->tmap))) goto ERR();
    
    for (char *gr = context->group, *ch = gr;; ch++) // Selecting test groups
    {
        if (!*ch || iswhitespace(*ch, 1))
        {
            size_t l = ch - gr;
            if (!l) continue;

            for (size_t i = 0; i < ldres->testcnt; i++)
            {
                for (char *vgr = ldres->testnamestr + ldres->testname[i], *vch = vgr;; vch++)
                {
                    if (!*vch || *vch == '_')
                    {
                        size_t vl = vch - vgr;
                        if (l == vl && !strncmp(gr, vgr, l)) bitSet(tbits, i);

                        vgr = vch + 1;
                    }

                    if (!*vch) break;
                }
            }

            gr = ch + 1;
        }

        if (!*ch) break;
    }

    for (size_t i = 0; i < ldres->testcnt; i++) if (bitTest(tbits, i)) res->tmap[res->tmapcnt++] = i;

    if (!dynamicArrayFinalize((void **) &res->tmap, &res->tmapcnt, sizeof *res->tmap, res->tmapcnt)) goto ERR();
    
    switch (context->type)
    {
    case DENSITYFOLD_TYPE_AVERAGE:
        for (size_t i = 0; i < ldres->snpcnt; i++)
        {
            size_t c = 0;
            double s = 0.;

            for (size_t k = 0; k < res->tmapcnt; k++)
            {
                double x = densityres->dns[i + res->tmap[k] * ldres->snpcnt];
                if (!isnan(x)) s += x, c++;
            }
                        
            res->fdns[i] = c > 0 ? s / c : NaN;
        }

        break;
            
    default:
    case DENSITYFOLD_TYPE_MAXIMUM:
        for (size_t i = 0; i < ldres->snpcnt; i++)
        {
            double r = DBL_MIN;

            for (size_t k = 0; k < res->tmapcnt; k++)
            {
                double x = densityres->dns[i + res->tmap[k] * ldres->snpcnt];
                if (x > r) r = x;
            }

            res->fdns[i] = r;
        }

        break;
    }

    res->rfdns = ranksStable(res->fdns, ldres->snpcnt, sizeof *res->fdns, (compareCallbackStable) float64CompDscNaNStable, NULL);
    if (!res->rfdns) goto ERR(Rank);

    free(tbits);
    
    for (;;)
    {
        return 1;

    ERR():
        logError(FRAMEWORK_META(args)->log, __FUNCTION__, errno);
        break;

    ERR(Rank):
        logMsg(FRAMEWORK_META(args)->log, "ERROR (%s): Unable to compute ranks!\n", __FUNCTION__);
        break;
    }

    free(tbits);
    return 0;
}

void densityFoldContextDispose(densityFoldContext *context)
{
    if (!context) return;
    
    free(context->group);
    free(context);
}

bool densityFoldPrologue(densityFoldIn *in, densityFoldOut **pout, densityFoldContext *context)
{
    densityFoldOut *out = *pout = malloc(sizeof *out);
    if (!out) goto ERR();

    *out = (densityFoldOut) { .meta = DENSITYFOLD_META_INIT(in, out, context) };
    if (!outInfoSetNext(FRAMEWORK_META(in)->out, out)) goto ERR();

    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();
        
    if (!pnumTest(FRAMEWORK_META(out)->pnum, DENSITYFOLDSUPP_STAT_BIT_CNT)) goto ERR();
    *tsk = TASK_BIT_2_INIT(densityFoldThreadProc, bitTestMem, out, context, DENSITY_META(in)->stat, out->supp.stat, pnumGet(FRAMEWORK_META(out)->pnum, DENSITYSUPP_STAT_BIT_POS_TASK_SUCC), pnumGet(FRAMEWORK_META(out)->pnum, DENSITYFOLDSUPP_STAT_BIT_POS_TASK_COMP));
    
    sizeIncInterlocked(DENSITY_META(in)->hold, NULL);
    return  1;

ERR():
    logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);
    return 0;
}

static bool densityFoldThreadClose(densityFoldOut *args, void *context)
{
    (void) context;
    if (!args) return 1;

    densityFoldRes *res = &args->res;

    free(res->tmap);
    free(res->fdns);
    free(res->rfdns);
        
    return 1;
}

static bool densityFoldCloseCondition(densityFoldSupp *supp, void *arg)
{
    (void) arg;

    switch (bitGet2((void *) supp->stat, DENSITYFOLDSUPP_STAT_BIT_POS_TASK_COMP))
    {
    case 0: return 0;
    case 1: return 1;
    case 3: break;
    }
    
    return !supp->hold;
}

bool densityFoldEpilogue(densityFoldIn *in, densityFoldOut *out, void *context)
{
    if (!out) return 0;
    (void) context;
    
    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();

    *tsk = (task)
    {
        .callback = (taskCallback) densityFoldThreadClose,
        .cond = (conditionCallback) densityFoldCloseCondition,
        .asucc = (aggregatorCallback) sizeDecInterlocked,
        .afail = (aggregatorCallback) sizeDecInterlocked,
        .arg = out,
        .asuccmem = DENSITY_META(in)->hold,
        .afailmem = DENSITY_META(in)->hold,
        .condmem = &out->supp,
    };

    return 1;

ERR():
    logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);
    return 0;
}

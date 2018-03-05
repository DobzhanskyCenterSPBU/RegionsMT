#include "Common.h"
#include "Compare.h"
#include "Debug.h"
#include "Memory.h"
#include "Regions.h"
#include "Sort.h"
#include "TaskMacros.h"
#include "x86_64/Tools.h"

#include <errno.h>
#include <math.h>
#include <string.h>

typedef struct
{
    double val;
    size_t ind;
} snp;

// Region tree is stored as inversed tree, represented by slices. Every node contains a reference to parent node.
void regionTreeDispose(region **reg)
{
    for (; reg; free(*reg++));
    free(reg);
}

region *mergeRegions(region *reg, size_t sep, size_t cnt, double *restrict src)
{
    region *restrict res = malloc(cnt * sizeof *res);
    
    for (size_t i = 0, j = sep, k = 0; k < cnt; k++) // Merging by left ends
    {
        if (i < sep && (j == cnt || reg[i].l < reg[j].l)) res[k] = reg[i++];
        else res[k] = reg[j++];
    }
        
    size_t tot = 0;

    for (size_t i = 0, j = i + 1; j < cnt;) // Joining overlapping regions
    {
        if (reg[j].l <= reg[i].r) reg[tot] = (region) { .l = reg[i].l, .r = reg[j++].r };
        else tot++, i = j++;
    }

    /*{    
        res[j].l = res[i].l;
        
        for (size_t k = i + 1; k < cnt; k++)
        {
            if (reg[i].r >= reg[k].l)
            {
                reg[j].r = max(reg[i].r, reg[k].r);
                //reg[j].anc ;
            }
        }
    }*/
    
    return NULL;
}

static region *regionTreeLevel(region *restrict lev, size_t levcnt, snp *restrict loc, size_t loccnt, double *restrict src, size_t srccnt, double thresh, double slope, size_t minreg, size_t decay)
{
    (void) minreg;
    
    const size_t cnt = levcnt + loccnt;
    region *res;
    if (!arrayInit((void **) &res, cnt, sizeof *res)) goto ERR();
    
    for (size_t i = 0, j = 0, k = 0; k < cnt; k++) // Joining
    {
        if (i < levcnt && (j == loccnt || lev[i].l < loc[j].ind)) res[k] = res[i], res[i].anc = &lev[i], i++;
        else res[k] = (region) { .l = loc[j].ind, .r = loc[j].ind, .max = loc[j].ind, .level = loc[j].val }, j++;
    }
    
    if (cnt)
    {
        size_t mv, mf;
        
        for (mv = res[0].l; mv && src[mv - 1] >= thresh; mv--);
        for (mf = mv; mf && mv - mf + 1 < decay && src[mv] - src[mf - 1] <= slope * (mv - mf + 1); mf--);
        res[0].l = mf;
        
        mv = res[0].r;
        while (mv < srccnt && src[mv + 1] >= thresh) mv++;
        for (mf = mv; mf < srccnt && mf + 1 - mv < decay && src[mv] - src[mf + 1] <= slope * (mf + 1 - mv); mf++);
        res[0].r = mf;
    }
    
    //size_t tcnt = 0;
    
    for (size_t i = 1; i < cnt; i++) // Extending
    {
        size_t mv, mf;

        mv = res[0].l;
        while (mv && src[mv - 1] >= thresh) mv--;
        for (mf = mv; mf && mv - mf + 1 < decay && src[mv] - src[mf - 1] <= slope * (mv - mf + 1); mf--);
        res[0].l = mf;

        mv = res[0].r;
        while (mv < srccnt && src[mv + 1] >= thresh) mv++;
        for (mf = mv; mf < srccnt && mf + 1 - mv < decay && src[mv] - src[mf + 1] <= slope * (mf + 1 - mv); mf++);
        res[0].r = mf;
        
    }
    
    
    
    
    // Merging
    
    //for (size_t i = 0; i < levcnt; templev[i] = (region) { .l = lev[i].l, .r = lev[i].r, .max = lev[i].max, .anc = &lev[i] }, i++);
    //for (size_t i = levcnt; i < levcnt + loccnt; templev[i] = (region) { .l = loc[i].ind, .r = loc[i].ind, .max = lev[i].max }, i++);
    
    
    
    
ERR():
    return NULL;
}

static bool snpComp(const snp *a, const snp *b, compareCallbackStableThunk *thunk)
{
    int8_t res = thunk->comp(&a->val, &b->val, NULL);
    if (res > 0 || (!res && a->ind > b->ind)) return 1;
    return 0;
}

// Returns a refined set of local maximizers of the 'source' array
static snp *getRefinedMaxima(const double *src, size_t len, size_t *pcnt)
{
    size_t cap = 0, cnt = 0;
    snp *extrema = NULL;

    // Stage 1 : Selecting extrema (we are not interested in the boundary minimums, although boundary maximums are selected)
    for (size_t r = 0; r < len;)
    {
        size_t l;
        
        do { // two internal loops are required to skip NaN's
            for (l = r; l < len && isnan(src[l]); l++);
            for (r = l + 1; r < len && isnan(src[r]); r++);
        } while (r < len && src[l] <= src[r]);
        
        if (!dynamicArrayTest((void **) &extrema, &cap, sizeof *extrema, cnt + 1)) goto ERR();
        extrema[cnt++] = (snp) { .val = src[l], .ind = l };
        
        do {
            for (l = r; l < len && isnan(src[l]); l++);
            for (r = l + 1; r < len && isnan(src[r]); r++);
        } while (r < len && src[l] >= src[r]);
                
        if (r == len) break; // boundary minimums are not encountered
        
        if (!dynamicArrayTest((void **) &extrema, &cap, sizeof *extrema, cnt + 1)) goto ERR();
        extrema[cnt++] = (snp) { .val = -src[l], .ind = l }; // minimums are negative
    }

    quickSort(extrema, cnt, sizeof *extrema, (compareCallback) snpComp, &(compareCallbackStableThunk) { .comp = (compareCallbackStable) float64CompDscAbsStable });
    
    // Stage 2 : Selecting maximums. If we have found earlier some number of minimums, we should skip the same number of maximums
    size_t scnt = 0, skip = 0;

    for (size_t i = 0; i < cnt; i++)
    {
        if (extrema[i].val > 0)
        {
            if (skip) skip--;
            else extrema[scnt++] = extrema[i];
        }
        else skip++;
    }
    
    if (!dynamicArrayFinalize((void **) &extrema, &cap, sizeof *extrema, scnt)) goto ERR();
    
    *pcnt = scnt;
    return extrema;

ERR():
    free(extrema);
    return NULL;
}

bool regionsThreadProc(regionsThreadProcArgs *args, regionsThreadProcContext *context)
{
    loadDataRes *ldres = LOADDATA_META(context->out)->res;
    densityRes *dres = DENSITY_META(context->out)->res;

    const size_t len = ldres->chrlen[args->chr];
    double *restrict lpv = &dres->lpv[ldres->chroff[args->chr] + args->test * ldres->snpcnt];

    size_t levcnt;
    snp *lev = getRefinedMaxima(lpv, len, &levcnt);
    if (!lev) goto ERR();
    
    const double tol = context->context->tolerance;
    const size_t dep = min(context->context->depth, levcnt);

    if (bitTest(DENSITY_META(context->out)->context->bits, DENSITYCONTEXT_BIT_POS_POS)) // By position
    {

    }
    else // By SNP
    {
        for (size_t i = 1; i < dep; i++) // First maximum not encounered
        {
            size_t j = i;
            for (; i + 1 < levcnt && lev[j].val - lev[i + 1].val < tol; i++);

            
            //regionTreeAddLevel(region *restrict lev, size_t levcnt, snp *restrict loc, size_t loccnt, double *restrict src, size_t srccnt, double thresh, double slope, size_t minreg, size_t decay)


        }
    }

    for (size_t i = 0; i < levcnt; i++)
    {
        printf("%zu, %f\n", lev[i].ind, lev[i].val);
    }

    putchar('\n');

    /*
    
        
        
    }*/
    
ERR():
    free(lev);
    
    return 1;
}

static bool regionsThreadClose(regionsOut *args, void *context)
{
    (void) context;
    
    if (!args) return 1;
    
    regionsSupp *supp = &args->supp;
           
    if (threadPoolRemoveTasks(FRAMEWORK_META(args)->pool, supp->tasks, supp->taskscnt))
        logMsg(FRAMEWORK_META(args)->log, "WARNING (%s): Pending tasks were dropped!\n", __FUNCTION__);

    free(supp->args);
    free(supp->tasks);
    free(supp->intermediate);
        
    /*

    for (size_t i = 0; i < chrcnt; i++)
    {
        for (size_t j = 0; args->supp.regtemp[i][j]; j++) free(args->supp.regtemp[i][j]);

        free(args->supp.regtemp[i]);
        free(args->supp.regtempcnt[i]);
    }

    free(args->supp.regtemp);
    free(args->supp.regtempcnt);
    */
    return 1;
}

static bool regionsThreadEpilogue(regionsOut *args, regionsContext *context)
{
    (void) context;
    
    regionsSupp *supp = &args->supp;
    regionsRes *res = &args->res;
    
    size_t totreg = 0, totlev = 0;
    size_t chrcnt = LOADDATA_META(args)->res->chrcnt;
    
    /*
    for (size_t i = 0; i < chrcnt; i++)
    {
        size_t templev = 0;
        
        for (size_t j = 0; supp->regtemp[i][j]; j++)
        {
            totreg += supp->regtempcnt[i][j];
            templev++;
        }
        
        if (templev > totlev) totlev = templev;
    }
    
    */

    res->levcnt = totlev;
    
    size_t *restrict reglev = res->reglev = malloc(totlev * sizeof *res->reglev);
    if (!reglev) goto ERR();
    
    region *restrict reg = res->reg = malloc(totreg * sizeof *res->reg);
    if (!reg) goto ERR();
    
    size_t ind = 0;
    uintptr_t off = 0;
    /*
    for (size_t i = 0; i + 1 < totlev; i++)
    {
        size_t temp = 0;
        for (size_t j = 0; j < chrcnt; temp += supp->regtempcnt[j++][i]);
        off += reglev[i] = temp;
        
        for (size_t j = 0; j < chrcnt; j++) for (size_t k = 0; k < supp->regtempcnt[j][i]; k++)
        {
            reg[ind] = supp->regtemp[j][i][k];
            reg[ind].anc = reg + (reg[ind].anc - supp->regtemp[j][i + 1]) + off;
            ind++;
        }
    }
    
    if (totlev)
    {
        size_t temp = 0;
        for (size_t j = 0; j < chrcnt; temp += supp->regtempcnt[j++][totlev - 1]);
        reglev[totlev - 1] = temp;
        
        for (size_t j = 0; j < chrcnt; j++) for (size_t k = 0; k < supp->regtempcnt[j][totlev - 1]; k++) reg[ind++] = supp->regtemp[j][totlev - 1][k];
    }
    */
    res->cnt = ind;
    
    return 1;

ERR():
    // All temporary data should be freed
        
    return 0;
}

static bool regionsThreadPrologue(regionsOut *args, regionsContext *context)
{
    regionsSupp *supp = &args->supp;    
    const size_t chrcnt = LOADDATA_META(args)->res->chrcnt, testcnt = LOADDATA_META(args)->res->testcnt;
        
    supp->context = (regionsThreadProcContext) { .out = args, .context = context };

    size_t rtaskscnt = supp->thresh = testcnt * chrcnt;

    if (!(
        arrayInit((void **) &supp->args, rtaskscnt, sizeof *supp->args) &&
        arrayInit((void **) &supp->tasks, rtaskscnt, sizeof *supp->tasks) &&
        arrayInitClear((void **) &supp->intermediate, rtaskscnt, sizeof *supp->intermediate))) goto ERR();
    
    for (size_t i = 0; i < chrcnt; i++)
    {
        for (size_t j = 0; j < testcnt; j++)
        {
            supp->args[supp->taskscnt] = (regionsThreadProcArgs) { .chr = i, .test = j };

            supp->tasks[supp->taskscnt] = (task)
            {
                .callback = (taskCallback) regionsThreadProc,
                .asucc = (aggregatorCallback) sizeIncInterlocked,
                .afail = (aggregatorCallback) sizeIncInterlocked,
                .arg = &supp->args[supp->taskscnt],
                .context = &supp->context,
                .asuccmem = &supp->succ,
                .afailmem = &supp->fail
            };

            supp->taskscnt++;
        }
    }

    if (!threadPoolEnqueueTasks(FRAMEWORK_META(args)->pool, supp->tasks, supp->taskscnt, 1)) goto ERR();
    return 1;

ERR():
    return 0;
}

void regionsContextDispose(regionsContext *context)
{
    free(context);
}

bool regionsPrologue(regionsIn *in, regionsOut **pout, regionsContext *context)
{
    frameworkMetadata *meta = FRAMEWORK_META(in);
    densityMetadata *dmeta = DENSITY_META(in);
    
    regionsOut *out = *pout = malloc(sizeof *out);
    if (!out) goto ERR();
    
    *out = (regionsOut) { .meta = REGIONS_META_INIT(in, out, context) };
    if (!outInfoSetNext(meta->out, out)) goto ERR();
    
    task *tsk = tasksInfoNextTask(meta->tasks);
    if (!tsk) goto ERR();
    
    if (!pnumTest(FRAMEWORK_META(out)->pnum, REGIONSSUPP_STAT_BIT_CNT)) goto ERR();
    *tsk = TASK_BIT_2_INIT(regionsThreadPrologue, bitTestMem, out, context, dmeta->stat, out->supp.stat, pnumGet(FRAMEWORK_META(out)->pnum, DENSITYSUPP_STAT_BIT_POS_TASK_SUCC), pnumGet(FRAMEWORK_META(out)->pnum, REGIONSSUPP_STAT_BIT_POS_INIT_COMP));
       
    sizeIncInterlocked(dmeta->hold, NULL);
    return 1;
    
ERR():
    logError(meta->log, __FUNCTION__, errno);
    return 0;
}

static bool regionsCloseCondition(regionsSupp *supp, void *arg)
{
    (void) arg;
    (void) supp;
    
    return 0;
}

bool regionsEpilogue(regionsIn *in, regionsOut *out, void *context)
{
    if (!out) return 0;
    
    (void) context;
    frameworkMetadata *meta = FRAMEWORK_META(in);
    densityMetadata *dmeta = DENSITY_META(in);
    
    task *tsk = tasksInfoNextTask(meta->tasks);
    if (!tsk) goto ERR();
    
    *tsk = (task)
    {
        .callback = (taskCallback) regionsThreadClose,
        .cond = (conditionCallback) regionsCloseCondition,
        .arg = out,
        .asucc = (aggregatorCallback) sizeDecInterlocked,
        .afail = (aggregatorCallback) sizeDecInterlocked,
        .condmem = &out->supp,
        .asuccmem = dmeta->hold,
        .afailmem = dmeta->hold
    };
    
    return 1;
    
ERR():
    logError(meta->log, __FUNCTION__, errno);
    return 0;
}

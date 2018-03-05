#include "Debug.h"
#include "Density.h"
#include "StringTools.h"
#include "TaskMacros.h"
#include "x86_64/Tools.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gsl/gsl_sf_gamma.h>

/*
#include <gmp.h>
#include <mpfr.h>

int func()
{
    mpfr_t mpa;

    mpfr_init2(mpa, 10);
}

*/

//double gsl_sf_gamma_inc_Q(double a, double x) { return NaN; }

bool densityTypeHandler(const char *str, size_t len, densityType *ptr, void *context)
{
    (void) context;
    (void) len;

    if (!strcmpci(str, "AVERAGE")) *ptr = DENSITY_TYPE_AVERAGE;
    else if (!strcmpci(str, "GAUSSIAN")) *ptr = DENSITY_TYPE_GAUSSIAN;
    else return 0;

    return 1;
}

struct densityCallbackArgs
{
    size_t chr, test;
};

struct densityCallbackContext
{
    densityOut *out;
    densityContext *context;
};

static bool densityThreadAverage(densityCallbackArgs *args, densityCallbackContext *context)
{
    loadDataRes *ldres = LOADDATA_META(context->out)->res;
    densityRes *res = &context->out->res;
    const size_t rad = context->context->radius, wnd = 2 * rad + 1, len = ldres->chrlen[args->chr];
    const size_t offchr = ldres->chroff[args->chr], off = args->test * ldres->snpcnt + offchr;
    double *const restrict in = ldres->nlpv + off, *const restrict dns = res->dns + off, *const restrict lpv = res->lpv + off;
    size_t *const restrict li = res->li + off, *const restrict ri = res->ri + off, *const restrict lc = res->lc + off, *const restrict rc = res->rc + off;
    size_t begin = 0, end = 0, l = 0, r = 0;
    double sum = 0.;

    while (end < len && r < wnd)
    {
        if (!isnan(in[end])) r++, sum += in[end];
        end++;
    }

    for (size_t i = 0; i < len; i++)
    {
        if (!isnan(in[i]))
        {
            while (end < len  && r <= rad)
            {
                if (!isnan(in[end]))
                {
                    r++, sum += in[end];

                    while (l > rad)
                    {
                        if (!isnan(in[begin])) l--, sum -= in[begin];
                        begin++;
                    }
                }

                end++;
            }

            if (sum < 0.) sum = 0.; // Correcting possible computation error

            if (l + r)
            {
                dns[i] = sum / (l + r);
                lpv[i] = -log10(gsl_sf_gamma_inc_Q((double) (l + r), sum)); // Upper incomplete normalized gamma function
            }
            else dns[i] = lpv[i] = NaN;
            
            lc[i] = l++, rc[i] = --r;
            li[i] = begin + offchr, ri[i] = end + offchr - 1;
        }
        else
        {
            dns[i] = lpv[i] = NaN;
            lc[i] = rc[i] = 0;
            li[i] = ri[i] = i + offchr;
        }
    }
    
    return 1;
}

static bool densityThreadAveragePos(densityCallbackArgs *args, densityCallbackContext *context)
{
    loadDataRes *ldres = LOADDATA_META(context->out)->res;
    densityRes *res = &context->out->res;
    const size_t rad = context->context->radius, wnd = 2 * rad + 1, len = ldres->chrlen[args->chr];
    const size_t offchr = ldres->chroff[args->chr], off = args->test * ldres->snpcnt + offchr;
    double *const restrict in = ldres->nlpv + off, *const restrict dns = res->dns + off, *const restrict lpv = res->lpv + off;
    size_t *const restrict pos = ldres->pos + offchr;
    size_t *const restrict li = res->li + off, *const restrict ri = res->ri + off, *const restrict lc = res->lc + off, *const restrict rc = res->rc + off;
    size_t begin = 0, end = 0, l = 0, r = 0;
    double sum = 0.;
    
    while (end < len && pos[end] < wnd + pos[begin])
    {
        if (!isnan(in[end])) r++, sum += in[end];
        end++;
    }

    for (size_t i = 0; i < len; i++)
    {
        if (!isnan(in[i]))
        {
            while (end < len && pos[i] + rad >= pos[end])
            {
                if (!isnan(in[end]))
                {
                    r++, sum += in[end];

                    while (pos[begin] + rad < pos[i])
                    {
                        if (!isnan(in[begin])) l--, sum -= in[begin];
                        begin++;
                    }
                }

                end++;
            }
           
            if (sum < 0.) sum = 0.; // Correcting possible computation error

            if (l + r)
            {
                dns[i] = sum / (l + r);
                lpv[i] = -log10(gsl_sf_gamma_inc_Q((double) (l + r), sum)); // Upper incomplete normalized gamma function
            }
            else dns[i] = lpv[i] = NaN;

            lc[i] = l++, rc[i] = --r;
            li[i] = begin + offchr, ri[i] = end + offchr - 1;
        }
        else
        {
            dns[i] = lpv[i] = NaN;
            lc[i] = rc[i] = 0;
            li[i] = ri[i] = i + offchr;
        }
    }
    
    return 1;
}

///////////////////////////////////////////////////////////////////////////////

static void arrangeArrays(double *an, double *ax, size_t anc, size_t axc, double *uni, double *rep, size_t *unic, size_t *repc)
{
    size_t j = 0;
    (*unic) = (*repc) = 0;

    for (size_t i = 0; i < anc; i++)
    {
        for (; j < axc && ax[j] < an[i]; uni[(*unic)++] = ax[j++]);
        if (ax[j] == an[i]) rep[(*repc)++] = ax[j++];
        else uni[(*unic)++] = an[i];
    }

    for (; j < axc; uni[(*unic)++] = ax[j++]);
}

static double pvalueGaussian(double x, const double *uni, const double *rep, size_t unicnt, size_t repcnt)
{
    (void) x;
    (void) uni;
    (void) rep;
    (void) unicnt;
    (void) repcnt;
    
    return NaN;
}

static bool densityThreadGaussian(densityCallbackArgs *args, densityCallbackContext *context)
{
    bool success = 0;
    loadDataRes *ldres = LOADDATA_META(context->out)->res;
    densityRes *res = &context->out->res;
    const size_t rad = context->context->radius, wnd = 2 * rad + 1, len = ldres->chrlen[args->chr];
    const size_t offchr = ldres->chroff[args->chr], off = args->test * ldres->snpcnt + offchr;
    double *const restrict in = ldres->nlpv + off, *const restrict dns = res->dns + off, *const restrict lpv = res->lpv + off;
    size_t *const restrict li = res->li + off, *const restrict ri = res->ri + off, *const restrict lc = res->lc + off, *const restrict rc = res->rc + off;
    const double smooth = context->context->smooth;

    double *lar = malloc((wnd - 1) * sizeof *lar), *rar = malloc((wnd - 1) * sizeof *rar);
    double *uni = malloc(wnd * sizeof *uni), *rep = malloc(rad * sizeof *rep);

    if (!(lar && rar && uni && rep)) goto ERR();

    uni[0] = 1.; // First unique coefficient is always present

    for (size_t i = 0; i < len; i++)
    {
        if (!isnan(in[i]))
        {
            size_t begin = i, end = i, l = 0, r = 0;
            double a = in[i], b = 1.;

            for (bool next = 1; next;)
            {
                next = 0;

                if (begin && (l < rad || (begin + 1 == len && l + r + 1 < wnd)))
                {
                    next = 1;
                    begin--;

                    if (!isnan(in[begin]))
                    {
                        double dl = (double) (l + 1) / smooth;

                        a += in[begin] * (lar[l] = exp(-.5 * dl * dl));
                        b += lar[l];
                        l++;
                    }
                }

                if (end + 1 < len && (r < rad || (!begin && l + r + 1 < wnd)))
                {
                    next = 1;
                    end++;

                    if (!isnan(in[end]))
                    {
                        double dr = (double) (r + 1) / smooth;

                        a += in[end] * (rar[r] = exp(-.5 * dr * dr));
                        b += rar[r];
                        r++;
                    }
                }
            }

            dns[i] = a / b;
            lc[i] = l, rc[i] = r;
            li[i] = begin + offchr, ri[i] = end + offchr;
            
            // P-value computation
            const bool ord = l < r;
            size_t unicnt, repcnt;

            arrangeArrays(ord ? lar : rar, ord ? rar : lar, ord ? l : r, ord ? r : l, uni + 1, rep, &unicnt, &repcnt); // 'uni[0]' is already set
            lpv[i] = -log10(pvalueGaussian(a, uni, rep, unicnt + 1, repcnt)); // P-value, computed in the independence assumptions
        }
        else
        {
            dns[i] = lpv[i] = NaN;
            lc[i] = rc[i] = 0;
            li[i] = ri[i] = i + offchr;
        }
    }

    success = 1;

ERR():
    free(lar);
    free(rar);
    free(uni);
    free(rep);

    return success;
}

bool densityThreadGaussianPos(densityCallbackArgs *args, densityCallbackContext *context)
{
    bool success = 0;
    loadDataRes *ldres = LOADDATA_META(context->out)->res;
    densityRes *res = &context->out->res;
    const size_t rad = context->context->radius, wnd = 2 * rad + 1, len = ldres->chrlen[args->chr];
    const size_t offchr = ldres->chroff[args->chr], off = args->test * ldres->snpcnt + offchr;
    double *const restrict in = ldres->nlpv + off, *const restrict dns = res->dns + off, *const restrict lpv = res->lpv + off;
    size_t *const restrict pos = ldres->pos + offchr;
    size_t *const restrict li = res->li + off, *const restrict ri = res->ri + off, *const restrict lc = res->lc + off, *const restrict rc = res->rc + off;
    const double smooth = context->context->smooth;

    double *lar = malloc((wnd - 1) * sizeof *lar), *rar = malloc((wnd - 1) * sizeof *rar);
    double *uni = malloc(wnd * sizeof *uni), *rep = malloc(rad * sizeof *rep);

    if (!(lar && rar && uni && rep)) goto ERR();

    uni[0] = 1.; // First unique coefficient is always present

    for (size_t i = 0; i < len; i++)
    {
        if (!isnan(in[i]))
        {
            size_t begin = i, end = i, l = 0, r = 0;
            double a = in[i], b = 1.;

            for (bool next = 1; next;)
            {
                next = 0;

                if (begin && (pos[begin - 1] + rad >= pos[i]))
                {
                    next = 1;
                    begin--;

                    if (!isnan(in[begin]))
                    {
                        double dl = (double) (pos[i] - pos[begin]) / smooth;

                        a += in[begin] * (lar[l] = exp(-.5 * dl * dl));
                        b += lar[l];
                        l++;
                    }
                }

                if (end + 1 < len && (pos[i] + rad >= pos[end + 1]))
                {
                    next = 1;
                    end++;

                    if (!isnan(in[end]))
                    {
                        double dr = (double) (pos[end] - pos[i]) / smooth;

                        a += in[end] * (rar[r] = exp(-.5 * dr * dr));
                        b += rar[r];
                        r++;
                    }
                }
            }

            dns[i] = a / b;
            lc[i] = l, rc[i] = r;
            li[i] = begin + offchr, ri[i] =  end + offchr;

            // P-value computation
            const bool ord = l < r;
            size_t unicnt, repcnt;

            arrangeArrays(ord ? lar : rar, ord ? rar : lar, ord ? l : r, ord ? r : l, uni + 1, rep, &unicnt, &repcnt); // 'uni[0]' is already set
            lpv[i] = -log10(pvalueGaussian(a, uni, rep, unicnt + 1, repcnt)); // P-value, computed in the independence assumptions
        }
        else
        {
            dns[i] = lpv[i] = NaN;
            lc[i] = rc[i] = 0;
            li[i] = ri[i] = i + offchr;
        }
    }

    success = 1;

ERR():
    free(lar);
    free(rar);
    free(uni);
    free(rep);

    return success;
}

static bool densityThreadEpilogueCondition(densitySupp *supp, void *arg)
{
    (void) arg;
    return supp->succ + supp->fail == supp->thresh;
}

static bool densityThreadEpilogue(densityOut *args, densityContext *context)
{
    (void) context;
    return !args->supp.fail;
}

#define DEFRADIUS 10

static bool densityThreadPrologue(densityOut *args, densityContext *context)
{
    const char *strings[] =
    {
        __FUNCTION__,
        "WARNING (%s): Density radius is not provided and set to default: %" PRIu32 "!\n",
        "WARNING (%s): Density smoothing magnitude is provided, but not used!\n",
        "WARNING (%s): Density smoothing magnitude is not provided and set to default: %f!\n",
        "ERROR (%s): Unable to enqueue tasks!\n",
    };

    enum
    {
        STR_FN = 0,
        STR_FR_WR,
        STR_FR_WN,
        STR_FR_WS,
        STR_FR_ET
    };
    
    taskCallback density = NULL;

    if (!bitTest(context->bits, DENSITYCONTEXT_BIT_POS_RADIUS))
    {
        context->radius = DEFRADIUS;
        logMsg(FRAMEWORK_META(args)->log, strings[STR_FR_WR], strings[STR_FN], context->radius);
    }

    switch (context->type) // Density callback selector
    {
    default:
    case DENSITY_TYPE_AVERAGE:
        density = (taskCallback) (bitTest(context->bits, DENSITYCONTEXT_BIT_POS_POS) ? densityThreadAveragePos : densityThreadAverage);
        
        if (bitTest(context->bits, DENSITYCONTEXT_BIT_POS_SMOOTH)) logMsg(FRAMEWORK_META(args)->log, strings[STR_FR_WN], strings[STR_FN]);
        break;

    case DENSITY_TYPE_GAUSSIAN:
        density = (taskCallback) (bitTest(context->bits, DENSITYCONTEXT_BIT_POS_POS) ? densityThreadGaussianPos : densityThreadGaussian);
        
        if (!bitTest(context->bits, DENSITYCONTEXT_BIT_POS_SMOOTH))
        {
            context->smooth = (double) context->radius / 3.;
            logMsg(FRAMEWORK_META(args)->log, strings[STR_FR_WS], strings[STR_FN], context->smooth);
        }
        break;
    }

    size_t lpvcnt = LOADDATA_META(args)->res->pvcnt, testcnt = LOADDATA_META(args)->res->testcnt, chrcnt = LOADDATA_META(args)->res->chrcnt;
    
    densityRes *restrict res = &args->res;
    densitySupp *restrict supp = &args->supp;
        
    if (!(
        arrayInit((void **) &res->dns, lpvcnt, sizeof *res->dns) &&
        arrayInit((void **) &res->lpv, lpvcnt, sizeof *res->lpv) &&
        arrayInit((void **) &res->li, lpvcnt, sizeof *res->li) &&
        arrayInit((void **) &res->ri, lpvcnt, sizeof *res->ri) &&
        arrayInit((void **) &res->lc, lpvcnt, sizeof *res->lc) &&
        arrayInit((void **) &res->rc, lpvcnt, sizeof *res->rc))) goto ERR();
    
    size_t dtaskscnt = supp->thresh = chrcnt * testcnt;
    
    if (dtaskscnt)
    {
        supp->args = malloc(dtaskscnt * sizeof *supp->args);
        if (!supp->args) goto ERR();
    }

    supp->context = malloc(sizeof *supp->context);
    if (!supp->context) goto ERR();

    *supp->context = (densityCallbackContext) { .out = args, .context = context };

    supp->tasks = malloc((dtaskscnt + 1) * sizeof *supp->tasks);
    if (!supp->tasks) goto ERR();

    for (size_t i = 0; i < chrcnt; i++)
    {
        for (size_t j = 0; j < testcnt; j++)
        {
            supp->args[supp->taskscnt] = (densityCallbackArgs) { .chr = i, .test = j };
            
            supp->tasks[supp->taskscnt] = (task)
            {
                .callback = density,
                .asucc = (aggregatorCallback) sizeIncInterlocked,
                .afail = (aggregatorCallback) sizeIncInterlocked,
                .arg = &supp->args[supp->taskscnt],
                .context = supp->context,
                .condmem = LOADDATA_META(args)->stat,
                .asuccmem = &supp->succ,
                .afailmem = &supp->fail,
            };

            supp->taskscnt++;
        }
    }
   
    supp->tasks[supp->taskscnt++] = TASK_BIT_2_INIT(densityThreadEpilogue, densityThreadEpilogueCondition, args, NULL, supp, supp->stat, NULL, pnumGet(FRAMEWORK_META(args)->pnum, DENSITYSUPP_STAT_BIT_POS_TASK_COMP));
   
    if (!threadPoolEnqueueTasks(FRAMEWORK_META(args)->pool, supp->tasks, supp->taskscnt, 1)) goto ERR(Pool);
    return 1;

ERR():
    logError(FRAMEWORK_META(args)->log, strings[STR_FN], errno);
    return 0;

ERR(Pool):
    logMsg(FRAMEWORK_META(args)->log, strings[STR_FR_ET], strings[STR_FN]);
    return 0;
}

void densityContextDispose(densityContext *context)
{
    free(context);
}

bool densityPrologue(densityIn *in, densityOut **pout, densityContext *context)
{
    densityOut *out = *pout = malloc(sizeof *out);
    if (!out) goto ERR();
    
    *out = (densityOut) { .meta = DENSITY_META_INIT(in, out, context), .supp = { .ldstat = LOADDATA_META(in)->stat } };
    if (!outInfoSetNext(FRAMEWORK_META(in)->out, out)) goto ERR();

    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();
   
    if (!pnumTest(FRAMEWORK_META(out)->pnum, DENSITYSUPP_STAT_BIT_CNT)) goto ERR();
    *tsk = TASK_BIT_2_INIT(densityThreadPrologue, bitTestMem, out, context, LOADDATA_META(in)->stat, out->supp.stat, pnumGet(FRAMEWORK_META(out)->pnum, LOADDATASUPP_STAT_BIT_POS_LOAD_SUCC), pnumGet(FRAMEWORK_META(out)->pnum, DENSITYSUPP_STAT_BIT_POS_INIT_COMP));
    
    sizeIncInterlocked(LOADDATA_META(in)->holdload, NULL); // Increase the chromosome data hold counter
    return  1;

ERR():
    logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);
    return 0;
}

static bool densityThreadClose(densityOut *args, void *context)
{
    (void) context;

    if (!args) return 1;

    densityRes *res = &args->res;

    free(res->dns);
    free(res->lpv);
    free(res->li);
    free(res->ri);
    free(res->lc);
    free(res->rc);

    densitySupp *supp = &args->supp;

    // Before disposing task information all corresponding pending tasks should be dropped from the queue    
    if (threadPoolRemoveTasks(FRAMEWORK_META(args)->pool, supp->tasks, supp->taskscnt))
        logMsg(FRAMEWORK_META(args)->log, "WARNING (%s): Pending tasks were dropped!\n", __FUNCTION__);

    free(supp->args);
    free(supp->context);
    free(supp->tasks);
        
    return 1;
}

static bool densityCloseCondition(densitySupp *supp, void *arg)
{
    (void) arg;

    switch (bitGet2((void *) supp->stat, DENSITYSUPP_STAT_BIT_POS_INIT_COMP))
    {
    case 0: return 0;
    case 1: return 1;
    case 3: break;
    }
        
    switch (bitGet2((void *) supp->stat, DENSITYSUPP_STAT_BIT_POS_TASK_COMP))
    {
    case 0: return 0;
    case 1: return 1;
    case 3: break;
    }

    return !supp->hold;
}

bool densityEpilogue(densityIn *in, densityOut *out, void *context)
{
    if (!out) return 0;
    
    (void) context;
    
    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();
        
    *tsk = (task)
    {
        .callback = (taskCallback) densityThreadClose,
        .cond = (conditionCallback) densityCloseCondition,
        .asucc = (aggregatorCallback) sizeDecInterlocked,
        .afail = (aggregatorCallback) sizeDecInterlocked,
        .arg = out,
        .asuccmem = LOADDATA_META(in)->holdload,
        .afailmem = LOADDATA_META(in)->holdload,
        .condmem = &out->supp
    };

    return 1;

ERR():
    logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);
    return 0;
}
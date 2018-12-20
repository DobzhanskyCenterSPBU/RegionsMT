#include "Common.h"
#include "Compare.h"
#include "Debug.h"
#include "Density-Report.h"
#include "DensityFold-Report.h"
#include "Sort.h"
#include "StringTools.h"
#include "TaskMacros.h"
#include "x86_64/Tools.h"

#include <errno.h>
#include <ctype.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool dfReportHandler(const char *str, size_t len, dfReportType *ptr, void *context)
{
    (void) context;
    (void) len;

    if (!strcmpci(str, "density")) *ptr = DFREPORT_TYPE_DENSITY;
    else if (!strcmpci(str, "nlpv")) *ptr = DFREPORT_TYPE_NLPV;
    else if (!strcmpci(str, "qas")) *ptr = DFREPORT_TYPE_QAS;
    else return 0;

    return 1;
}

static bool dfReportThreadProc(dfReportOut *args, dfReportContext *context)
{
    const char *strings[] =
    {
        __FUNCTION__,
        "ERROR (%s): %s: cannot open specified file \"%s\". %s!\n",
        "ERROR (%s): %s: %s!\n",
        "Unable to create report",
        "failed to compute orders"        
    };

    enum
    {
        STR_FN = 0,
        STR_FR_EI,
        STR_FR_EG,
        STR_M_UNA,
        STR_M_ORD
    };
    
    bool succ = 0;

    FILE *f = NULL;
    double *ptr = NULL;
    double *restrict testarr = NULL;
    size_t *restrict testind = NULL;
    uintptr_t *restrict ord = NULL;
    
    loadDataRes *ldres = LOADDATA_META(args)->res;
    
    char tempbuff[TEMP_BUFF] = { '\0' };
    densityFoldRes *densityfoldres = DENSITYFOLD_META(args)->res;
    densityRes *densityres = DENSITY_META(args)->res;

    f = fopen(context->path, "w");
    if (!f) goto ERR(File);       

    double threshold = bitTest(context->bits, DFREPORTCONTEXT_BIT_POS_THRESHOLD) ? context->threshold : DBL_MIN;
    size_t cnt = bitTest(context->bits, DFREPORTCONTEXT_BIT_POS_LIMIT) ? ldres->snpcnt > context->limit ? context->limit : ldres->snpcnt : ldres->snpcnt;

    switch (context->type)
    {
    case DFREPORT_TYPE_NLPV:
        ptr = ldres->nlpv;
        break;

    case DFREPORT_TYPE_QAS:
        ptr = ldres->qas;
        break;

    default:;
    }

    uint8_t semi = (uint8_t) bitTest(context->bits, DFREPORTCONTEXT_BIT_POS_SEMICOLON);

    const char *head = ((const char *[])
    {
        "'v_ind', 'r_density', 'density', 'left_ind', 'right_ind', 'left_count', 'right_count', 'naive_p'\n",
        "'v_ind'; 'r_density'; 'density'; 'left_ind'; 'right_ind'; 'left_count'; 'right_count'; 'naive_p'\n"
        
        /*    "\"Chromosome index\","
        "\"SNP index\","
        "\"SNP name\","
        "\"Position\","
        "\"MAF\","
        "\"Gene name\","
        "\"Rank of Density\","
        "\"Density\","
        "\"Test description\","
        "\"Rank of -log(P-value)\","
        "\" -log(P-value)\","
        "\"Rank of log(QAS)\","
        "\" log(QAS)\"\n",

        "Chromosome index;"
        "SNP index;"
        "SNP name;"
        "Position;"
        "MAF;"
        "Gene name;"
        "Rank of Density;"
        "Density;"
        "Test description;"
        "Rank of -log(P-value);"
        " -log(P-value);"
        "Rank of log(QAS);"
        " log(QAS)\n"*/
    })[semi];

    const char *form = ((const char *[]) {
        "%zu, %zu, %.16e, %zu, %zu, %zu, %zu, %.16e\n",
        "%zu; %zu; %.16e; %zu; %zu; %zu; %zu; %.16e\n"
    //    "%zu," "%zu," "\"%s\"," "%" PRIu32 "," "%f," "\"%s\"," "%" PRIuPTR "," "%f," "\"%s\"," "%" PRIuPTR "," "%f," "%" PRIuPTR "," "%f\n",
    //    "%zu;" "%zu;" "%s;" "%" PRIu32 ";" "%f;" "%s;" "%" PRIuPTR ";" "%f;" "%s;" "%" PRIuPTR ";" "%f;" "%" PRIuPTR ";" "%f\n"
    })[semi];

#   define TABLE_HEAD() fprintf(f, "%s", head)
#   define TABLE_LINE(j, ri, l) \
        if (ri + 1 && densityfoldres->fdns[j] > threshold) \
        { \
            l++; \
            /*size_t chr = findBound(j, ldres->chroff, ldres->chrcnt);*/ \
            size_t lpvind = j + ri * ldres->snpcnt; \
            fprintf(f, \
                form, \
                ri + j * ldres->testcnt + 1, \
                densityfoldres->rfdns[j] + 1, \
                isfinite(densityfoldres->fdns[j]) ? densityfoldres->fdns[j] : DBL_MAX, \
                densityres->li[lpvind] + 1, \
                densityres->ri[lpvind] + 1, \
                densityres->lc[lpvind], \
                densityres->rc[lpvind], \
                densityres->lpv[lpvind] \
                /*chr + 1, \
                j - ldres->chroff[chr] + 1, \
                ldres->snpnamestr + ldres->snpname[j], \
                ldres->pos[j], \
                ldres->maf[j], \
                ldres->genenamestr + ldres->genename[j], \
                densityfoldres->rfdns[j] + 1, \
                densityfoldres->fdns[j], \
                ldres->testnamestr + ldres->testname[ri], \
                ldres->rnlpv[lpvind] + 1, \
                ldres->nlpv[lpvind], \
                ldres->rqas[lpvind] + 1, \
                ldres->qas[lpvind]*/ \
            ); \
        }

    switch (context->type)
    {
    default:
    case DFREPORT_TYPE_DENSITY:
        if (ldres->snpcnt)
        {
            ord = ordersInvert(densityfoldres->rfdns, ldres->snpcnt);
            if (!ord) goto ERR(Order);
        }

        if (bitTest(context->bits, DFREPORTCONTEXT_BIT_POS_HEADER)) TABLE_HEAD();

        for (size_t i = 0, l = 0; l < cnt && i < ldres->snpcnt; i++)
        {
            size_t j = ord[i];
            size_t ri = SIZE_MAX;
            double r = DBL_MIN;

            for (size_t k = 0; k < densityfoldres->tmapcnt; k++)
            {
                size_t ind = j + densityfoldres->tmap[k] * ldres->snpcnt;
                if (ldres->nlpv[ind] > r) r = ldres->nlpv[ind], ri = densityfoldres->tmap[k];
            }
            
            TABLE_LINE(j, ri, l);
        }

        break;

    case DFREPORT_TYPE_NLPV:
    case DFREPORT_TYPE_QAS:
        if (ldres->snpcnt)
        {
            testarr = malloc(ldres->snpcnt * sizeof *testarr);
            if (!testarr) goto ERR();
            
            testind = malloc(ldres->snpcnt * sizeof *testind);
            if (!testind) goto ERR();
        }

        for (size_t i = 0; i < ldres->snpcnt; i++)
        {
            size_t ri = SIZE_MAX;
            double r = DBL_MIN;

            for (size_t k = 0; k < densityfoldres->tmapcnt; k++)
            {
                double x = ptr[i + densityfoldres->tmap[k] * ldres->snpcnt];
                if (x > r) r = x, ri = densityfoldres->tmap[k];
            }

            testarr[i] = r;
            testind[i] = ri;
        }

        ord = ordersStable(testarr, ldres->snpcnt, sizeof *testarr, (compareCallbackStable) float64CompDscStable, NULL);
        if (!ord) goto ERR(Order);

        if (bitTest(context->bits, DFREPORTCONTEXT_BIT_POS_HEADER)) TABLE_HEAD();

        for (size_t i = 0, l = 0; l < cnt && i < ldres->snpcnt; i++)
        {
            size_t j = ord[i];
            size_t ri = testind[j];

            TABLE_LINE(j, ri, l);
        }

        break;
    }
    
#   undef TABLE_LINE
#   undef TABLE_HEAD

    for (;;)
    {
        succ = 1;
        break;
    
    ERR():
        strerror_s(tempbuff, sizeof tempbuff, errno);
        logMsg(FRAMEWORK_META(args)->log, strings[STR_FR_EG], strings[STR_FN], strings[STR_M_UNA], tempbuff);
        break;
        
    ERR(Order):
        logMsg(FRAMEWORK_META(args)->log, strings[STR_FR_EG], strings[STR_FN], strings[STR_M_UNA], strings[STR_M_ORD]);
        break;

    ERR(File):
        strerror_s(tempbuff, sizeof tempbuff, errno);
        logMsg(FRAMEWORK_META(args)->log, strings[STR_FR_EI], strings[STR_FN], strings[STR_M_UNA], context->path, tempbuff);
        break;
    }
    
    free(testind);
    free(testarr);
    free(ord);
    if (f) fclose(f);

    return succ;
}

void dfReportContextDispose(dfReportContext *context)
{
    if (!context) return;
    
    free(context->path);
    free(context);    
}

static bool dfReportThreadProcCondition(dfReportSupp *supp, void *arg)
{
    (void) arg;
    return bitTest((void *) supp->ldstat, LOADDATASUPP_STAT_BIT_POS_TASK_SUCC) && bitTest((void *) supp->dfstat, DENSITYFOLDSUPP_STAT_BIT_POS_TASK_SUCC);
}

bool dfReportPrologue(dfReportIn *in, dfReportOut **pout, dfReportContext *context)
{
    dfReportOut *out = *pout = malloc(sizeof *out);
    if (!out) goto ERR();

    *out = (dfReportOut) { .meta = DFREPORT_META_INIT(in, out, context), .supp = DFREPORT_SUPP_INIT(in, out, context) };
    if (!outInfoSetNext(FRAMEWORK_META(in)->out, out)) goto ERR();

    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();

    if (!pnumTest(FRAMEWORK_META(out)->pnum, DFREPORTSUPP_STAT_BIT_CNT)) goto ERR();
    *tsk = TASK_BIT_1_INIT(dfReportThreadProc, dfReportThreadProcCondition, out, context, &out->supp, out->supp.stat, pnumGet(FRAMEWORK_META(out)->pnum, DFREPORTSUPP_STAT_BIT_POS_TASK_COMP));

    sizeIncInterlocked(DENSITYFOLD_META(in)->hold, NULL);
    return  1;

ERR():
    logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);
    return 0;
}

static bool dfReportThreadClose(dfReportOut *args, void *context)
{
    (void) context;
    if (!args) return 1;
    
    dfReportSupp *supp = &args->supp;

    // Before disposing task information all pending tasks should be dropped from the queue    
    if (threadPoolRemoveTasks(FRAMEWORK_META(args)->pool, supp->tasks, supp->taskscnt))
        logMsg(FRAMEWORK_META(args)->log, "WARNING (%s): Pending tasks were dropped!\n", __FUNCTION__);

    free(supp->tasks);
    return 1;
}

static bool dfReportCloseCondition(dfReportSupp *supp, void *arg)
{
    (void) arg;
           
    switch (bitGet2((void *) supp->ldstat, LOADDATASUPP_STAT_BIT_POS_TASK_COMP))
    {
    case 0: return 0;
    case 1: return 1;
    case 3: break;
    }
    
    switch (bitGet2((void *) supp->dfstat, DENSITYFOLDSUPP_STAT_BIT_POS_TASK_COMP))
    {
    case 0: return 0;
    case 1: return 1;
    case 3: break;
    }
    
    return bitTest((void *) supp->stat, DFREPORTSUPP_STAT_BIT_POS_TASK_COMP);
}

bool dfReportEpilogue(dfReportIn *in, dfReportOut *out, void *context)
{
    if (!out) return 0;
    (void) context;
    
    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();
    
    *tsk = (task)
    {
        .callback = (taskCallback) dfReportThreadClose,
        .cond = (conditionCallback) dfReportCloseCondition,
        .asucc = (aggregatorCallback) sizeDecInterlocked,
        .afail = (aggregatorCallback) sizeDecInterlocked,
        .arg = out,
        .condmem = &out->supp,
        .asuccmem = DENSITYFOLD_META(in)->hold,
        .afailmem = DENSITYFOLD_META(in)->hold
    };

    return 1;

ERR():
    logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);
    return 0;
}

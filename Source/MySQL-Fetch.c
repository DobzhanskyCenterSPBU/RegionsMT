#include "Common.h"
#include "Debug.h"
#include "MySQL-Fetch.h"
#include "StringTools.h"
#include "TaskMacros.h"
#include "x86_64/Tools.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <mysql.h>

struct mysqlFetchThreadProcArgs
{
    size_t chr;
};

struct mysqlFetchThreadProcContext
{
    mysqlFetchOut *out;
    mysqlFetchContext *context;
};

static bool mysqlFetchThreadProc(mysqlFetchThreadProcArgs *args, mysqlFetchThreadProcContext *context)
{
    const char *strings[] =
    {
        __FUNCTION__,
        "ERROR (%s): Unable to read chromosome no. %zu. %s!\n",
        "ERROR (%s): Database corruption occurred while reading chromosome no. %zu!\n",
        "Unable to establish MySQL connection",
    };

    enum { STR_FN, STR_FR_RC, STR_FR_CC, STR_M_SQL };
    
    const char form[] =
        "select `nrow`, `pos`, `alias`, `maf`, `gene` from `rows` where `chr` = \"%zu\";"
        "select `nrow`, `col`, coalesce(`pval`, 0), coalesce(`ratio`, 0) from `ranks` where `chr` = \"%zu\";";
    
    enum { COL0_ROW = 0, COL0_POS, COL0_ALIAS, COL0_TMAF, COL0_GENE };
    enum { COL1_ROW = 0, COL1_COL, COL1_PV, COL1_QAS };

    bool succ = 0;
    enum { ERR_SQL = 0, ERR_VAL, SUCCESS } errm = SUCCESS;

    char tempbuff[TEMP_BUFF] = { '\0' };
    
    mysqlFetchRes *restrict res = &context->out->res;

    const size_t chr = args->chr;
    snprintf(tempbuff, sizeof tempbuff, form, chr + 1, chr + 1);
   
    MYSQL *mysqlcon = mysql_init(NULL);
    if (!mysqlcon) goto ERR();

    if (mysql_real_connect(mysqlcon, context->context->host, context->context->login, context->context->password, context->context->schema, context->context->port, NULL, CLIENT_MULTI_STATEMENTS))
    {
        if (!mysql_query(mysqlcon, tempbuff)) for (;;)
        {
            size_t possnp = 0, posgene = 0, st = res->chroff[chr], basesnp = res->snpname[st], basegene = res->genename[st];
            MYSQL_RES *mysqlres = mysql_use_result(mysqlcon);

            if (mysqlres)
            {
                size_t i = 0;

                for (;; i++)
                {
                    char *test;
                    size_t len;
                    MYSQL_ROW mysqlrow = mysql_fetch_row(mysqlres);
                    if (!mysqlrow) break;
                                        
                    ///////////////////////////////////////////////////////

                    size_t ind = strtosz(mysqlrow[COL0_ROW], &test, 10) - 1;
                    if (*test || ind >= res->chrlen[chr]) { errm = ERR_VAL; break; }

                    size_t off = st + ind;

                    ///////////////////////////////////////////////////////

                    res->pos[off] = strtouint32(mysqlrow[COL0_POS], &test, 10);
                    if (*test) { errm = ERR_VAL; break; }

                    ///////////////////////////////////////////////////////

                    res->snpname[off] = possnp + basesnp;
                    len = strlen(mysqlrow[COL0_ALIAS]);

                    if (len)
                    {
                        if (possnp + basesnp + len >= res->snpnamestrsz) { errm = ERR_VAL; break; }
                        strcpy(res->snpnamestr + basesnp + possnp, mysqlrow[COL0_ALIAS]);
                        possnp += len + 1;
                    }
                    else res->snpname[off] = res->snpnamestrsz - 1;

                    ///////////////////////////////////////////////////////

                    res->tmaf[off] = strtod(mysqlrow[COL0_TMAF], &test);
                    if (*test) { errm = ERR_VAL; break; }

                    ///////////////////////////////////////////////////////

                    res->genename[off] = posgene + basegene;
                    len = strlen(mysqlrow[COL0_GENE]);

                    if (len)
                    {
                        if (posgene + basegene + len >= res->genenamestrsz) { errm = ERR_VAL; break; }
                        strcpy(res->genenamestr + basegene + posgene, mysqlrow[COL0_GENE]);
                        posgene += len + 1;
                    }
                    else res->genename[off] = res->genenamestrsz - 1;
                }
                
                if (i != res->chrlen[chr]) errm = ERR_VAL;

                mysql_free_result(mysqlres);
            }
            else errm = ERR_SQL;
            
            if (errm < SUCCESS || mysql_next_result(mysqlcon)) { errm = ERR_VAL; break; }
            
            mysqlres = mysql_use_result(mysqlcon);

            if (mysqlres)
            {
                size_t i = 0;
                
                for (;; i++)
                {
                    char *test;
                    double ftest;
                    MYSQL_ROW mysqlrow = mysql_fetch_row(mysqlres);
                    if (!mysqlrow) break;

                    ///////////////////////////////////////////////////////

                    size_t row = strtosz(mysqlrow[COL1_ROW], &test, 10) - 1;
                    if (*test || row >= res->chrlen[chr]) { errm = ERR_VAL; break; }

                    size_t col = strtosz(mysqlrow[COL1_COL], &test, 10) - 1;
                    if (*test || col >= res->testcnt) { errm = ERR_VAL; break; }

                    size_t ind = row + res->chroff[chr] + col * res->snpcnt;

                    ///////////////////////////////////////////////////////

                    ftest = strtod(mysqlrow[COL1_PV], &test);
                    if (*test) { errm = ERR_VAL; break; }

                    if (ftest >= 0 && ftest <= 1) res->nlpv[ind] = -log10(ftest);

                    ///////////////////////////////////////////////////////

                    ftest = strtod(mysqlrow[COL1_QAS], &test);
                    if (*test) { errm = ERR_VAL; break; }

                    if (ftest >= 0) res->qas[ind] = log10(ftest);
                }

                if (i != res->testcnt * res->chrlen[chr]) errm = ERR_VAL;

                mysql_free_result(mysqlres);
            }
            else errm = ERR_SQL;

            break;
        }
        else errm = ERR_SQL;
    }
    else errm = ERR_SQL;
           
    switch(errm)
    {
    ERR():
    case ERR_SQL:
        if (mysqlcon) logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_RC], strings[STR_FN], chr + 1, mysql_error(mysqlcon));
        else logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_RC], strings[STR_FN], chr + 1, strings[STR_M_SQL]);
        break;

    case ERR_VAL:
        logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_CC], strings[STR_FN], chr + 1);
        break;
    
    default:
        succ = 1;
    }
    
    mysql_close(mysqlcon);
    mysql_thread_end();
        
    return succ;
}

///////////////////////////////////////////////////////////////////////////////

static bool  mysqlFetchTransitionCondition(mysqlFetchSupp *supp, void *arg)
{
    (void) arg;
    return supp->succ + supp->fail == supp->thresh;
}

static bool mysqlFetchTransition(mysqlFetchSupp *args, void *context)
{
    (void) context;    
    return !args->fail;
}

static bool mysqlFetchThreadEpilogue(void *args, void *context)
{
    (void) args;
    (void) context;
    
    return 1;
}

///////////////////////////////////////////////////////////////////////////////

static bool mysqlFetchThreadPrologue(mysqlFetchOut *args, mysqlFetchContext *context)
{
    const char query[] =
        "select max(cast(`chr` as decimal)) from `rows`;"
        "select max(`col`), sum(length(`test`) + sign(length(`test`))) from `cols`;"
        "select `chr`, max(`nrow`), sum(length(`gene`) + sign(length(`gene`))), sum(length(`alias`) + sign(length(`alias`))) from `rows` where `chr` in(select `chr` from `rows`) group by `chr`;"
        "select `col`, `test` from `cols`;";
    
    bool succ = 0;
    enum { ERR_SQL = 0, ERR_VAL, ERR_MEM, SUCCESS } errm = SUCCESS;
    enum step { STP_CHR = 0, STP_COL, STP_LEN, STP_STR, STP_END };
    
    const char *msgptr;

    mysqlFetchRes *restrict res = &args->res;
    MYSQL *mysqlcon = mysql_init(NULL);
    if (!mysqlcon) goto ERR(MySQL);

    size_t *tempsnp = NULL, *tempgene = NULL;

    if (mysql_real_connect(mysqlcon, context->host, context->login, context->password, context->schema, context->port, NULL, CLIENT_MULTI_STATEMENTS))
    {
        if (!mysql_query(mysqlcon, query))
        {
            for (enum step stp = STP_CHR;;)
            {
                MYSQL_RES *mysqlres = mysql_use_result(mysqlcon);

                if (mysqlres)
                {
                    char *test;
                    size_t num, pos;
                    
                    MYSQL_ROW mysqlrow = mysql_fetch_row(mysqlres);
                    
                    if (mysqlrow)
                    {
                        switch (stp)
                        {
                        case STP_CHR:
                            res->chrcnt = strtosz(mysqlrow[0], &test, 10);
                            if (*test) { errm = ERR_VAL; break; }
                            
                            if (!(
                                arrayInitClear((void **) &res->chrlen, res->chrcnt, sizeof *res->chrlen) &&
                                arrayInitClear((void **) &res->chroff, res->chrcnt, sizeof *res->chroff) &&
                                arrayInitClear((void **) &res->chrname, res->chrcnt, sizeof *res->chrname))) { errm = ERR_MEM; break; }
                            
                            break;

                        case STP_COL:
                            res->testcnt = strtosz(mysqlrow[0], &test, 10);
                            if (*test) { errm = ERR_VAL; break; }

                            res->testnamestrsz = max(1, strtosz(mysqlrow[1], &test, 10));
                            if (*test) { errm = ERR_VAL; break; }
                                                        
                            if (!(
                                arrayInitClear((void **) &res->testname, res->testcnt, sizeof *res->testname) &&
                                arrayInitClear((void **) &res->testnamestr, res->testnamestrsz, sizeof *res->testnamestr))) { errm = ERR_MEM; break; }
                                                        
                            break;

                        case STP_LEN:
                            if (!(
                                arrayInit((void **) &tempsnp, res->chrcnt, sizeof *tempsnp) &&
                                arrayInit((void **) &tempgene, res->chrcnt, sizeof *tempgene))) { errm = ERR_MEM; break; }

                            num = res->chrcnt;
                            
                            do {
                                size_t ind = strtosz(mysqlrow[0], &test, 10) - 1;
                                if (*test || ind >= res->chrcnt) { errm = ERR_VAL; break; }
                                                                
                                res->snpcnt += res->chrlen[ind] = res->chroff[ind] = strtosz(mysqlrow[1], &test, 10);
                                if (*test) { errm = ERR_VAL; break; }
                                                                
                                res->genenamestrsz += tempgene[ind] = strtosz(mysqlrow[2], &test, 10);
                                if (*test) { errm = ERR_VAL; break; }

                                res->snpnamestrsz += tempsnp[ind] = strtosz(mysqlrow[3], &test, 10);
                                if (*test) { errm = ERR_VAL; break; }
                                
                                mysqlrow = mysql_fetch_row(mysqlres);
                                num--;
                            } while (num && mysqlrow);

                            if (num || mysqlrow) { errm = ERR_VAL; break; }
                                                        
                            if (res->snpcnt)
                            {
                                if (!(
                                    arrayInitClear((void **) &res->genename, res->snpcnt, sizeof *res->genename) &&
                                    arrayInitClear((void **) &res->snpname, res->snpcnt, sizeof *res->snpname) &&
                                    arrayInitClear((void **) &res->pos, res->snpcnt, sizeof *res->pos) // &&
                                    //arrayInitClear((void **) &res->allelename, res->snpcnt, sizeof *res->allelename) &&
                                    //arrayInitClear((void **) &res->tmaf, res->snpcnt, sizeof *res->tmaf)
                                    )) { errm = ERR_MEM; break; }
                                
                                //MEMORY_SET(res->tmaf, res->snpcnt); // All floats are set to some quite NaN
                            }

                            // Convert lengths to offsets
                            for (size_t i = 0, sum = 0, sumsnp = 0, sumgene = 0; i < res->chrcnt; i++)
                            {
                                res->chroff[i] = sum;
                                res->snpname[sum] = sumsnp;
                                res->genename[sum] = sumgene;

                                sum += res->chrlen[i];
                                sumsnp += tempsnp[i];
                                sumgene += tempgene[i];
                            }

                            if (!res->genenamestrsz) res->genenamestrsz = 1;
                            if (!res->snpnamestrsz) res->snpnamestrsz = 1;

                            //res->allelenamestrsz = 1;
                            res->chrnamestrsz = 1;
                                
                            if (!(
                                arrayInitClear((void **) &res->genenamestr, res->genenamestrsz, sizeof *res->genenamestr) &&
                                arrayInitClear((void **) &res->snpnamestr, res->snpnamestrsz, sizeof *res->snpnamestr) &&
                                //arrayInitClear((void **) &res->allelenamestr, res->allelenamestrsz, sizeof *res->allelenamestr) &&
                                arrayInitClear((void **) &res->chrnamestr, res->chrnamestrsz, sizeof *res->chrnamestr))) { errm = ERR_MEM; break; }
                            
                            res->pvcnt = res->snpcnt * res->testcnt;

                            if (!(
                                arrayInit((void **) &res->qas, res->pvcnt, sizeof *res->qas) &&
                                arrayInit((void **) &res->nlpv, res->pvcnt, sizeof *res->nlpv) &&
                                arrayInitClear((void **) &res->rqas, res->pvcnt, sizeof *res->rqas) &&
                                arrayInitClear((void **) &res->rnlpv, res->pvcnt, sizeof *res->rnlpv))) { errm = ERR_MEM; break; }
                                
                            MEMORY_SET(res->qas, res->pvcnt);
                            MEMORY_SET(res->nlpv, res->pvcnt);
                                
                            // Other fields are initialized elsewhere...                            
                            break;

                        case STP_STR:
                            num = res->testcnt;
                            pos = 0;

                            do {
                                size_t ind = strtosz(mysqlrow[0], &test, 10) - 1;
                                if (*test || ind >= res->testcnt) { errm = ERR_VAL; break; }

                                res->testname[ind] = pos;
                                size_t len = strlen(mysqlrow[1]);

                                if (len)
                                {
                                    strcpy(res->testnamestr + pos, mysqlrow[1]);
                                    if (pos + len >= res->testnamestrsz) { errm = ERR_VAL; break; }
                                    pos += len + 1;
                                }
                                else res->testname[ind] = res->testnamestrsz - 1;

                                mysqlrow = mysql_fetch_row(mysqlres);
                                num--;
                            } while (num && mysqlrow);

                            if (pos != res->testnamestrsz || num || mysqlrow) errm = ERR_VAL;
                            break;

                        default:
                            errm = ERR_VAL;
                            break;
                        }
                    }
                    else errm = ERR_SQL;

                    mysql_free_result(mysqlres);
                    if (errm < SUCCESS) break;
                }
                else errm = ERR_SQL;

                stp++;

                if (stp == STP_END) break;
                if (mysql_next_result(mysqlcon) > 0) { errm = ERR_SQL; break; }
            }
        }
        else errm = ERR_SQL;
    }
    else errm = ERR_SQL;

    free(tempsnp);
    free(tempgene);
 
    switch (errm)
    {
    case ERR_MEM:
        goto ERR();
    
    case ERR_SQL:
        goto ERR(MySQL);

    case ERR_VAL:
        goto ERR(Layout);

    default:
        break;
    }

    mysqlFetchSupp *restrict supp = &args->supp;
    
    if (res->chrcnt)
    {
        supp->args = malloc(res->chrcnt * sizeof *supp->args);
        if (!supp->args) goto ERR();
    }
    
    bool ranks = !bitTest(context->bits, MYSQLFETCHCONTEXT_BIT_POS_NORANKS);

    // Tasks: chromosome fetching tasks, one transition task, four sorting tasks, and one epilogue task 
    supp->tasks = malloc((res->chrcnt + (ranks ? MYSQLFETCHSUPP_TASKS_RANK_CNT : 0) + 2) * sizeof (task));
    if (!supp->tasks) goto ERR();

    supp->context = malloc(sizeof *supp->context);
    if (!supp->context) goto ERR();

    *supp->context = (mysqlFetchThreadProcContext) { .out = args, .context = context };

    ///////////////////////////////////////////////////////////////////////////
    
    // Chromosome fetching tasks
    
    for (size_t i = 0; i < res->chrcnt; i++)
    {
        supp->args[i] = (mysqlFetchThreadProcArgs) { .chr = i };

        supp->tasks[supp->taskscnt++] = (task)
        {
            .callback = (taskCallback) mysqlFetchThreadProc,
            .asucc = (aggregatorCallback) sizeIncInterlocked,
            .afail = (aggregatorCallback) sizeIncInterlocked,
            .arg = &supp->args[i],
            .context = supp->context,            
            .asuccmem = &supp->succ,
            .afailmem = &supp->fail,
        };
    }
    
    supp->thresh = supp->taskscnt;

    // Sorting tasks
    
    if (ranks)
    {
        loadDataRanksSchedule(&supp->rargs[MYSQLFETCHSUPP_TASKS_RANKLPV_PRO >> 1], MYSQLFETCH_META(args), MYSQLFETCHSUPP_STAT_BIT_POS_LOAD_SUCC, supp->tasks, offsetof(mysqlFetchRes, nlpv), offsetof(mysqlFetchRes, rnlpv), MYSQLFETCHSUPP_STAT_BIT_POS_SORTLPV_COMP, MYSQLFETCHSUPP_STAT_BIT_POS_RANKLPV_COMP, &supp->taskscnt);
        loadDataRanksSchedule(&supp->rargs[MYSQLFETCHSUPP_TASKS_RANKQAS_PRO >> 1], MYSQLFETCH_META(args), MYSQLFETCHSUPP_STAT_BIT_POS_LOAD_SUCC, supp->tasks, offsetof(mysqlFetchRes, qas), offsetof(mysqlFetchRes, rqas), MYSQLFETCHSUPP_STAT_BIT_POS_SORTQAS_COMP, MYSQLFETCHSUPP_STAT_BIT_POS_RANKQAS_COMP, &supp->taskscnt);
    }
    else
    {
        bitSet2Interlocked(supp->stat, MYSQLFETCHSUPP_STAT_BIT_POS_SORTLPV_COMP);
        bitSet2Interlocked(supp->stat, MYSQLFETCHSUPP_STAT_BIT_POS_RANKLPV_COMP);
        bitSet2Interlocked(supp->stat, MYSQLFETCHSUPP_STAT_BIT_POS_SORTQAS_COMP);
        bitSet2Interlocked(supp->stat, MYSQLFETCHSUPP_STAT_BIT_POS_RANKQAS_COMP);
    }
    
    // Transition task (tasks routine as in epilogue)    
    supp->tasks[supp->taskscnt++] = TASK_BIT_1_INIT(mysqlFetchTransition, mysqlFetchTransitionCondition, supp, NULL, supp, supp->stat, pnumGet(FRAMEWORK_META(args)->pnum, MYSQLFETCHSUPP_STAT_BIT_POS_LOAD_COMP));
    
    // Epilogue
    supp->tasks[supp->taskscnt++] = TASK_BIT_2_INIT(mysqlFetchThreadEpilogue, bitTestRangeMem, args, NULL, supp->stat, supp->stat, pnumGet(FRAMEWORK_META(args)->pnum, MYSQLFETCHSUPP_STAT_BIT_POS_TASK_COMP), pnumGet(FRAMEWORK_META(args)->pnum, MYSQLFETCHSUPP_STAT_BIT_POS_TASK_COMP));
        
    if (!threadPoolEnqueueTasks(FRAMEWORK_META(args)->pool, supp->tasks, supp->taskscnt, 1)) goto ERR(Pool);
    
    for (;;)
    {
        succ = 1;
        break;

    ERR() :
        logError(FRAMEWORK_META(args)->log, __FUNCTION__, errno);
        break;
        
    ERR(MySQL):
        for (;;)
        {        
            if (mysqlcon) msgptr = mysql_error(mysqlcon);
            else msgptr = "Unable to establish MySQL connection";
            break;

        ERR(Layout):
            msgptr = "Invalid database layout";
            break;

        ERR(Pool):
            msgptr = "Unable to enqueue tasks";
            break;
        }

        logMsg(FRAMEWORK_META(args)->log, "ERROR (%s): %s!\n", __FUNCTION__, msgptr);
        break;   
    }    

    mysql_close(mysqlcon);
    mysql_thread_end();

    return succ;
}

void mysqlFetchContextDispose(mysqlFetchContext *context)
{
    if (!context) return;

    free(context->schema);
    free(context->host);
    free(context->login);
    free(context->password);
    free(context);
}

bool mysqlFetchPrologue(mysqlFetchIn *in, mysqlFetchOut **pout, mysqlFetchContext *context)
{
    mysqlFetchOut *out = *pout = malloc(sizeof *out);
    if (!out) goto ERR();
    
    *out = (mysqlFetchOut) { .meta = MYSQLFETCH_META_INIT(in, out, context) };
    if (!outInfoSetNext(FRAMEWORK_META(in)->out, out)) goto ERR();

    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();
    
    if (!pnumTest(FRAMEWORK_META(out)->pnum, MYSQLFETCHSUPP_STAT_BIT_CNT)) goto ERR();
    *tsk = TASK_BIT_1_INIT(mysqlFetchThreadPrologue, NULL, out, context, NULL, out->supp.stat, pnumGet(FRAMEWORK_META(out)->pnum, MYSQLFETCHSUPP_STAT_BIT_POS_INIT_COMP));
    return 1;

ERR():
    logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);
    return 0;
}

static bool mysqlFetchThreadClose(mysqlFetchOut *args, void *context)
{
    (void) context;
    if (!args) return 1;

    testDataClose(&args->res);

    mysqlFetchSupp *supp = &args->supp;
    
    // Before disposing task information all pending tasks should be dropped from the queue    
    if (threadPoolRemoveTasks(FRAMEWORK_META(args)->pool,  supp->tasks, supp->taskscnt))
        logMsg(FRAMEWORK_META(args)->log, "WARNING (%s): Pending tasks were dropped!\n", __FUNCTION__);
    
    free(supp->args);
    free(supp->context);
    free(supp->tasks);

    return 1;
}

static bool mysqlFetchThreadCloseCondition(mysqlFetchSupp *supp, void *arg)
{
    (void) arg;    
    
    switch (bitGet2((void *) supp->stat, MYSQLFETCHSUPP_STAT_BIT_POS_INIT_COMP))
    {
    case 0: return 0;
    case 1: return 1;
    case 3: break;
    }
    
    switch (bitGet2((void *) supp->stat, MYSQLFETCHSUPP_STAT_BIT_POS_LOAD_COMP))
    {
    case 0: return 0;
    case 1: return 1;
    case 3: break;
    }    
    
    if (supp->holdload) return 0;

    size_t ind = 0;

    switch (bitGet2((void *) supp->stat, MYSQLFETCHSUPP_STAT_BIT_POS_SORTLPV_COMP))
    {
    case 0: return 0;
    case 1: ind++;
    case 3: break;
    }

    switch (bitGet2((void *) supp->stat, MYSQLFETCHSUPP_STAT_BIT_POS_SORTQAS_COMP))
    {
    case 0: return 0;
    case 1: ind++;
    case 3: break;
    }

    if (ind) return 1;

    ind = 0;

    switch (bitGet2((void *) supp->stat, MYSQLFETCHSUPP_STAT_BIT_POS_RANKLPV_COMP))
    {
    case 0: return 0;
    case 1: ind++;
    case 3: break;
    }

    switch (bitGet2((void *) supp->stat, MYSQLFETCHSUPP_STAT_BIT_POS_RANKQAS_COMP))
    {
    case 0: return 0;
    case 1: ind++;
    case 3: break;
    }

    if (ind) return 1;

    switch (bitGet2((void *) supp->stat, MYSQLFETCHSUPP_STAT_BIT_POS_TASK_COMP))
    {
    case 0: return 0;
    case 1: return 1; // Never happens
    case 3: break;
    }

    return !(supp->hold || supp->holdload);
}

bool mysqlFetchEpilogue(mysqlFetchIn *in, mysqlFetchOut* out, void *context)
{
    if (!out) return 0;    
    (void) context;
    
    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();
        
    *tsk = (task)
    {
        .arg = out,
        .callback = (taskCallback) mysqlFetchThreadClose,
        .cond = (conditionCallback) mysqlFetchThreadCloseCondition,
        .condmem = &out->supp
    };

    return 1;
    
ERR():
    logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);
    return 0;
}

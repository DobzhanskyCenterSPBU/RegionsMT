#include "Common.h"
#include "Debug.h"
#include "MySQL-Dispatch.h"
#include "TaskMacros.h"
#include "Threading.h"
#include "x86_64/Tools.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>

#include <mysql.h>

// ToDo: Handle properly the quotes

static bool writeProcInd(FILE *f, loadDataRes *res, size_t offset, size_t length)
{
    for (size_t i = offset, chr = findBound(i, res->chroff, res->chrcnt), row = i - res->chroff[chr]; i < offset + length;)
    {
        if (fprintf(f, "%" PRIu16 "," "%" PRIu32 "," "%" PRIu32 "\n", (uint16_t) (chr + 1), (uint32_t) (row + 1), (uint32_t) (i + 1)) < 0) return 0;
        if (++i == res->chroff[chr] + res->chrlen[chr]) chr++, row = 0;
        else row++;
    }
    
    return 1;
}

static bool writeProcVind(FILE *f, loadDataRes *res, size_t offset, size_t length)
{
    for (size_t i = offset, test = i / res->snpcnt, ind = i % res->snpcnt, base = res->snpcnt * test; i < offset + length;)
    {
        if (fprintf(f, "%" PRIu32 "," "%" PRIu32 "," "%zu\n", (uint32_t) (ind + 1), (uint32_t) (test + 1), ind * res->testcnt + test + 1) < 0) return 0;
        if (++i == base + res->snpcnt) test++, ind = 0, base += res->snpcnt;
        else ind++;
    }
    
    return 1;
}

#define DEFINE_WRITEPROC(POSTFIX, FORMAT, ...) \
    static bool (writeProc ## POSTFIX)(FILE *f, loadDataRes *res, size_t offset, size_t length) \
    { \
        for (size_t _index = offset; _index < offset + length; _index++) \
            if (fprintf(f, "%zu," FORMAT "\n", _index + 1, __VA_ARGS__) < 0) return 0; \
        \
        return 1; \
    }

#define DEFINE_WRITEPROC_V(POSTFIX, FORMAT, ...) \
    static bool (writeProc ## POSTFIX)(FILE *f, loadDataRes *res, size_t offset, size_t length) \
    { \
        for (size_t _index = offset, test = _index / res->snpcnt, ind = _index % res->snpcnt, base = res->snpcnt * test; _index < offset + length;) \
        { \
            if (fprintf(f, "%zu," FORMAT "\n", ind * res->testcnt + test + 1, __VA_ARGS__) < 0) return 0; \
            if (++_index == base + res->snpcnt) test++, ind = 0, base += res->snpcnt; \
            else ind++; \
        } \
        \
        return 1; \
    }

DEFINE_WRITEPROC(Chr, "\"%.255s\"", res->chrnamestr + res->chrname[_index])
DEFINE_WRITEPROC(ChrSupp, "%zu, %zu", res->chroff[_index], res->chrlen[_index])
DEFINE_WRITEPROC(Test, "\"%.255s\"", res->testnamestr + res->testname[_index])

DEFINE_WRITEPROC(Pos, "%zu", res->pos[_index])
DEFINE_WRITEPROC(Alias, "\"%.31s\"", res->snpnamestr + res->snpname[_index])
//DEFINE_WRITEPROC(Allele, "\"%.31s\"", res->allelenamestr + res->allelename[_index])

DEFINE_WRITEPROC_V(Lpv, "%.16e", isnan((res->nlpv)[_index]) ? -1. : pow(10, -res->nlpv[_index]))
DEFINE_WRITEPROC_V(Qas, "%.16e", res->qas[_index])
//DEFINE_WRITEPROC_V(Maf, "%.16e", isnan((res->maf)[_index]) ? -1. : res->maf[_index])
DEFINE_WRITEPROC_V(Rlpv, "%" PRIuPTR, res->rnlpv[_index] + 1)
DEFINE_WRITEPROC_V(Rqas, "%" PRIuPTR, res->rqas[_index] + 1)

#undef DEFINE_WRITEPROC

// Data which should be stored globally
static const mysqlDispatchThreadInfo schemaInfo[] =
{
    { .table = "`chr`", .primkey = "`chr`", .key = "`chrname`", .writeProc = writeProcChr },
    { .table = "`chrsupp`", .primkey = "`chr`", .writeProc = writeProcChrSupp },
    { .table = "`col`", .primkey = "`col`", .writeProc = writeProcTest },
    { .table = "`ind`", .primkey = "`chr`,`nrow`", .key = "`ind`", .writeProc = writeProcInd },
    { .table = "`pos`", .primkey = "`ind`", .key = "`pos`", .writeProc = writeProcPos },
    { .table = "`alias`", .primkey = "`ind`", .key = "`alias`", .writeProc = writeProcAlias },
    //{ .table = "`allele`", .primkey = "`ind`", .key = "`allele`", .writeProc = writeProcAllele },
    { .table = "`v_ind`", .primkey = "`ind`,`col`", .key = "`v_ind`", .writeProc = writeProcVind },
    { .table = "`pval`", .primkey = "`v_ind`", .key = "`pval`", .writeProc = writeProcLpv },
    { .table = "`ratio`", .primkey = "`v_ind`", .key = "`ratio`", .writeProc = writeProcQas },
    //{ .table = "`maf`", .primkey = "`v_ind`", .key = "`maf`", .writeProc = writeProcMaf },
    { .table = "`r_pval`", .primkey = "`v_ind`", .key = "`r_pval`", .writeProc = writeProcRlpv },
    { .table = "`r_ratio`", .primkey = "`v_ind`", .key = "`r_ratio`", .writeProc = writeProcRqas }
};

static bool mysqlInitSchema(mysqlDispatchOut *args, mysqlDispatchContext *context)
{
    const char *strings[] =
    {
        __FUNCTION__,
        "ERROR (%s): %s!\n",
        "Unable to establish MySQL connection",
        "Invalid schema name provided",
    };

    enum { STR_FN, STR_FR_EG, STR_M_SQL, STR_M_ISN };

    bool succ = 0;
    char tempbuff[2 * TEMP_BUFF] = { '\0' };

    const char *queries[] =
    {
        "set FOREIGN_KEY_CHECKS = 0;",
        "drop schema if exists `%s`;",
        "create schema `%s` default character set utf8;",
        "use `%s`;",

        "create table `chr` (`chr` int(2) unsigned not null, `chrname` varchar(255) not null) engine = innodb",
        "create table `chrsupp` (`chr` int(2) unsigned not null, `chroff` int(4) unsigned  not null, `chrlen` int(4) unsigned not null) engine = innodb",
        "create table `col` (`col` int(4) unsigned not null, `test` text not null) engine = innodb",

        "create table `ind` (`chr` int(2) unsigned not null, `nrow` int(4) unsigned not null, `ind` int(4) unsigned not null) engine = innodb",
        "create table `pos` (`ind` int(4) unsigned not null, `pos` int(4) unsigned not null) engine = innodb",
        "create table `alias` (`ind` int(4) unsigned not null, `alias` varchar(31) not null) engine = innodb",
        //"create table `allele` (`ind` int(4) unsigned not null, `allele` varchar(7) not null) engine = innodb",
        
        "create table `v_ind` (`ind` int(8) unsigned not null, `col` int(4) unsigned not null, `v_ind` int(8) unsigned not null) engine = innodb",
        "create table `pval` (`v_ind` int(8) unsigned not null, `pval` double not null) engine = innodb",
        "create table `ratio` (`v_ind` int(8) unsigned not null, `ratio` double not null) engine = innodb",
        //"create table `maf` (`v_ind` int(8) unsigned not null, `maf` double not null) engine = innodb",
        "create table `r_pval` (`v_ind` int(8) unsigned not null, `r_pval` int(8) unsigned not null) engine = innodb",
        "create table `r_ratio` (`v_ind` int(8) unsigned not null, `r_ratio` int(8) unsigned not null) engine = innodb"
    };

    uint8_t queryPreproc[BYTE_CNT(countof(queries))] = { 0b00001110, 0 }; // Queries that require preprocessing

    MYSQL *mysqlcon = mysql_init(NULL);
    if (!mysqlcon) goto ERR(MySQL);

    if (mysql_real_connect(mysqlcon, context->host, context->login, context->password, NULL, context->port, NULL, CLIENT_MULTI_STATEMENTS))
    {
        for (size_t i = 0; i < countof(queries); i++)
        {
            if (bitTest(queryPreproc, i))
            {
                int ilen = snprintf(tempbuff, sizeof tempbuff, queries[i], context->schema);
                if (ilen < 0 || ilen == sizeof tempbuff) goto ERR(Schema);
                if (mysql_query(mysqlcon, tempbuff)) goto ERR(MySQL);
            }
            else if (mysql_query(mysqlcon, queries[i])) goto ERR(MySQL);
        }
    }
    else goto ERR(MySQL);

    for (;;)
    {
        succ = 1;
        break;

    ERR(Schema):
        logMsg(FRAMEWORK_META(args)->log, strings[STR_FR_EG], strings[STR_FN], strings[STR_M_ISN]);
        break;

    ERR(MySQL) :
        if (mysqlcon) logMsg(FRAMEWORK_META(args)->log, strings[STR_FR_EG], strings[STR_FN], mysql_error(mysqlcon));
        else logMsg(FRAMEWORK_META(args)->log, strings[STR_FR_EG], strings[STR_FN], strings[STR_M_SQL]);
        break;
    }

    mysql_close(mysqlcon);
    mysql_thread_end();

    return succ;
}

static bool  mysqlDispatchTransitionCondition(taskSupp *supp, void *arg)
{
    (void) arg;
    return supp->succ + supp->fail == supp->taskscnt;
}

static bool  mysqlDispatchTransition(taskSupp *args, void *context)
{
    (void) context;

    return !args->fail;
}

typedef struct
{
    const mysqlDispatchThreadInfo *inf;
    size_t offset, length;
} mysqlDispatchThreadArgs;

static bool mysqlDispatchThreadProc(mysqlDispatchThreadArgs *args, mysqlDispatchThreadContext *context)
{
    const char *strings[] =
    {
        __FUNCTION__,
        "Loading rows %zu:%zu into the MySQL table \"%s.%s\"",
        "Creating primary key in the MySQL table \"%s.%s\"",
        "Creating key in the MySQL table \"%s.%s\"",
        "ERROR (%s): %s!\n",
        "ERROR (%s): Cannot open temporary file \"%s\". %s!\n",
        "Unable to establish MySQL connection",
        "Invalid temporary file path provided",
        "Invalid table name provided",
        "Failed to write to temporary file",
    };

    enum { STR_FN, STR_FR_GL, STR_FR_GP, STR_FR_GK, STR_FR_EG, STR_FR_EI, STR_M_SQL, STR_M_IFN, STR_M_ITN, STR_M_WRF };
    
    const char *queries[] =
    {
        "load data concurrent local infile '%s' replace into table %s fields terminated by ',' optionally enclosed by '\"' lines terminated by '\\n';" ,
        "alter table %s add primary key(%s);",
        "alter table %s add key(%s);",
    };
    
    bool succ = 0;
    char tempbuff[TEMP_BUFF] = { '\0' }, filebuff[TEMP_BUFF_LARGE] = { '\0' }, querybuff[TEMP_BUFF_LARGE + TEMP_BUFF_LARGE] = { '\0' };
    FILE *f = NULL;

    if (getTempFileName(FRAMEWORK_META(context->out)->pool, filebuff, sizeof filebuff, context->context->temppr) == sizeof filebuff) goto ERR(FileName);
    
    f = fopen(filebuff, "wb");
    if (!f) goto ERR(File);
        
    if (!args->inf->writeProc(f, LOADDATA_META(context->out)->res, args->offset, args->length)) goto ERR(Write);
    fclose(f);
    f = NULL;
    
    MYSQL *mysqlcon = mysql_init(NULL);
    if (!mysqlcon) goto ERR(MySQL);
    
    mysql_options(mysqlcon, MYSQL_OPT_LOCAL_INFILE, &(unsigned) { 1 });
    
    if (mysql_real_connect(mysqlcon, context->context->host, context->context->login, context->context->password, context->context->schema, context->context->port, NULL, CLIENT_MULTI_STATEMENTS | CLIENT_LOCAL_FILES))
    {
        uint64_t initime = getTime();
        
        int ilen = snprintf(querybuff, sizeof querybuff, queries[0], filebuff, args->inf->table);
        if (ilen < 0 || ilen == sizeof querybuff) goto ERR(Table);
        if (mysql_query(mysqlcon, querybuff)) goto ERR(MySQL);

        snprintf(querybuff, sizeof querybuff, strings[STR_FR_GL], args->offset + 1, args->offset + args->length, context->context->schema, args->inf->table);
        logTime(FRAMEWORK_META(context->out)->log, initime, strings[STR_FN], querybuff);

        if (args->inf->primkey)
        {
            initime = getTime();

            ilen = snprintf(querybuff, sizeof querybuff, queries[1], args->inf->table, args->inf->primkey);
            if (ilen < 0 || ilen == sizeof querybuff) goto ERR(Table);
            if (mysql_query(mysqlcon, querybuff)) goto ERR(MySQL);

            snprintf(querybuff, sizeof querybuff, strings[STR_FR_GP], context->context->schema, args->inf->table);
            logTime(FRAMEWORK_META(context->out)->log, initime, strings[STR_FN], querybuff);
        }
        
        if (args->inf->key)
        {
            initime = getTime();

            ilen = snprintf(querybuff, sizeof querybuff, queries[2], args->inf->table, args->inf->key);
            if (ilen < 0 || ilen == sizeof querybuff) goto ERR(Table);
            if (mysql_query(mysqlcon, querybuff)) goto ERR(MySQL);

            snprintf(querybuff, sizeof querybuff, strings[STR_FR_GK], context->context->schema, args->inf->table);
            logTime(FRAMEWORK_META(context->out)->log, initime, strings[STR_FN], querybuff);
        }

    }
    else goto ERR(MySQL);
        
    for (;;)
    {
        succ = 1;
        break;

    ERR(Table):
        logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_EG], strings[STR_FN], strings[STR_M_ITN]);
        break;
        
    ERR(MySQL):
        if (mysqlcon) logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_EG], strings[STR_FN], mysql_error(mysqlcon));
        else logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_EG], strings[STR_FN], strings[STR_M_SQL]);
        break;
    }

    mysql_close(mysqlcon);
    mysql_thread_end();
    remove(filebuff);

    for (;;)
    {
        break;
    
    ERR(FileName):
        logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_EG], strings[STR_FN], strings[STR_M_IFN]);
        break;

    ERR(File):
        strerror_s(tempbuff, sizeof tempbuff, errno);
        logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_EI], strings[STR_FN], filebuff, tempbuff);
        break;

    ERR(Write):
        logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_EG], strings[STR_FN], strings[STR_M_WRF]);
        break;
    }

    if (f) fclose(f);
    return succ;
}

#define MYSQLDISPATCH_MIN_LINES 10000000
#define MYSQLDISPATCH_MIN_LINES_HALF (MYSQLDISPATCH_MIN_LINES >> 1)

static bool mysqlDispatchSchedule(taskSupp *supp, const mysqlDispatchThreadInfo *inf, mysqlDispatchThreadContext *context, size_t linecnt, logInfo *log)
{
    size_t cnt = 1; // (((linecnt + MYSQLDISPATCH_MIN_LINES_HALF - 1) / MYSQLDISPATCH_MIN_LINES_HALF) + 1) >> 1;

    if (!(
        arrayInit((void **) &supp->tasks, cnt, sizeof *supp->tasks) &&
        arrayInit((void **) &supp->args, cnt, sizeof (mysqlDispatchThreadArgs)))) goto ERR();
        
    for (size_t i = 0, offset = 0; i < cnt; i++)
    {
        size_t length = linecnt / (cnt - i);
        ((mysqlDispatchThreadArgs *) supp->args)[i] = (mysqlDispatchThreadArgs) { .inf = inf, .offset = offset, .length = length };
        
        supp->tasks[i] = (task)
        {
            .callback = (taskCallback) mysqlDispatchThreadProc,
            .asucc = (aggregatorCallback) sizeIncInterlocked,
            .afail = (aggregatorCallback) sizeIncInterlocked,
            .arg = &((mysqlDispatchThreadArgs *) supp->args)[i],
            .context = context,
            .asuccmem = &supp->succ,
            .afailmem = &supp->fail
        };
                
        offset += length;
        linecnt -= length;
    }

    supp->taskscnt = cnt;
    return 1;

ERR():
    logError(log, __FUNCTION__, errno);
    return 0;
}

bool mysqlDispatchThreadPrologue(mysqlDispatchOut *args, mysqlDispatchContext *context)
{
    loadDataRes *res = LOADDATA_META(args)->res;
    mysqlDispatchSupp *supp = &args->supp;
    if (!mysqlInitSchema(args, context)) return 0;

    supp->context = (mysqlDispatchThreadContext) { .out = args, .context = context };

    if (!arrayInit((void **) &supp->transsupp[MYSQLDISPATCHSUPP_TRANSSUPP_TRANSITION].tasks, MYSQLDISPATCHSUPP_TASKSUPP_CNT, sizeof *supp->transsupp[MYSQLDISPATCHSUPP_TRANSSUPP_TRANSITION].tasks)) goto ERR();
        
    // Additional configuration which not needed to be stored globally
    const struct { ptrdiff_t offcnt; size_t *compbit; } schemaConf[] =
    {
        { offsetof(loadDataRes, chrcnt), pnumGet(FRAMEWORK_META(args)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_POS_CHR_COMP) },
        { offsetof(loadDataRes, chrcnt), pnumGet(FRAMEWORK_META(args)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_POS_CHRSUPP_COMP) },
        { offsetof(loadDataRes, testcnt), pnumGet(FRAMEWORK_META(args)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_POS_TEST_COMP) },
        { offsetof(loadDataRes, snpcnt), pnumGet(FRAMEWORK_META(args)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_POS_IND_COMP) },
        { offsetof(loadDataRes, snpcnt), pnumGet(FRAMEWORK_META(args)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_POS_POS_COMP) },
        { offsetof(loadDataRes, snpcnt), pnumGet(FRAMEWORK_META(args)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_POS_ALIAS_COMP) },
        //{ offsetof(loadDataRes, snpcnt), pnumGet(FRAMEWORK_META(args)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_POS_ALLELE_COMP) },
        { offsetof(loadDataRes, pvcnt), pnumGet(FRAMEWORK_META(args)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_POS_VIND_COMP) },
        { offsetof(loadDataRes, pvcnt), pnumGet(FRAMEWORK_META(args)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_POS_LPV_COMP) },
        { offsetof(loadDataRes, pvcnt), pnumGet(FRAMEWORK_META(args)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_POS_QAS_COMP) },
        //{ offsetof(loadDataRes, pvcnt), pnumGet(FRAMEWORK_META(args)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_POS_MAF_COMP) },
        { offsetof(loadDataRes, pvcnt), pnumGet(FRAMEWORK_META(args)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_POS_RLPV_COMP) },
        { offsetof(loadDataRes, pvcnt), pnumGet(FRAMEWORK_META(args)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_POS_RQAS_COMP) }
    };

    static_assert(countof(schemaInfo) == MYSQLDISPATCHSUPP_TASKSUPP_CNT && countof(schemaConf) == MYSQLDISPATCHSUPP_TASKSUPP_CNT, "Wrong count of elements!");

    for (size_t i = MYSQLDISPATCHSUPP_TASKSUPP_CNT; i--;)
    {
        if (!mysqlDispatchSchedule(&supp->tasksupp[i], &schemaInfo[i], &supp->context, *(size_t *) memberof(res, schemaConf[i].offcnt), FRAMEWORK_META(args)->log)) goto ERR(Empty);

        supp->transsupp[MYSQLDISPATCHSUPP_TRANSSUPP_TRANSITION].tasks[supp->transsupp[MYSQLDISPATCHSUPP_TRANSSUPP_TRANSITION].taskscnt++] =
            TASK_BIT_1_INIT(mysqlDispatchTransition, mysqlDispatchTransitionCondition, &supp->tasksupp[i], NULL, &supp->tasksupp[i], supp->stat, schemaConf[i].compbit);
    }
    
    for (size_t i = MYSQLDISPATCHSUPP_TRANSSUPP_CNT; i--;)
        if (!threadPoolEnqueueTasks(FRAMEWORK_META(args)->pool, supp->transsupp[i].tasks, supp->transsupp[i].taskscnt, 1)) goto ERR(Pool);

    for (size_t i = MYSQLDISPATCHSUPP_TASKSUPP_CNT; i--;)
        if (!threadPoolEnqueueTasks(FRAMEWORK_META(args)->pool, supp->tasksupp[i].tasks, supp->tasksupp[i].taskscnt, 1)) goto ERR(Pool);
       
    for (;;)
    {
        return 1;

    ERR():
        logError(FRAMEWORK_META(args)->log, __FUNCTION__, errno);
        break;

    ERR(Pool):
        logMsg(FRAMEWORK_META(args)->log, "ERROR (%s): Unable to enqueue tasks!\n", __FUNCTION__);
        break;

    ERR(Empty):
        break;
    }

    return 0;
}

void mysqlDispatchContextDispose(mysqlDispatchContext *context)
{
    if (!context) return;
    
    free(context->temppr);
    free(context->schema);
    free(context->host);
    free(context->login);
    free(context->password);
    free(context);
}

bool mysqlDispatchPrologue(mysqlDispatchIn *in, mysqlDispatchOut **pout, mysqlDispatchContext *context)
{
    mysqlDispatchOut *out = *pout = malloc(sizeof *out);
    if (!out) goto ERR();

    *out = (mysqlDispatchOut) { .meta = MYSQLDISPATCH_META_INIT(in, out, context) };
    if (!outInfoSetNext(FRAMEWORK_META(in)->out, out)) goto ERR();

    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();

    if (!pnumTest(FRAMEWORK_META(out)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_CNT)) goto ERR();
    *tsk = TASK_BIT_2_INIT(mysqlDispatchThreadPrologue, bitTestMem, out, context, LOADDATA_META(in)->stat, out->supp.stat, pnumGet(FRAMEWORK_META(out)->pnum, LOADDATASUPP_STAT_BIT_POS_TASK_SUCC), pnumGet(FRAMEWORK_META(out)->pnum, MYSQLDISPATCHSUPP_STAT_BIT_POS_INIT_COMP));
    sizeIncInterlocked(LOADDATA_META(in)->hold, NULL); // Increase the chromosome data hold counter
    return 1;

ERR() :
    logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);

    return 0;
}

static bool mysqlDispatchClose(mysqlDispatchOut *args, void *context)
{
    (void) context;

    bool pend = 0;
    mysqlDispatchSupp *supp = &args->supp;

    for (size_t i = 0; i < MYSQLDISPATCHSUPP_TASKSUPP_CNT; i++)
    {
        free(supp->tasksupp[i].args);
        
        // Before disposing task information all pending tasks should be dropped from the queue
        if (threadPoolRemoveTasks(FRAMEWORK_META(args)->pool, supp->tasksupp[i].tasks, supp->tasksupp[i].taskscnt) && !pend)
            logMsg(FRAMEWORK_META(args)->log, "WARNING (%s): Pending tasks were dropped!\n", __FUNCTION__), pend = 1;
        
        free(supp->tasksupp[i].tasks);
    }

    for (size_t i = 0; i < MYSQLDISPATCHSUPP_TRANSSUPP_CNT; i++)
    {
        if (threadPoolRemoveTasks(FRAMEWORK_META(args)->pool, supp->transsupp[i].tasks, supp->transsupp[i].taskscnt) && !pend)
            logMsg(FRAMEWORK_META(args)->log, "WARNING (%s): Pending tasks were dropped!\n", __FUNCTION__), pend = 1;

        free(supp->transsupp[i].tasks);
    }

    return 1;
}

static bool mysqlDispatchCloseCondition(mysqlDispatchSupp *supp, void *arg)
{
    (void) arg;

    switch (bitGet2((void *) supp->stat, MYSQLDISPATCHSUPP_STAT_BIT_POS_INIT_COMP))
    {
    case 0: return 0;
    case 1: return 1;
    case 3: break;
    }

    return bitTestRange2((void *) supp->stat, MYSQLDISPATCHSUPP_STAT_BIT_CNT >> 1);
}

bool mysqlDispatchEpilogue(mysqlDispatchIn *in, mysqlDispatchOut* out, void *context)
{
    (void) context;

    if (!out) return 0;

    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();

    *tsk = (task)
    {
        .callback = (taskCallback) mysqlDispatchClose,
        .cond = (conditionCallback) mysqlDispatchCloseCondition,
        .asucc = (aggregatorCallback) sizeDecInterlocked,
        .afail = (aggregatorCallback) sizeDecInterlocked,
        .arg = out,
        .asuccmem = LOADDATA_META(in)->hold,
        .afailmem = LOADDATA_META(in)->hold,
        .condmem = &out->supp
    };

    return 1;

ERR():
    logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);
    return 0;
}

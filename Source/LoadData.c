#include "Common.h"
#include "Compare.h"
#include "Debug.h"
#include "LoadData.h"
#include "Memory.h"
#include "Threading.h"
#include "TaskMacros.h"
#include "SystemInfo.h"
#include "Wrappers.h"
#include "x86_64/Tools.h"
#include "x86_64/Spinlock.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>

typedef struct
{
    size_t testcnt;
} headSelectorContext;

static size_t headSelector(char *str, size_t len, size_t *pos, headSelectorContext *context)
{
    (void) str;
    (void) len;
    (void) pos;
    (void) context;
    return SIZE_MAX;
}

static bool ranksComp(const double **a, const double **b, void *context)
{
    int8_t res = float64CompDscNaNStable(*a, *b, context);
    if (res > 0 || (!res && *a > *b)) return 1;
    return 0;
}

bool loadDataThreadRanksPrologue(loadDataThreadRanksArgs *args, loadDataMetadata *meta)
{
    loadDataRes *restrict res = meta->res;
    double *restrict arr = *(double **) memberof(res, args->val);
    uintptr_t *restrict rarr = *(uintptr_t **) memberof(res, args->rval);
    
    args->initime = getTime(); // Setting timer

    for (size_t i = 0; i < res->pvcnt; rarr[i] = (uintptr_t) &arr[i], i++);

    sortMTSync sync =
    {
        .asucc = (aggregatorCallback) bitSet2InterlockedMem,
        .asuccmem = meta->stat,
        .asuccarg = &args->sortbit
    };

    args->smt = sortMTCreate(rarr, res->pvcnt, sizeof *rarr, (compareCallback) ranksComp, NULL, FRAMEWORK_META(meta)->pool, &sync);
    if (args->smt) return 1;

    logMsg(FRAMEWORK_META(meta)->log, "ERROR (%s): Unable to compute ranks!\n", __FUNCTION__);
    return 0;
}

bool loadDataThreadRanksEpilogue(loadDataThreadRanksArgs *args, loadDataMetadata *meta)
{
    sortMTDispose(args->smt);

    loadDataRes *restrict res = meta->res;
    double *restrict arr = *(double **) memberof(res, args->val);
    uintptr_t *restrict rarr = *(uintptr_t **) memberof(res, args->rval);

    if (!createRanks(rarr, (uintptr_t) arr, res->pvcnt, sizeof *arr)) goto ERR();
        
    logTime(FRAMEWORK_META(meta)->log, args->initime, __FUNCTION__, "Ranks computation");
    return 1;

ERR() :
    logMsg(FRAMEWORK_META(meta)->log, "ERROR (%s): Unable to compute ranks!\n", __FUNCTION__);
    return 0;
}

void loadDataRanksSchedule(loadDataThreadRanksArgs *rarg, loadDataMetadata *meta, size_t lbit, task *rtask, ptrdiff_t val, ptrdiff_t rval, size_t sbit, size_t rbit, size_t *pcnt)
{
    *rarg = (loadDataThreadRanksArgs) { .val = val, .rval = rval, .sortbit = sbit, .rankbit = rbit, .loadbit = lbit };
    
    rtask[(*pcnt)++] = (task) // Warning: non-usual task initialization!
    {
        .callback = (taskCallback) loadDataThreadRanksPrologue,
        .cond = (conditionCallback) bitTestMem,
        .afail = (aggregatorCallback) bitSetInterlockedMem,
        .arg = rarg,
        .context = meta,
        .condmem = meta->stat,
        .afailmem = meta->stat,
        .condarg = &rarg->loadbit,
        .afailarg = &rarg->sortbit
    };
    
    rtask[(*pcnt)++] = TASK_BIT_2_INIT(loadDataThreadRanksEpilogue, bitTest2Mem, rarg, meta, meta->stat, meta->stat, &rarg->sortbit, &rarg->rankbit);
}

///////////////////////////////////////////////////////////////////////////////

typedef struct // Input
{
    tblsch *sch;
    char *path;
    size_t offset, length; // 'offset' should be aligned to the line boundary (after the '\n' character)!
} loadDataThreadReadArgsCommon;

#define READARGS_COMMON(X) ((loadDataThreadReadArgsCommon *) X)

struct loadDataThreadReadArgs
{
    loadDataThreadReadArgsCommon common; // Input
    struct // Output
    {
        size_t rowcnt; // Numbers of rows
        void **context, **res; // arrays of size 'sch->colscnt'
        struct
        {
            char **strtbl;
            size_t *strtblcnt, *strtblcap;
            size_t strtbldim;
        };
    };
};

// This destructor function should be idempotent
static void loadDataThreadReadArgsCloseTest(loadDataThreadReadArgs *args)
{
    if (!args) return;

    if (args->context)
    {
        for (size_t i = 0; i < READARGS_COMMON(args)->sch->colschcnt; free(args->context[i++]));
        free(args->context);
        args->context = NULL;
    }   

    if (args->res)
    {
        tblClose(args->res, READARGS_COMMON(args)->sch);
        free(args->res);
        args->res = NULL;
    }
    
    if (args->strtbl)
    {
        for (size_t i = 0; i < args->strtbldim; free(args->strtbl[i++]));
        free(args->strtbl);
        args->strtbl = NULL;
    }

    free(args->strtblcnt);
    args->strtblcnt = NULL;

    free(args->strtblcap);
    args->strtblcap = NULL;
}

static bool loadDataThreadRead(loadDataThreadReadArgs *args, loadDataThreadContext *context)
{
    const char *strings[] =
    {
        __FUNCTION__,
        "Reading %zu rows from %zu B of the file \"%s\" + %zu B",
        "ERROR (%s): %s!\n",
        "ERROR (%s): Cannot open specified file \"%s\". %s!\n",
        "ERROR (%s): Failed to read CSV table (file \"%s\" + %zu B, row %zu, column %zu, byte %zu). %s!\n"
    };

    enum { STR_FN, STR_FR_GR, STR_FR_EG, STR_FR_EI, STR_FR_ER };
    
    char tempbuff[TEMP_BUFF] = { '\0' };    
    FILE *f = NULL;
    
    uint64_t initime = getTime();

    f = fopen(READARGS_COMMON(args)->path, "r");
    if (!f) goto ERR(File);

    size_t rowcnt = rowCount(f, READARGS_COMMON(args)->offset, READARGS_COMMON(args)->length), rowskip = 0;
    if (!rowcnt) goto ERR(); // Never happens?..

    if (bitTest(context->context->bits, LOADDATACONTEXT_BIT_POS_HEADER)) rowskip = !READARGS_COMMON(args)->offset; // When we are at the beginning of the file, table head should be skipped
    
    args->res = calloc(READARGS_COMMON(args)->sch->colschcnt, sizeof *args->res);
    if (!args->res) goto ERR();

    args->context = calloc(READARGS_COMMON(args)->sch->colschcnt, sizeof *args->context);
    if (!args->context) goto ERR();

    if (!tblInit(args->res, READARGS_COMMON(args)->sch, rowcnt - rowskip, 0)) goto ERR();
    
    for (size_t i = 0; i <  READARGS_COMMON(args)->sch->colschcnt; i++) if (READARGS_COMMON(args)->sch->colsch[i].handler.read == (readHandlerCallback) strTableHandler)
    {
        args->context[i] = malloc(sizeof (strTableHandlerContext));
        if (!args->context[i]) goto ERR();

        args->strtbldim++;
    }
    
    if (args->strtbldim)
    {
        args->strtbl = calloc(args->strtbldim, sizeof *args->strtbl);
        if (!args->strtbl) goto ERR();
        
        args->strtblcnt = calloc(args->strtbldim, sizeof *args->strtblcnt);
        if (!args->strtblcnt) goto ERR();
        
        args->strtblcap = calloc(args->strtbldim, sizeof *args->strtblcap);
        if (!args->strtblcap) goto ERR();
        
        for (size_t i = 0, j = 0; i <  READARGS_COMMON(args)->sch->colschcnt; i++) 
            if (READARGS_COMMON(args)->sch->colsch[i].handler.read == (readHandlerCallback) strTableHandler)
            {
                *(strTableHandlerContext *) args->context[i] = (strTableHandlerContext) { .strtbl = &args->strtbl[j], .strtblcnt = &args->strtblcnt[j], .strtblcap = &args->strtblcap[j] };
                j++;
            }
    }
    
    if (fseek64(f, READARGS_COMMON(args)->offset, SEEK_SET)) goto ERR(); // Never happens?
    
    rowReadRes res;
    if (!rowRead(f, READARGS_COMMON(args)->sch, args->res, args->context, rowskip, rowcnt, 0, &res, ',')) goto ERR(Data);

    if (args->strtbldim) 
        for (size_t i = 0, j = 0; i < READARGS_COMMON(args)->sch->colschcnt; i++) 
            if (READARGS_COMMON(args)->sch->colsch[i].handler.read == (readHandlerCallback) strTableHandler) 
            {
                if (!dynamicArrayFinalize((void **) &args->strtbl[j], &args->strtblcap[j], 1, args->strtblcnt[j])) goto ERR();
                j++;
            }

    snprintf(tempbuff, sizeof tempbuff, strings[STR_FR_GR], res.read, READARGS_COMMON(args)->length, READARGS_COMMON(args)->path, READARGS_COMMON(args)->offset);
    logTime(FRAMEWORK_META(context->out)->log, initime, strings[STR_FN], tempbuff);

    args->rowcnt = res.read;

    fclose(f);
    
    for (;;)
    {
        return 1;

    ERR():
        strerror_s(tempbuff, sizeof tempbuff, errno);
        logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_EG], strings[STR_FN], tempbuff);
        break;
        
    ERR(File):
        strerror_s(tempbuff, sizeof tempbuff, errno);
        logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_EI], strings[STR_FN], READARGS_COMMON(args)->path, tempbuff);
        break;

    ERR(Data):
        logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_ER], strings[STR_FN], READARGS_COMMON(args)->path, READARGS_COMMON(args)->offset, res.row + 1, res.col + 1, res.byte + 1, rowReadError(res.err));
        break;
    }
    
    loadDataThreadReadArgsCloseTest(args);
    if (f) fclose(f);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

typedef struct
{
    size_t chr, row, ind;
    loadDataRes *res;
} loadDataValContext;

typedef struct loadDataThreadReadValArgs
{
    loadDataThreadReadArgsCommon common;
    loadDataValContext context;
} loadDataThreadReadValArgs;

static bool loadDataColumnHandlerChr(const char *str, size_t len, void *ptr, loadDataValContext *context)
{
    (void) len; (void) ptr;
    
    char *test;
    uint16_t res = (uint16_t) strtoul(str, &test, 10);
    if (*test || !res || res > context->res->chrcnt) return 0;
    context->chr = res - 1;
    
    return 1;
}

static bool loadDataColumnHandlerRow(const char *str, size_t len, void *ptr, loadDataValContext *context)
{
    (void) len; (void) ptr;
    
    char *test;
    uint32_t res = (uint32_t) strtoul(str, &test, 10);
    if (*test || !res || res > context->res->chrlen[context->chr]) return 0;
    context->row = res - 1;
    
    return 1;
}

static bool loadDataColumnHandlerTest(const char *str, size_t len, void *ptr, loadDataValContext *context)
{
    (void) len; (void) ptr;
    
    char *test;
    uint32_t res = (uint32_t) strtoul(str, &test, 10);
    if (*test || !res || res > context->res->testcnt) return 0;
    context->ind = context->res->snpcnt * (res - 1) + context->res->chroff[context->chr] + context->row;
    
    return 1;
}

static bool loadDataColumnHandlerLpvLog(const char *str, size_t len, void *ptr, loadDataValContext *context)
{
    (void) len; (void) ptr;
    
    char *test;
    double res = (double) strtod(str, &test);
    if (*test) return 0;
    if (res > 0 && res < 1) context->res->nlpv[context->ind] = -log10(res); // Quite NaN otherwise
    
    return 1;
}

static bool loadDataColumnHandlerQas(const char *str, size_t len, void *ptr, loadDataValContext *context)
{
    (void) len; (void) ptr;
    
    char *test;
    double res = (double) strtod(str, &test);
    if (*test) return 0;
    if (res >= 0)
        context->res->qas[context->ind] = res; // Quite NaN otherwise
    
    return 1;
}

static bool loadDataColumnHandlerLpv(const char *str, size_t len, void *ptr, loadDataValContext *context)
{
    (void) len; (void) ptr;
    
    char *test;
    double res = (double) strtod(str, &test);
    if (*test) return 0;
    if (res >= 0) context->res->nlpv[context->ind] = res; // Quite NaN otherwise
    
    return 1;
}

/*
static bool loadDataColumnHandlerMaf(const char *str, size_t len, void *ptr, loadDataValContext *context)
{
    (void) len; (void) ptr;

    char *test;
    double res = (double) strtod(str, &test);
    if (*test) return 0;
    context->res->maf[context->ind] = res; // Quite NaN otherwise

    return 1;
}
*/

// Highly simplified version of 'loadDataThreadRead'
static bool loadDataThreadReadVal(loadDataThreadReadValArgs *args, loadDataThreadContext *context)
{
    const char *strings[] =
    {
        __FUNCTION__,
        "Reading %zu rows from %zu B of the file \"%s\" + %zu B",
        "ERROR (%s): %s!\n",
        "ERROR (%s): Cannot open specified file \"%s\". %s!\n",
        "ERROR (%s): Failed to read CSV table (file \"%s\" + %zu B, row %zu, column %zu, byte %zu). %s!\n"
    };

    enum { STR_FN, STR_FR_GR, STR_FR_EG, STR_FR_EI, STR_FR_ER };
    
    char tempbuff[TEMP_BUFF] = { '\0' };
    FILE *f = NULL;

    uint64_t initime = getTime();

    f = fopen(READARGS_COMMON(args)->path, "r");
    if (!f) goto ERR(File);

    size_t rowskip = 0;
    
    if (bitTest(context->context->bits, LOADDATACONTEXT_BIT_POS_HEADER)) rowskip = !READARGS_COMMON(args)->offset; // When we are at the beginning of the file, table head should be skipped
    if (fseek64(f, READARGS_COMMON(args)->offset, SEEK_SET)) goto ERR(); // Never happens?

    args->context.res = &context->out->res;
    
    rowReadRes res;
    if (!rowRead(f, READARGS_COMMON(args)->sch, (void *[]) { NULL }, (void *[]) { &args->context }, rowskip, 0, READARGS_COMMON(args)->length, &res, ',')) goto ERR(Data);
   
    snprintf(tempbuff, sizeof tempbuff, strings[STR_FR_GR], res.read, READARGS_COMMON(args)->length, READARGS_COMMON(args)->path, READARGS_COMMON(args)->offset);
    logTime(FRAMEWORK_META(context->out)->log, initime, strings[STR_FN], tempbuff);

    fclose(f);
    
    for (;;)
    {
        return 1;

    ERR():        
        strerror_s(tempbuff, sizeof tempbuff, errno);
        logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_EG], strings[STR_FN], tempbuff);
        break;

    ERR(File):
        strerror_s(tempbuff, sizeof tempbuff, errno);
        logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_EI], strings[STR_FN], READARGS_COMMON(args)->path, tempbuff);
        break;

    ERR(Data) :
        logMsg(FRAMEWORK_META(context->out)->log, strings[STR_FR_ER], strings[STR_FN], READARGS_COMMON(args)->path, READARGS_COMMON(args)->offset, res.row + 1, res.col + 1, res.byte + 1, rowReadError(res.err));
        break;
    }
        
    if (f) fclose(f);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

static bool loadDataThreadEpilogue(void *args, void *context)
{
    (void) args;
    (void) context;

    return 1;
}

static bool loadDataThreadTransitionCondition(taskSupp *supp, void *arg)
{
    (void) arg;
    return supp->succ + supp->fail == supp->taskscnt;
}

static bool loadDataThreadTransition(taskSupp *args, void *context)
{
   (void) context;
   return !args->fail;
}

typedef struct
{
    size_t offset, cnt;
    loadDataThreadReadArgs *args;
} loadDataThreadCombinePartialArgs;

#define ERROR_ZER(NO, ROW) \
    do { \
        logMsg(FRAMEWORK_META(context->out)->log, "ERROR (%s): Zero value is not permitted for the field no. %u (row %zu)!\n", __FUNCTION__, (unsigned) (NO), (ROW)); \
        return 0; \
    } while (0)

#define ERROR_RAN(NO, ROW) \
    do { \
        logMsg(FRAMEWORK_META(context->out)->log, "ERROR (%s): Value in the field no. %u (row %zu) is out of range!\n", __FUNCTION__, (unsigned) (NO), (ROW)); \
        return 0; \
    } while (0)

// Specialized combine routine for chromosome table
bool loadDataThreadProcCombineChr(loadDataThreadCombineArgs *args, loadDataThreadContext *context)
{
    enum { COL_CHR = 0, COL_CHRSTR };
    enum { TEXT_CHRSTR = 0 };

    size_t chromcnt = 0, chrnamestrsz = 0;

    for (size_t i = 0, tot = 0; i < args->cnt; i++, tot += args->args[i].rowcnt)
    {
        for (size_t j = 0; j < args->args[i].rowcnt; j++)
        {
            uint16_t chr = ((uint16_t *) args->args[i].res[COL_CHR])[j];

            if (!chr) ERROR_ZER(COL_CHR, tot + j + 1);
            if (chr > chromcnt) chromcnt = chr;
        }

        chrnamestrsz += args->args[i].strtblcnt[TEXT_CHRSTR];
    }

    loadDataRes *res = &context->out->res;

    res->chrcnt = chromcnt;
    res->chrnamestrsz = chrnamestrsz;

    if (!(
        arrayInitClear((void **) &res->chrlen, chromcnt, sizeof *res->chrlen) &&
        arrayInit((void **) &res->chrname, chromcnt, sizeof *res->chrname) &&
        arrayInit((void **) &res->chroff, chromcnt, sizeof *res->chroff) &&
        arrayInit((void **) &res->chrnamestr, chrnamestrsz, sizeof *res->chrnamestr))) goto ERR();

    for (size_t i = 0, off = 0; i < args->cnt; i++)
    {
        for (size_t j = 0; j < args->args[i].rowcnt; j++)
            res->chrname[((uint16_t *) args->args[i].res[COL_CHR])[j] - 1] = ((ptrdiff_t *) args->args[i].res[COL_CHRSTR])[j] + off;

        memcpy(res->chrnamestr + off, args->args[i].strtbl[TEXT_CHRSTR], args->args[i].strtblcnt[TEXT_CHRSTR]);
        off += args->args[i].strtblcnt[TEXT_CHRSTR];

        loadDataThreadReadArgsCloseTest(&args->args[i]);
    }

    return 1;

ERR():
    logError(FRAMEWORK_META(context->out)->log, __FUNCTION__, errno);
    return 0;
}

// Specialized combine routines for test table
bool loadDataThreadProcCombineTest(loadDataThreadCombineArgs *args, loadDataThreadContext *context)
{
    enum { COL_TEST = 0, COL_TESTSTR };
    enum { TEXT_TESTSTR = 0 };
    
    size_t testcnt = 0, testnamestrsz = 0;
    
    for (size_t i = 0, tot = 0; i < args->cnt; i++, tot += args->args[i].rowcnt)
    {
        for (size_t j = 0; j < args->args[i].rowcnt; j++)
        {
            uint32_t test = ((uint32_t *) args->args[i].res[COL_TEST])[j];
            
            if (!test) ERROR_ZER(COL_TEST, tot + j + 1);
            if (test > testcnt) testcnt = test;
        }
        
        testnamestrsz += args->args[i].strtblcnt[TEXT_TESTSTR];
    }
    
    loadDataRes *res = &context->out->res;
    
    res->testcnt = testcnt;
    res->testnamestrsz = testnamestrsz;
    
    if (!(
        arrayInit((void **) &res->testname, testcnt, sizeof *res->testname) &&
        arrayInit((void **) &res->testnamestr, testnamestrsz, sizeof *res->testnamestr))) goto ERR();

    for (size_t i = 0, off = 0; i < args->cnt; i++)
    {
        for (size_t j = 0; j < args->args[i].rowcnt; j++)
            res->testname[((uint32_t *) args->args[i].res[COL_TEST])[j] - 1] = ((ptrdiff_t *) args->args[i].res[COL_TESTSTR])[j] + off;

        memcpy(res->testnamestr + off, args->args[i].strtbl[TEXT_TESTSTR], args->args[i].strtblcnt[TEXT_TESTSTR]);
        off += args->args[i].strtblcnt[TEXT_TESTSTR];

        loadDataThreadReadArgsCloseTest(&args->args[i]);
    }    
    
    return 1;
    
ERR():
    logError(FRAMEWORK_META(context->out)->log, __FUNCTION__, errno);
    return 0;
}

// Specialized combine routines for test table
bool loadDataThreadProcCombineRow(loadDataThreadCombineArgs *args, loadDataThreadContext *context)
{
    enum { COL_CHR = 0, COL_ROW, COL_SNP, COL_POS, COL_ALLELE, COL_TMAF };
    enum { TEXT_SNP = 0, TEXT_ALLELE };
    
    size_t snpstrsz = 0, allelestrsz = 0;
    loadDataRes *res = &context->out->res;
    
    for (size_t i = 0, tot = 0; i < args->cnt; i++, tot += args->args[i].rowcnt)
    {
        for (size_t j = 0; j < args->args[i].rowcnt; j++)
        {
            uint16_t chr = ((uint16_t *) args->args[i].res[COL_CHR])[j];
            uint32_t row = ((uint32_t *) args->args[i].res[COL_ROW])[j];
            
            if (!chr) ERROR_ZER(COL_CHR, tot + j + 1);
            if (!row) ERROR_ZER(COL_ROW, tot + j + 1);
            
            if (row > res->chrlen[chr - 1]) res->chrlen[chr - 1] = row;
        }
        
        snpstrsz += args->args[i].strtblcnt[TEXT_SNP];
        allelestrsz += args->args[i].strtblcnt[TEXT_ALLELE];
    }
    
    size_t snpcnt = 0;
    for (size_t i = 0; i < res->chrcnt; res->chroff[i] = snpcnt, snpcnt += res->chrlen[i], i++);
    
    res->snpcnt = snpcnt;
    res->snpnamestrsz = snpstrsz;
    //res->allelenamestrsz = allelestrsz;
    
    if (!(
        arrayInit((void **) &res->snpname, snpcnt, sizeof *res->snpname) &&
        arrayInitClear((void **) &res->genename, snpcnt, sizeof *res->genename) && // for the sake of consistency all elements of 'gene' array points to zero
        //arrayInit((void **) &res->allelename, snpcnt, sizeof *res->allelename) &&
        arrayInit((void **) &res->pos, snpcnt, sizeof *res->pos) &&
        arrayInit((void **) &res->snpnamestr, snpstrsz, sizeof *res->snpnamestr) &&
        arrayInitClear((void **) &res->genenamestr, 1, sizeof *res->genenamestr) // && 'genestr' array is initialized as zero-string
        //arrayInit((void **) &res->tmaf, snpcnt, sizeof *res->tmaf)
        //arrayInit((void **) &res->allelenamestr, allelestrsz, sizeof *res->allelenamestr)
        )) goto ERR();

    //MEMORY_SET(res->tmaf, snpcnt);
    
    for (size_t i = 0, offsnp = 0/*, offallele = 0*/; i < args->cnt; i++)
    {
        for (size_t j = 0; j < args->args[i].rowcnt; j++)
        {
            uint16_t chr = ((uint16_t *) args->args[i].res[COL_CHR])[j];
            uint32_t row = ((uint32_t *) args->args[i].res[COL_ROW])[j];
            size_t ind = res->chroff[chr - 1] + row - 1;

            res->snpname[ind] = ((ptrdiff_t *) args->args[i].res[COL_SNP])[j] + offsnp;
            //res->tmaf[ind] = ((double *) args->args[i].res[COL_TMAF])[j];
            //res->allelename[ind] = ((ptrdiff_t *) args->args[i].res[COL_ALLELE])[j] + offallele;
            res->pos[ind] = ((uint32_t *) args->args[i].res[COL_POS])[j];
        }

        memcpy(res->snpnamestr + offsnp, args->args[i].strtbl[TEXT_SNP], args->args[i].strtblcnt[TEXT_SNP]);
        offsnp += args->args[i].strtblcnt[TEXT_SNP];

        //memcpy(res->allelenamestr + offallele, args->args[i].strtbl[TEXT_ALLELE], args->args[i].strtblcnt[TEXT_ALLELE]);
        //offallele += args->args[i].strtblcnt[TEXT_ALLELE];

        loadDataThreadReadArgsCloseTest(&args->args[i]);
    }
    
    // Checking for position correctness
    for (size_t i = 0; i < res->chrcnt; i++)
    {
        for (size_t j = res->chroff[i] + 1; j < res->chroff[i] + res->chrlen[i]; j++) if (res->pos[j - 1] > res->pos[j])
        {
            logMsg(FRAMEWORK_META(context->out)->log,"ERROR (%s): Discordant genome positions provided (first occurrence on chr %zu, nrow %zu)!\n", __FUNCTION__, i + 1, j - res->chroff[i]);
            return 0;
        }
    }
    
    size_t lpvcnt = res->pvcnt = snpcnt * res->testcnt;
    
    if (!(
        arrayInit((void **) &res->nlpv, lpvcnt, sizeof *res->nlpv) &&
        arrayInit((void **) &res->qas, lpvcnt, sizeof *res->qas) &&
        //arrayInit((void **) &res->maf, lpvcnt, sizeof *res->maf) &&
        arrayInitClear((void **) &res->rqas, lpvcnt, sizeof *res->rqas) &&
        arrayInitClear((void **) &res->rnlpv, lpvcnt, sizeof *res->rnlpv)
        )) goto ERR();

    MEMORY_SET(res->qas, lpvcnt);
    MEMORY_SET(res->nlpv, lpvcnt);
    //MEMORY_SET(res->maf, lpvcnt);
        
    return 1;
    
ERR():
    logError(FRAMEWORK_META(context->out)->log, __FUNCTION__, errno);
    return 0;
}

#undef ERROR_ZER
#undef ERROR_RAN

///////////////////////////////////////////////////////////////////////////////

#define LOADDATA_MIN_BLOCK 8096
#define LOADDATA_MIN_BLOCK_HALF (LOADDATA_MIN_BLOCK >> 1)

typedef bool (*readCallback)(loadDataThreadReadArgsCommon *, loadDataThreadContext *context);

static bool loadDataSchedule(char *path, tblsch *sch, taskSupp *supp, loadDataThreadContext *context, size_t cnt,
    logInfo *log, size_t argsz, readCallback callback, conditionCallback condition, volatile void *condmem, void *condarg)
{
    FILE *f = fopen(path, "r");
    if (!f) goto ERR();

    fseek64(f, 0, SEEK_END);
    size_t sz = ftell64(f);
        
    cnt = min(cnt, (((sz + LOADDATA_MIN_BLOCK_HALF - 1) / LOADDATA_MIN_BLOCK_HALF) + 1) >> 1);
    
    if (!arrayInit((void **) &supp->tasks, cnt, sizeof *supp->tasks)) goto ERR();
    if (!arrayInitClear((void **) &supp->args, cnt, argsz)) goto ERR();

    loadDataThreadReadArgsCommon *argiter = supp->args;
    
    supp->taskscnt = 0;
    
    for (size_t i = cnt, offset = 0; i; i--)
    {
        size_t temp = offset + sz / i, corr = rowAlign(f, temp);
        if (corr == SIZE_MAX) goto ERR(Align);

        size_t length = corr - offset;
        if (!length) continue;

        *argiter = (loadDataThreadReadArgsCommon) { .path = path, .sch = sch, .offset = offset, .length = length };
        
        supp->tasks[supp->taskscnt++] = (task)
        {
            .callback = (taskCallback) callback,
            .cond = condition,
            .asucc = (aggregatorCallback) sizeIncInterlocked,
            .afail = (aggregatorCallback) sizeIncInterlocked,
            .arg = argiter,
            .context = context,
            .condmem = condmem,
            .asuccmem = &supp->succ,
            .afailmem = &supp->fail,
            .condarg = condarg
        };

        ptrinc(argiter, argsz);
        
        offset = corr;
        sz -= length;
    }

    return 1;

ERR():
    for (;;)
    {
        logError(log, __FUNCTION__, errno);
        break;

    ERR(Align):
         logMsg(log, "ERROR (%s): Physical and virtual file sizes differs!\n", __FUNCTION__);
        break;
    }

    //loadDataThreadReadArgsCloseTest(supp->args);
    //free(supp->args);
    if (f) fclose(f);
     
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

static const tblsch statSchChr = CLII((tblcolsch[])
{
    { .handler = { .read = (readHandlerCallback) uint16Handler }, .ind = 0, .size = sizeof(uint16_t) },
    { .handler = { .read = (readHandlerCallback) strTableHandler }, .ind = 1, .size = sizeof(ptrdiff_t) },
});

static const tblsch statSchTest= CLII((tblcolsch[])
{
    { .handler = { .read = (readHandlerCallback) uint32Handler }, .ind = 0, .size = sizeof(uint32_t) },
    { .handler = { .read = (readHandlerCallback) strTableHandler }, .ind = 1, .size = sizeof(ptrdiff_t) },
});

static const tblsch statSchRow = CLII((tblcolsch[])
{
    { .handler = { .read = (readHandlerCallback) uint16Handler }, .ind = 0, .size = sizeof(uint16_t) },
    { .handler = { .read = (readHandlerCallback) uint32Handler }, .ind = 1, .size = sizeof(uint32_t) },
    { .handler = { .read = (readHandlerCallback) strTableHandler }, .ind = 2, .size = sizeof(ptrdiff_t) },
    { .handler = { .read = (readHandlerCallback) uint32Handler }, .ind = 3, .size = sizeof(uint32_t) },
    { .handler = { .read = (readHandlerCallback) strTableHandler }, .ind = 4, .size = sizeof(ptrdiff_t) },
    { .handler = { .read = (readHandlerCallback) float64Handler }, .ind = 5, .size = sizeof(double) },
});

static const tblsch statSchVal = CLII((tblcolsch[]) // Each field have dedicated handler for this time...
{
    { .handler = { .read = (readHandlerCallback) loadDataColumnHandlerChr } },
    { .handler = { .read = (readHandlerCallback) loadDataColumnHandlerRow } },
    { .handler = { .read = (readHandlerCallback) loadDataColumnHandlerTest } },
    { .handler = { .read = (readHandlerCallback) loadDataColumnHandlerLpv } },
    { .handler = { .read = (readHandlerCallback) loadDataColumnHandlerQas } },
    //{ .handler = { .read = (readHandlerCallback) loadDataColumnHandlerMaf } },
});

static const tblsch statSchValLog = CLII((tblcolsch[])
{
    { .handler = { .read = (readHandlerCallback) loadDataColumnHandlerChr } },
    { .handler = { .read = (readHandlerCallback) loadDataColumnHandlerRow } },
    { .handler = { .read = (readHandlerCallback) loadDataColumnHandlerTest } },
    { .handler = { .read = (readHandlerCallback) loadDataColumnHandlerLpvLog } },
    { .handler = { .read = (readHandlerCallback) loadDataColumnHandlerQas } },
    //{ .handler = { .read = (readHandlerCallback) loadDataColumnHandlerMaf } },
});

///////////////////////////////////////////////////////////////////////////////

static inline bool loadDataThreadPrologue(loadDataOut *args, loadDataContext *context)
{
    bool succ = 0;
       
    loadDataSupp *supp = &args->supp;
    size_t div = threadPoolGetCount(FRAMEWORK_META(args)->pool);
    
    bool ranks = !bitTest(context->bits, LOADDATACONTEXT_BIT_POS_NORANKS);

    ///////////////////////////////////////////////////////////////////////////
    
    if (!arrayInit((void **) &supp->transsupp[LOADDATASUPP_TRANSSUPP_TRANSITION].tasks, LOADDATASUPP_TRANS_TRANSITION_CNT, sizeof *supp->transsupp[LOADDATASUPP_TRANSSUPP_TRANSITION].tasks)) goto ERR();
    if (!arrayInit((void **) &supp->transsupp[LOADDATASUPP_TRANSSUPP_COMBINE].tasks, LOADDATASUPP_TRANS_COMBINE_CNT, sizeof *supp->transsupp[LOADDATASUPP_TRANSSUPP_COMBINE].tasks)) goto ERR();
    if (!arrayInit((void **) &supp->transsupp[LOADDATASUPP_TRANSSUPP_EPILOGUE].tasks, LOADDATASUPP_TRANS_EPILOGUE_CNT, sizeof *supp->transsupp[LOADDATASUPP_TRANSSUPP_EPILOGUE].tasks)) goto ERR();
       
    loadDataThreadCombineArgs *carg = supp->cargs;
    
    supp->context = (loadDataThreadContext) { .out = args, .context = context };
    
    ///////////////////////////////////////////////////////////////////////////
    
    //  Processing file with chromosome info
    
    supp->sch[LOADDATASUPP_TASKSUPP_LOADCHR] = (tblschPtr) &statSchChr;
    if (!loadDataSchedule(context->pathchr, supp->sch[LOADDATASUPP_TASKSUPP_LOADCHR], &supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADCHR], &supp->context, div, FRAMEWORK_META(args)->log, sizeof (loadDataThreadReadArgs), (readCallback) loadDataThreadRead, NULL, NULL, NULL)) goto ERR(Empty);
    
    supp->transsupp[LOADDATASUPP_TRANSSUPP_TRANSITION].tasks[supp->transsupp[LOADDATASUPP_TRANSSUPP_TRANSITION].taskscnt++] = 
        TASK_BIT_1_INIT(loadDataThreadTransition, loadDataThreadTransitionCondition, &supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADCHR], NULL, &supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADCHR], supp->sync, pnumGet(FRAMEWORK_META(args)->pnum, LOADDATASUPP_SYNC_BIT_POS_LOADCHR_COMP));
        
    *carg = (loadDataThreadCombineArgs) { .cnt = supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADCHR].taskscnt, .args = supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADCHR].args };
    supp->transsupp[LOADDATASUPP_TRANSSUPP_COMBINE].tasks[supp->transsupp[LOADDATASUPP_TRANSSUPP_COMBINE].taskscnt++] = 
        TASK_BIT_2_INIT(loadDataThreadProcCombineChr, bitTest2Mem, carg, &supp->context, supp->sync, supp->sync, pnumGet(FRAMEWORK_META(args)->pnum, LOADDATASUPP_SYNC_BIT_POS_LOADCHR_COMP), pnumGet(FRAMEWORK_META(args)->pnum, LOADDATASUPP_SYNC_BIT_POS_COMBINECHR_COMP));
    carg++;

    ///////////////////////////////////////////////////////////////////////////
    
    //  Scheduling the processing of the file with test info

    supp->sch[LOADDATASUPP_TASKSUPP_LOADTEST] = (tblschPtr) &statSchTest;
    if (!loadDataSchedule(context->pathtest, supp->sch[LOADDATASUPP_TASKSUPP_LOADTEST], &supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADTEST], &supp->context, div, FRAMEWORK_META(args)->log, sizeof(loadDataThreadReadArgs), (readCallback) loadDataThreadRead, NULL, NULL, NULL)) goto ERR(Empty);
    
    supp->transsupp[LOADDATASUPP_TRANSSUPP_TRANSITION].tasks[supp->transsupp[LOADDATASUPP_TRANSSUPP_TRANSITION].taskscnt++] = 
        TASK_BIT_1_INIT(loadDataThreadTransition, loadDataThreadTransitionCondition, &supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADTEST], NULL, &supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADTEST], supp->sync, pnumGet(FRAMEWORK_META(args)->pnum, LOADDATASUPP_SYNC_BIT_POS_LOADTEST_COMP));
        
    *carg = (loadDataThreadCombineArgs) { .cnt = supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADTEST].taskscnt, .args = supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADTEST].args };
    supp->transsupp[LOADDATASUPP_TRANSSUPP_COMBINE].tasks[supp->transsupp[LOADDATASUPP_TRANSSUPP_COMBINE].taskscnt++] = 
        TASK_BIT_2_INIT(loadDataThreadProcCombineTest, bitTest2Mem, carg, &supp->context, supp->sync, supp->sync, pnumGet(FRAMEWORK_META(args)->pnum, LOADDATASUPP_SYNC_BIT_POS_LOADTEST_COMP), pnumGet(FRAMEWORK_META(args)->pnum, LOADDATASUPP_SYNC_BIT_POS_COMBINETEST_COMP));
    carg++;

    ///////////////////////////////////////////////////////////////////////////

    //  Scheduling the processing of the file with row info

    supp->sch[LOADDATASUPP_TASKSUPP_LOADROW] = (tblschPtr) &statSchRow;
    if (!loadDataSchedule(context->pathrow, supp->sch[LOADDATASUPP_TASKSUPP_LOADROW], &supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADROW], &supp->context, div, FRAMEWORK_META(args)->log, sizeof(loadDataThreadReadArgs), (readCallback) loadDataThreadRead, NULL, NULL, NULL)) goto ERR(Empty);
    
    supp->transsupp[LOADDATASUPP_TRANSSUPP_TRANSITION].tasks[supp->transsupp[LOADDATASUPP_TRANSSUPP_TRANSITION].taskscnt++] = 
        TASK_BIT_1_INIT(loadDataThreadTransition, loadDataThreadTransitionCondition, &supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADROW], NULL, &supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADROW], supp->sync, pnumGet(FRAMEWORK_META(args)->pnum, LOADDATASUPP_SYNC_BIT_POS_LOADROW_COMP));
    
    *carg = (loadDataThreadCombineArgs) { .cnt = supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADROW].taskscnt, .args = supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADROW].args };
    supp->transsupp[LOADDATASUPP_TRANSSUPP_COMBINE].tasks[supp->transsupp[LOADDATASUPP_TRANSSUPP_COMBINE].taskscnt++] = 
        TASK_BIT_2_INIT(loadDataThreadProcCombineRow, /* --> */ bitTestRangeMem, carg, &supp->context, supp->sync, supp->sync, pnumGet(FRAMEWORK_META(args)->pnum, LOADDATASUPP_SYNC_BIT_POS_COMBINEROW_COMP), pnumGet(FRAMEWORK_META(args)->pnum, LOADDATASUPP_SYNC_BIT_POS_COMBINEROW_COMP));
    carg++;
    
    ///////////////////////////////////////////////////////////////////////////

    //  Scheduling the processing of the file with P-values
    //  Note: Values are being loaded into table while reading the table from disk
    
    supp->sch[LOADDATASUPP_TASKSUPP_LOADVAL] = (tblschPtr) (bitTest(context->bits, LOADDATACONTEXT_BIT_POS_LOGARITHM) ? &statSchVal : &statSchValLog);
    if (!loadDataSchedule(context->pathval, supp->sch[LOADDATASUPP_TASKSUPP_LOADVAL], &supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADVAL], &supp->context, div, FRAMEWORK_META(args)->log, sizeof(loadDataThreadReadValArgs), (readCallback) loadDataThreadReadVal, (conditionCallback) bitTestMem, supp->sync, (void *) pnumGet(FRAMEWORK_META(args)->pnum, LOADDATASUPP_SYNC_BIT_POS_COMBINEROW_SUCC))) goto ERR(Empty);

    supp->transsupp[LOADDATASUPP_TRANSSUPP_TRANSITION].tasks[supp->transsupp[LOADDATASUPP_TRANSSUPP_TRANSITION].taskscnt++] = 
        TASK_BIT_1_INIT(loadDataThreadTransition, loadDataThreadTransitionCondition, &supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADVAL], NULL, &supp->tasksupp[LOADDATASUPP_TASKSUPP_LOADVAL], supp->stat, pnumGet(FRAMEWORK_META(args)->pnum, LOADDATASUPP_STAT_BIT_POS_LOAD_COMP));
    
    ///////////////////////////////////////////////////////////////////////////

    //  Scheduling the computation of ranks for P-values and odds ratios

    if (ranks)
    {
        if (!arrayInit((void **) &supp->transsupp[LOADDATASUPP_TRANSSUPP_RANK].tasks, LOADDATASUPP_TRANS_RANK_CNT, sizeof *supp->transsupp[LOADDATASUPP_TRANSSUPP_RANK].tasks)) goto ERR();

        loadDataRanksSchedule(&supp->rargs[LOADDATASUPP_TRANS_RANKLPV_PRO >> 1], LOADDATA_META(args), LOADDATASUPP_STAT_BIT_POS_LOAD_SUCC, supp->transsupp[LOADDATASUPP_TRANSSUPP_RANK].tasks, offsetof(loadDataRes, nlpv), offsetof(loadDataRes, rnlpv), LOADDATASUPP_STAT_BIT_POS_SORTLPV_COMP, LOADDATASUPP_STAT_BIT_POS_RANKLPV_COMP, &supp->transsupp[LOADDATASUPP_TRANSSUPP_RANK].taskscnt);
        loadDataRanksSchedule(&supp->rargs[LOADDATASUPP_TRANS_RANKQAS_PRO >> 1], LOADDATA_META(args), LOADDATASUPP_STAT_BIT_POS_LOAD_SUCC, supp->transsupp[LOADDATASUPP_TRANSSUPP_RANK].tasks, offsetof(loadDataRes, qas), offsetof(loadDataRes, rqas), LOADDATASUPP_STAT_BIT_POS_SORTQAS_COMP, LOADDATASUPP_STAT_BIT_POS_RANKQAS_COMP, &supp->transsupp[LOADDATASUPP_TRANSSUPP_RANK].taskscnt);
    }
    else
    {
        bitSet2Interlocked(supp->stat, LOADDATASUPP_STAT_BIT_POS_SORTLPV_COMP);
        bitSet2Interlocked(supp->stat, LOADDATASUPP_STAT_BIT_POS_RANKLPV_COMP);
        bitSet2Interlocked(supp->stat, LOADDATASUPP_STAT_BIT_POS_SORTQAS_COMP);
        bitSet2Interlocked(supp->stat, LOADDATASUPP_STAT_BIT_POS_RANKQAS_COMP);
    }

    supp->transsupp[LOADDATASUPP_TRANSSUPP_EPILOGUE].tasks[supp->transsupp[LOADDATASUPP_TRANSSUPP_EPILOGUE].taskscnt++] = 
        TASK_BIT_2_INIT(loadDataThreadEpilogue, bitTestRangeMem, NULL, NULL, supp->stat, supp->stat, pnumGet(FRAMEWORK_META(args)->pnum, LOADDATASUPP_STAT_BIT_POS_TASK_COMP), pnumGet(FRAMEWORK_META(args)->pnum, LOADDATASUPP_STAT_BIT_POS_TASK_COMP));
    
    ///////////////////////////////////////////////////////////////////////////

    //  Enqueuing tasks

    for (size_t i = LOADDATASUPP_TRANSSUPP_CNT; i--;)
        if (!threadPoolEnqueueTasks(FRAMEWORK_META(args)->pool, supp->transsupp[i].tasks, supp->transsupp[i].taskscnt, 1)) goto ERR(Pool);

    for (size_t i = LOADDATASUPP_TASKSUPP_CNT; i--;)
        if (!threadPoolEnqueueTasks(FRAMEWORK_META(args)->pool, supp->tasksupp[i].tasks, supp->tasksupp[i].taskscnt, 1)) goto ERR(Pool);

    ///////////////////////////////////////////////////////////////////////////

    //  Error handling

    for (;;)
    {
        succ = 1;
        break;        
    
    ERR():
        logError(FRAMEWORK_META(args)->log, __FUNCTION__, errno);
        break;

    ERR(Pool):
        logMsg(FRAMEWORK_META(args)->log, "ERROR (%s): Unable to enqueue tasks!\n", __FUNCTION__);
        break;
    
    ERR(Empty):
        break;
    }

    return succ;
}

void loadDataContextDispose(loadDataContext *context)
{
    if (!context) return;
    
    free(context->pathchr);
    free(context->pathrow);
    free(context->pathval);
    free(context->pathtest);
    
    free(context);
}

bool loadDataPrologue(loadDataIn *in, loadDataOut **pout, loadDataContext *context)
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

static bool loadDataThreadClose(loadDataOut *args, void *context)
{
    (void) context;
    if (!args) return 1;
    
    bool pend = 0;
    testDataClose(&args->res);
    
    loadDataSupp *supp = &args->supp;
    
    for (size_t i = 0; i < LOADDATASUPP_TASKSUPP_CNT; i++)
    {
        if (supp->tasksupp[i].args)
        {
            // First three tasks have special argument layout which should be handled properly
            if (i < LOADDATASUPP_TASKSUPP_LOAD_DISP)
                for (size_t j = 0; j < supp->tasksupp[i].taskscnt; loadDataThreadReadArgsCloseTest(&((loadDataThreadReadArgs *) supp->tasksupp[i].args)[j++]));
            
            free(supp->tasksupp[i].args);
        }

        // Before disposing task information all pending tasks should be dropped from the queue
        if (threadPoolRemoveTasks(FRAMEWORK_META(args)->pool, supp->tasksupp[i].tasks, supp->tasksupp[i].taskscnt) && !pend)
            logMsg(FRAMEWORK_META(args)->log, "WARNING (%s): Pending tasks were dropped!\n", __FUNCTION__), pend = 1;
        
        free(supp->tasksupp[i].tasks);
    }

    for (size_t i = 0; i < LOADDATASUPP_TRANSSUPP_CNT; i++)
    {
        if (threadPoolRemoveTasks(FRAMEWORK_META(args)->pool, supp->transsupp[i].tasks, supp->transsupp[i].taskscnt) && !pend)
            logMsg(FRAMEWORK_META(args)->log, "WARNING (%s): Pending tasks were dropped!\n", __FUNCTION__), pend = 1;

        free(supp->transsupp[i].tasks);
    }
        
    return 1;
}

static bool loadDataThreadCloseCondition(loadDataSupp *supp, void *arg)
{
    (void) arg;
    
    switch (bitGet2((void *) supp->stat, LOADDATASUPP_STAT_BIT_POS_INIT_COMP))
    {
    case 0: return 0;
    case 1: return 1;
    case 3: break;
    }
    
    size_t ind = 0;

    switch (bitGet2((void *) supp->sync, LOADDATASUPP_SYNC_BIT_POS_LOADCHR_COMP))
    {
    case 0: return 0;
    case 1: ind++; break;
    case 3:
        switch (bitGet2((void *) supp->sync, LOADDATASUPP_SYNC_BIT_POS_COMBINECHR_COMP))
        {
        case 0: return 0;
        case 1: ind++;
        case 3: break;
        }
    }

    switch (bitGet2((void *) supp->sync, LOADDATASUPP_SYNC_BIT_POS_LOADTEST_COMP))
    {
    case 0: return 0;
    case 1: ind++; break;
    case 3: 
        switch (bitGet2((void *) supp->sync, LOADDATASUPP_SYNC_BIT_POS_COMBINETEST_COMP))
        {
        case 0: return 0;
        case 1: ind++;
        case 3: break;
        }
    }
    
    switch (bitGet2((void *) supp->sync, LOADDATASUPP_SYNC_BIT_POS_LOADROW_COMP))
    {
    case 0: return 0;
    case 1: ind++;
    case 3: break;
    }
    
    if (ind) return 1;

    switch (bitGet2((void *) supp->sync, LOADDATASUPP_SYNC_BIT_POS_COMBINEROW_COMP))
    {
    case 0: return 0;
    case 1: return 1;
    case 3: break;
    }

    switch (bitGet2((void *) supp->stat, LOADDATASUPP_STAT_BIT_POS_LOAD_COMP))
    {
    case 0: return 0;
    case 1: return 1;
    case 3: break;
    }

    if (supp->holdload) return 0;

    ind = 0;

    switch (bitGet2((void *) supp->stat, LOADDATASUPP_STAT_BIT_POS_SORTLPV_COMP))
    {
    case 0: return 0;
    case 1: ind++;
    case 3: break;
    }

    switch (bitGet2((void *) supp->stat, LOADDATASUPP_STAT_BIT_POS_SORTQAS_COMP))
    {
    case 0: return 0;
    case 1: ind++;
    case 3: break;
    }

    if (ind) return 1;

    ind = 0;

    switch (bitGet2((void *) supp->stat, LOADDATASUPP_STAT_BIT_POS_RANKLPV_COMP))
    {
    case 0: return 0;
    case 1: ind++;
    case 3: break;
    }

    switch (bitGet2((void *) supp->stat, LOADDATASUPP_STAT_BIT_POS_RANKQAS_COMP))
    {
    case 0: return 0;
    case 1: ind++;
    case 3: break;
    }

    if (ind) return 1;

    switch (bitGet2((void *) supp->stat, LOADDATASUPP_STAT_BIT_POS_TASK_COMP))
    {
    case 0: return 0;
    case 1: return 1; // Never happens
    case 3: break;
    }

    return !(supp->hold || supp->holdload);
}

bool loadDataEpilogue(loadDataIn *in, loadDataOut* out, void *context)
{
    (void) context;
    
    if (!out) return 0;
    
    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();
    
    *tsk = (task)
    {
        .arg = out,
        .callback = (taskCallback) loadDataThreadClose,
        .cond = (conditionCallback) loadDataThreadCloseCondition,
        .condmem = &out->supp
    };
    
    return 1;
    
ERR():
    logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);
    return 0;
}

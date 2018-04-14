#include "Common.h"
#include "Genotypes.h"
#include "TaskMacros.h"
#include "TableProc.h"
#include "Sort.h"

enum chr {
    CHR_UNDEFINED = 0,
    CHR_X = 23,
    CHR_Y = 24
};


bool chrHandler(const char *str, size_t len, uint16_t *ptr, handlerContext *context)
{
    (void) len;

    char *test;
    *ptr = (uint16_t) strtoul(str, &test, 10);
    
    if (*test)
    {
        if (!strcmpci(str, "X")) *ptr = 23;
        else if (!strcmpci(str, "Y")) *ptr = 24;
        else return 0;
    }    

    if (context) bitSet((unsigned char *) ptr + context->set, context->pos);
    return 1;
}

static const tblsch statSchBim = CLII((tblcolsch[])
{
    { .handler = { .read = (readHandlerCallback) chrHandler }, .ind = 0, .size = sizeof(uint16_t) }, // genotypesRes::chr_len
    { .handler = { .read = (readHandlerCallback) strTableHandler }, .ind = 1, .size = sizeof(ptrdiff_t) }, // genotypesRes::snpname
    { .handler = { .read = NULL } }, // dinstance in Morgans
    { .handler = { .read = (readHandlerCallback) uint32Handler }, .ind = 2, .size = sizeof(uint32_t) }, // genotypesRes::pos
    { .handler = { .read = NULL } }, // allele Major
    { .handler = { .read = NULL } } // allele Minor
});


bool read_bim(genotypesRes *res, genotypesContext *context)
{
    FILE *f = fopen(context->path_bim, "rb");
    // if (!f) ...
    
    fseek64(f, 0, SEEK_END);
    size_t sz = ftell64(f);

    size_t row_cnt = rowCount(f, 0, sz);

    void *tbl[3];
    tblInit((void **) &tbl, (tblsch *) &statSchBim, row_cnt, 1);

    char *str = NULL;
    size_t strtblcnt = 0, strtblcap = 0;
    void *cont[] = { [1] = &(strTableHandlerContext) { .strtbl = &str, .strtblcnt = &strtblcnt, .strtblcap = &strtblcap }, [4] = NULL };
    
    fseek64(f, 0, SEEK_SET);
    rowRead(f, (tblsch *) &statSchBim, tbl, cont, 0, 0, 0, NULL, '\t');

    uint8_t chr_bits[BYTE_CNT(25)] = { 0 };
    for (size_t i = 0; i < row_cnt; i++)
    {
        uint16_t ind = ((uint16_t *) (tbl[0]))[i];
        if (ind < 25) bitSet(chr_bits, ind);
    }

    size_t chr_cnt = 25;
    for (size_t i = 25; i; i--) if (!bitTest(chr_bits, i - 1)) chr_cnt--;
    
    res->snp_cnt = row_cnt;
    res->chr_cnt = chr_cnt;
    res->chr_len = calloc(chr_cnt, sizeof(*res->chr_len));
    
    for (size_t i = 0; i < row_cnt; i++)
    {
        uint16_t ind = ((uint16_t *) (tbl[0]))[i];
        if (ind < chr_cnt) res->chr_len[ind]++;
    }

    res->pos = tbl[2];
    res->snp_name_off = tbl[1];
    res->snp_name = str;
    res->snp_name_sz = strtblcnt;
    
    free(tbl[0]);
    fclose(f);
    return 1;
}

static const tblsch statSchFam = CLII((tblcolsch[])
{
    { .handler = { .read = NULL } },
    { .handler = { .read = NULL } },
    { .handler = { .read = NULL } },
    { .handler = { .read = NULL } },
    { .handler = { .read = NULL } },
    { .handler = { .read = (readHandlerCallback) strTableHandler }, .ind = 0, .size = sizeof(ptrdiff_t) } // phenotype
});

int8_t uint16Comp(const uint16_t *a, const uint16_t *b, void *context)
{
    (void) context;
    return *a > *b ? 1 : *a == *b ? 0 : -1;
}

bool read_fam(genotypesRes *res, genotypesContext *context)
{
    FILE *f = fopen(context->path_fam, "rb");
    // if (!f) ...
    
    fseek64(f, 0, SEEK_END);
    size_t sz = ftell64(f);

    size_t row_cnt = rowCount(f, 0, sz);

    void *tbl[1];
    tblInit((void **) &tbl, (tblsch *) &statSchFam, row_cnt, 1);

    char *str = NULL;
    size_t strtblcnt = 0, strtblcap = 0;
    void *cont[6] = { [0] = &(strTableHandlerContext) { .strtbl = &str, .strtblcnt = &strtblcnt, .strtblcap = &strtblcap } };

    fseek64(f, 0, SEEK_SET);
    bool r = rowRead(f, (tblsch *) &statSchFam, tbl, cont, 0, 0, 0, NULL, ' ');
    
    res->phn_off = tbl[0];
    res->phn_cnt = row_cnt;
    res->phn_name = str;
    res->phn_name_sz = strtblcnt;
    
    //ordersStableUnique(tbl[0], row_cnt, sizeof(uint16_t), uint16Comp, NULL, &res->phn_uni);

    return 1;
}

union gen {
    uint8_t bin;
    struct {
        uint8_t n0 : 2;
        uint8_t n1 : 2;
        uint8_t n2 : 2;
        uint8_t n3 : 2;
    };
};

bool read_bed(genotypesRes *res, genotypesContext *context)
{
    char remap[] = { 0, 3, 1, 2 };    
    FILE *f = fopen(context->path_bed, "rb");
    char buff[BLOCK_READ];
    size_t snp_ind = 0, phn_ind = 0;;

    size_t rd = fread(buff, sizeof(*buff), countof(buff), f), pos = 0;
    if (strncmp(buff, "\x6c\x1b\x01", 3)) return 0;
    pos = 3;

    res->gen = malloc(res->snp_cnt * (res->phn_cnt + 3) / 4);
    
    for (; rd; rd = fread(buff, sizeof(*buff), countof(buff), f), pos = 0)
    {
        for (size_t i = pos; i < rd; i++)
        {
            union gen gen_in = { .bin = buff[i] }, gen_out = { .n0 = remap[gen_in.n0], .n1 = remap[gen_in.n1], .n2 = remap[gen_in.n2], .n3 = remap[gen_in.n3] };
            res->gen[res->snp_cnt * (res->phn_cnt + 3) / 4 + phn_ind / 4] = gen_out.bin;
            phn_ind += 4;
            if (phn_ind >= res->phn_cnt) phn_ind = 0, snp_ind++;
            if (snp_ind == res->snp_cnt) return 0; // error msg
        }
    }

    return 1;
}


static void genotypesResClose(genotypesRes *res)
{
    free(res->gen);
}

static bool genotypesThreadProc(loopMTArg *args, genotypesThreadProcContext *context)
{
    FILE *f = fopen(context->context->path_bed, "rb");
    if (!f) goto ERR();

    if (!fseek64(f, args->offset, SEEK_SET) && fread(context->out->res.gen + args->offset, 1, args->length, f) == args->length)
    {
        logMsg(FRAMEWORK_META(context->out)->log, "INFO (%s): Thread %zu have read %zu bytes of the file %s + %zu B.\n", __FUNCTION__, threadPoolFetchThredId(FRAMEWORK_META(context->out)->pool), args->length, context->context->path_bed, args->offset);
    }    
    else
    {
        logMsg(FRAMEWORK_META(context->out)->log, "ERROR (%s): Thread %zu cannot read the file %s + %zu B!\n", __FUNCTION__, threadPoolFetchThredId(FRAMEWORK_META(context->out)->pool), context->context->path_bed, args->offset);
        fclose(f);
        return 0;
    }

    fclose(f);
    return 1;
    
ERR() :
    logError(FRAMEWORK_META(context->out)->log, __FUNCTION__, errno);
    return 0;
}

static bool genotypesThreadPrologue(genotypesOut *args, genotypesContext *context)
{
    const char *strings[] =
    {
        __FUNCTION__,
        "ERROR (%s): Cannot open specified file \"%s\". %s!\n",
        "ERROR (%s): Unable to setup parallel loop!\n"
    };

    enum { STR_FN, STR_FR_EI, STR_FR_LP };
    
    read_bim(&args->res, context);
    read_fam(&args->res, context);
    read_bed(&args->res, context);

    /*
    char tempbuff[TEMP_BUFF] = { '\0' };
    FILE *f = NULL;
        
    f = fopen(context->path_bed, "rb");
    if (!f) goto ERR(File);
    
    fseek64(f, 0, SEEK_END);
    size_t sz = ftell64(f);
    
    args->supp.sync = (loopMTSync) {
        .asucc = (aggregatorCallback) bitSet2InterlockedMem,
        .afail = (aggregatorCallback) bitSetInterlockedMem,
        .asuccmem = args->supp.stat, 
        .afailmem = args->supp.stat,
        .asuccarg = pnumGet(FRAMEWORK_META(args)->pnum, GENOTYPES_STAT_BIT_POS_TASK_COMP),
        .afailarg = pnumGet(FRAMEWORK_META(args)->pnum, GENOTYPES_STAT_BIT_POS_TASK_COMP),
    };

    args->supp.context = (genotypesThreadProcContext) { .out = args, .context = context };
    
    args->res.gen_cnt = sz;

    args->res.gen = malloc(sz);
    if (sz && !args->res.gen) goto ERR();

    args->supp.lmt = loopMTCreate((loopMTCallback) genotypesThreadProc, 0, sz, &args->supp.context, FRAMEWORK_META(args)->pool, &args->supp.sync);
    if (!args->supp.lmt) goto ERR(Loop);

    fclose(f);
    */
    for (;;)
    {
        return 1;
    
    ERR():
        logError(FRAMEWORK_META(args)->log, __FUNCTION__, errno);
        break;

    ERR(File):
        //strerror_s(tempbuff, sizeof tempbuff, errno);
        //logMsg(FRAMEWORK_META(args)->log, strings[STR_FR_EI], strings[STR_FN], context->path_bed, tempbuff);
        break;

    ERR(Loop):
        logMsg(FRAMEWORK_META(args)->log, strings[STR_FR_LP], strings[STR_FN]);
        break;
    }

    //if (f) fclose(f);
    return 0;    
}

void genotypesContextDispose(genotypesContext *context)
{
    if (!context) return;

    free(context->path_bim);
    free(context->path_fam);
    free(context->path_bed);
    free(context);
}

bool genotypesPrologue(genotypesIn *in, genotypesOut **pout, genotypesContext *context)
{
    genotypesOut *out = *pout = malloc(sizeof *out);
    if (!out) goto ERR();
        
    *out = (genotypesOut) { .meta = GENOTYPES_META_INIT(in, out, context) };
    if (!outInfoSetNext(FRAMEWORK_META(in)->out, out)) goto ERR();

    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();

    if (!pnumTest(FRAMEWORK_META(out)->pnum, GENOTYPES_STAT_BIT_CNT)) goto ERR();
    *tsk = TASK_BIT_1_INIT(genotypesThreadPrologue, NULL, out, context, NULL, out->supp.stat, pnumGet(FRAMEWORK_META(out)->pnum, GENOTYPES_STAT_BIT_POS_INIT_COMP));
    return 1;

ERR() :
    logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);
    return 0;
}

static bool genotypesThreadClose(genotypesOut *args, void *context)
{
    (void) context;
    if (!args) return 1;

    genotypesResClose(&args->res);
    loopMTDispose(args->supp.lmt);
    return 1;
}

static bool genotypesThreadCloseCondition(genotypesSupp *supp, void *arg)
{
    (void) arg;

    switch (bitGet2((void *) supp->stat, GENOTYPES_STAT_BIT_POS_INIT_COMP))
    {
    case 0: return 0;
    case 1: return 1;
    case 3: break;
    }

    switch (bitGet2((void *) supp->stat, GENOTYPES_STAT_BIT_POS_TASK_COMP))
    {
    case 0: return 0;
    case 1: return 1;
    case 3: break;
    }

    return !(supp->hold);
}

bool genotypesEpilogue(genotypesIn *in, genotypesOut* out, void *context)
{
    if (!out) return 0;
    (void) context;

    task *tsk = tasksInfoNextTask(FRAMEWORK_META(in)->tasks);
    if (!tsk) goto ERR();

    *tsk = (task)
    {
        .arg = out,
        .callback = (taskCallback) genotypesThreadClose,
        .cond = (conditionCallback) genotypesThreadCloseCondition,
        .condmem = &out->supp
    };

    return 1;

ERR():
    logError(FRAMEWORK_META(in)->log, __FUNCTION__, errno);
    return 0;
}

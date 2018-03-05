#pragma once

#include "DataLayout.h"
#include "Framework.h"
#include "SortMT.h"
#include "TableProc.h"

enum {
    LOADDATACONTEXT_BIT_POS_HEADER = 0,
    LOADDATACONTEXT_BIT_POS_NORANKS,
    LOADDATACONTEXT_BIT_POS_LOGARITHM,
    LOADDATACONTEXT_BIT_CNT
};

enum {
    LOADDATASUPP_STAT_BIT_POS_INIT_COMP = 0, // completeness flag
    LOADDATASUPP_STAT_BIT_POS_INIT_SUCC, // success flag
    LOADDATASUPP_STAT_BIT_POS_LOAD_COMP,
    LOADDATASUPP_STAT_BIT_POS_LOAD_SUCC,
    LOADDATASUPP_STAT_BIT_POS_SORTLPV_COMP,
    LOADDATASUPP_STAT_BIT_POS_SORTLPV_SUCC,
    LOADDATASUPP_STAT_BIT_POS_RANKLPV_COMP,
    LOADDATASUPP_STAT_BIT_POS_RANKLPV_SUCC,
    LOADDATASUPP_STAT_BIT_POS_SORTQAS_COMP,
    LOADDATASUPP_STAT_BIT_POS_SORTQAS_SUCC,
    LOADDATASUPP_STAT_BIT_POS_RANKQAS_COMP,
    LOADDATASUPP_STAT_BIT_POS_RANKQAS_SUCC,
    LOADDATASUPP_STAT_BIT_POS_TASK_COMP,
    LOADDATASUPP_STAT_BIT_POS_TASK_SUCC,
    LOADDATASUPP_STAT_BIT_CNT
};

enum {
    LOADDATASUPP_SYNC_BIT_POS_LOADCHR_COMP,
    LOADDATASUPP_SYNC_BIT_POS_LOADCHR_SUCC,
    LOADDATASUPP_SYNC_BIT_POS_COMBINECHR_COMP,
    LOADDATASUPP_SYNC_BIT_POS_COMBINECHR_SUCC,
    LOADDATASUPP_SYNC_BIT_POS_LOADTEST_COMP,
    LOADDATASUPP_SYNC_BIT_POS_LOADTEST_SUCC,
    LOADDATASUPP_SYNC_BIT_POS_COMBINETEST_COMP,
    LOADDATASUPP_SYNC_BIT_POS_COMBINETEST_SUCC,
    LOADDATASUPP_SYNC_BIT_POS_LOADROW_COMP,
    LOADDATASUPP_SYNC_BIT_POS_LOADROW_SUCC,
    LOADDATASUPP_SYNC_BIT_POS_COMBINEROW_COMP,
    LOADDATASUPP_SYNC_BIT_POS_COMBINEROW_SUCC,
    LOADDATASUPP_SYNC_BIT_CNT
};

enum {
    LOADDATASUPP_TASKSUPP_LOADCHR = 0,
    LOADDATASUPP_TASKSUPP_LOADTEST,
    LOADDATASUPP_TASKSUPP_LOADROW,
    LOADDATASUPP_TASKSUPP_LOAD_DISP,
    LOADDATASUPP_TASKSUPP_LOADVAL = LOADDATASUPP_TASKSUPP_LOAD_DISP, // Loading the values is not an ordinary loading task
    LOADDATASUPP_TASKSUPP_CNT,
};

enum {
    LOADDATASUPP_TRANSSUPP_TRANSITION,
    LOADDATASUPP_TRANSSUPP_COMBINE,
    LOADDATASUPP_TRANSSUPP_RANK,
    LOADDATASUPP_TRANSSUPP_EPILOGUE,
    LOADDATASUPP_TRANSSUPP_CNT
};

enum {
    LOADDATASUPP_TASKS_LOADCHR = 0,
    LOADDATASUPP_TASKS_LOADTEST,
    LOADDATASUPP_TASKS_LOADROW,
    LOADDATASUPP_TASKS_LOADVAL,
    LOADDATASUPP_TASKS_LOAD_CNT
};

enum {
    LOADDATASUPP_TRANS_TRANSITIONCHR = 0,
    LOADDATASUPP_TRANS_TRANSITIONTEST,
    LOADDATASUPP_TRANS_TRANSITIONROW,
    LOADDATASUPP_TRANS_TRANSITIONVAL,
    LOADDATASUPP_TRANS_TRANSITION_CNT
};

enum {
    LOADDATASUPP_TRANS_COMBINECHR = 0,
    LOADDATASUPP_TRANS_COMBINETEST,
    LOADDATASUPP_TRANS_COMBINEROW,
    LOADDATASUPP_TRANS_COMBINE_CNT
};

enum {
    LOADDATASUPP_TRANS_RANKLPV_PRO = 0,
    LOADDATASUPP_TRANS_RANKLPV_EPI,
    LOADDATASUPP_TRANS_RANKQAS_PRO,
    LOADDATASUPP_TRANS_RANKQAS_EPI,
    LOADDATASUPP_TRANS_RANK_CNT
};

enum {
    LOADDATASUPP_TRANS_EPILOGUE = 0,
    LOADDATASUPP_TRANS_EPILOGUE_CNT
};

typedef struct loadDataThreadContext loadDataThreadContext;

typedef struct {
    ptrdiff_t val, rval;
    size_t sortbit, rankbit, loadbit;
    sortMT *smt;
    uint64_t initime;
} loadDataThreadRanksArgs;

typedef struct loadDataThreadReadArgs loadDataThreadReadArgs;

typedef struct {
    size_t cnt;
    loadDataThreadReadArgs *args;
} loadDataThreadCombineArgs;

typedef struct loadDataOut loadDataOut;
typedef struct loadDataContext loadDataContext;

struct loadDataThreadContext
{
    loadDataOut *out;
    loadDataContext *context;
};

typedef struct {
    struct {
        task *tasks;
        size_t taskscnt;
    };
    
    void *args;
    size_t succ, fail;
} taskSupp;

typedef struct
{
    struct {
        task *tasks;
        size_t taskscnt;
    };
} transitionTaskSupp;

typedef struct {
    taskSupp tasksupp[LOADDATASUPP_TASKSUPP_CNT];
    transitionTaskSupp transsupp[LOADDATASUPP_TRANSSUPP_CNT];
    tblschPtr sch[LOADDATASUPP_TASKSUPP_CNT];
    loadDataThreadContext context;
    volatile uint8_t stat[BYTE_CNT(LOADDATASUPP_STAT_BIT_CNT)];
    volatile uint8_t sync[BYTE_CNT(LOADDATASUPP_SYNC_BIT_CNT)];
    volatile size_t holdload, hold;
    loadDataThreadCombineArgs cargs[LOADDATASUPP_TRANS_COMBINE_CNT];
    loadDataThreadRanksArgs rargs[LOADDATASUPP_TRANS_RANK_CNT >> 1];
} loadDataSupp;

struct loadDataContext
{
    char *pathchr, *pathtest, *pathrow, *pathval;
    uint8_t bits[BYTE_CNT(LOADDATACONTEXT_BIT_CNT)];
};

typedef frameworkOut loadDataIn, *loadDataInPtr;
typedef testData loadDataRes, *loadDataResPtr;

typedef struct {
    frameworkMetadata meta;
    loadDataResPtr res;
    volatile uint8_t *stat;
    volatile size_t *holdload, *hold;
} loadDataMetadata;

#define LOADDATA_META(IN) ((loadDataMetadata *) (IN))
#define LOADDATA_META_INIT(IN, OUT, CONTEXT) { .meta = *FRAMEWORK_META(IN), .res = &(OUT)->res, .stat = (OUT)->supp.stat, .holdload = &(OUT)->supp.holdload, .hold = &(OUT)->supp.hold }

struct loadDataOut
{
    loadDataMetadata meta;
    loadDataRes res;
    
    // All synchronization data goes here
    loadDataSupp supp;
};

void loadDataContextDispose(loadDataContext *);
bool loadDataPrologue(loadDataIn *, loadDataOut **, loadDataContext *);
bool loadDataEpilogue(loadDataIn *, loadDataOut *, void *);

bool loadDataThreadRanksPrologue(loadDataThreadRanksArgs *, loadDataMetadata *);
bool loadDataThreadRanksEpilogue(loadDataThreadRanksArgs *, loadDataMetadata *);

void loadDataRanksSchedule(loadDataThreadRanksArgs *, loadDataMetadata *, size_t, task *, ptrdiff_t, ptrdiff_t, size_t, size_t, size_t *);

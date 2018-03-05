#pragma once

#include "Common.h"
#include "DataLayout.h"
#include "Framework.h"
#include "LoadData.h"

enum
{
    MYSQLFETCHCONTEXT_BIT_POS_NORANKS,
    MYSQLFETCHCONTEXT_BIT_CNT
};

enum
{
    MYSQLFETCHSUPP_TASKS_RANKLPV_PRO = 0,
    MYSQLFETCHSUPP_TASKS_RANKLPV_EPI,
    MYSQLFETCHSUPP_TASKS_RANKQAS_PRO,
    MYSQLFETCHSUPP_TASKS_RANKQAS_EPI,
    MYSQLFETCHSUPP_TASKS_RANK_CNT
};

enum
{
    MYSQLFETCHSUPP_STAT_BIT_POS_INIT_COMP = 0,
    MYSQLFETCHSUPP_STAT_BIT_POS_INIT_SUCC,
    MYSQLFETCHSUPP_STAT_BIT_POS_LOAD_COMP,
    MYSQLFETCHSUPP_STAT_BIT_POS_LOAD_SUCC,
    MYSQLFETCHSUPP_STAT_BIT_POS_SORTLPV_COMP,
    MYSQLFETCHSUPP_STAT_BIT_POS_SORTLPV_SUCC,
    MYSQLFETCHSUPP_STAT_BIT_POS_RANKLPV_COMP,
    MYSQLFETCHSUPP_STAT_BIT_POS_RANKLPV_SUCC,
    MYSQLFETCHSUPP_STAT_BIT_POS_SORTQAS_COMP,
    MYSQLFETCHSUPP_STAT_BIT_POS_SORTQAS_SUCC,
    MYSQLFETCHSUPP_STAT_BIT_POS_RANKQAS_COMP,
    MYSQLFETCHSUPP_STAT_BIT_POS_RANKQAS_SUCC,
    MYSQLFETCHSUPP_STAT_BIT_POS_TASK_COMP,
    MYSQLFETCHSUPP_STAT_BIT_POS_TASK_SUCC,
    MYSQLFETCHSUPP_STAT_BIT_CNT
};

typedef struct mysqlFetchThreadProcArgs mysqlFetchThreadProcArgs;
typedef struct mysqlFetchThreadProcContext mysqlFetchThreadProcContext;
typedef loadDataThreadRanksArgs mysqlFetchThreadRanksArgs;

typedef struct
{
    // Status bits array
    volatile uint8_t stat[BYTE_CNT(MYSQLFETCHSUPP_STAT_BIT_CNT)];
    
    volatile size_t succ, fail;
    size_t thresh;

    // Cleanup hold on counters prevent resources to being disposed
    // 'holdload' -- holds at least chromosome data (do not require rank computation to complete successfully)
    // 'hold' -- holds all the data also including ranks
    volatile size_t holdload, hold;

    mysqlFetchThreadRanksArgs rargs[MYSQLFETCHSUPP_TASKS_RANK_CNT >> 1];
    
    struct
    {
        task *tasks;
        size_t taskscnt;
    };

    mysqlFetchThreadProcArgs *args;
    mysqlFetchThreadProcContext *context;
} mysqlFetchSupp;

typedef struct
{
    char *schema, *host, *login, *password;
    uint32_t port;
    uint8_t bits[BYTE_CNT(MYSQLFETCHCONTEXT_BIT_CNT)];
} mysqlFetchContext;

typedef frameworkMetadata mysqlFetchIn;
typedef loadDataRes mysqlFetchRes, *mysqlFetchResPtr;
typedef loadDataMetadata mysqlFetchMetadata;

#define MYSQLFETCH_META(IN) LOADDATA_META(IN)
#define MYSQLFETCH_META_INIT(IN, OUT, CONTEXT) LOADDATA_META_INIT(IN, OUT, CONTEXT)

typedef struct
{
    mysqlFetchMetadata meta;
    mysqlFetchRes res;

    // All synchronization data goes here
    mysqlFetchSupp supp;
} mysqlFetchOut;

void mysqlFetchContextDispose(mysqlFetchContext *);
bool mysqlFetchPrologue(mysqlFetchIn *, mysqlFetchOut **, mysqlFetchContext *);
bool mysqlFetchEpilogue(mysqlFetchIn *, mysqlFetchOut *, void *);

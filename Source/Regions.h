#pragma once

#include "Density.h"

enum
{
    REGIONSSUPP_STAT_BIT_POS_INIT_COMP = 0,
    REGIONSSUPP_STAT_BIT_POS_INIT_SUCC,
    REGIONSSUPP_STAT_BIT_POS_TASK_COMP,
    REGIONSSUPP_STAT_BIT_POS_TASK_SUCC,
    REGIONSSUPP_STAT_BIT_CNT,
};

enum
{
    REGIONSCONTEXT_BIT_POS_DECAY = 0,
    REGIONSCONTEXT_BIT_POS_DEPTH,
    REGIONSCONTEXT_BIT_POS_LENGTH,
    REGIONSCONTEXT_BIT_POS_THRESHOLD,
    REGIONSCONTEXT_BIT_POS_TOLERANCE,
    REGIONSCONTEXT_BIT_POS_SLOPE,
    REGIONSCONTEXT_BIT_CNT,
};

typedef struct regionsBackendArgs regionsBackendArgs;
typedef struct regionsBackendContext regionsBackendContext;

typedef struct 
{
    size_t chr, test;
} regionsThreadProcArgs;

typedef struct regionsOut regionsOut;
typedef struct regionsContext regionsContext;

typedef struct 
{
    regionsOut *out;
    regionsContext *context;
} regionsThreadProcContext;

typedef struct region region, *regionPtr;

struct region
{
    size_t l, r, max;
    double level;
    regionPtr anc;
};

typedef struct
{
    size_t l, r;
    struct
    {
        regionPtr *regions;
        size_t regionscnt;
    };
} interval;

typedef struct
{
    struct
    {
        region *reg;
        size_t *reglev, levcnt, cnt;
    };   
} regionsRes, *regionsResPtr;

typedef struct
{
    region **reg; // intermediate output (levels -> regions)
    size_t *reglev; // level counts
    size_t reglevcnt;
} regionsIntermediate;

typedef struct
{
    volatile uint8_t stat[BYTE_CNT(REGIONSSUPP_STAT_BIT_CNT)];
    volatile size_t succ, fail;
    size_t thresh, hold;

    struct
    {
        task *tasks;
        size_t taskscnt;
    };

    regionsThreadProcArgs *args;
    regionsThreadProcContext context;

    regionsIntermediate *intermediate; // by chromosome 
} regionsSupp;

struct regionsContext
{
    double threshold, tolerance, slope;
    uint32_t decay, depth, length; // decay of the region; depth of region tree; minimal length of region
    uint8_t bits[BYTE_CNT(REGIONSCONTEXT_BIT_CNT)];
};

typedef struct
{
    densityMetadata meta;
    regionsResPtr res;
    volatile uint8_t *stat;
    volatile size_t *hold;
} regionsMetadata;

#define REGIONS_META(IN) ((reportMetadata *) (IN))
#define REGIONS_META_INIT(IN, OUT, CONTEXT) { .meta = *DENSITY_META(IN), .res = &(OUT)->res, .stat = (OUT)->supp.stat, .hold = &(OUT)->supp.hold }

typedef densityMetadata regionsIn;

struct regionsOut
{
    regionsMetadata meta;
    regionsRes res;
    regionsSupp supp;
};

void regionsContextDispose(regionsContext *);
bool regionsPrologue(regionsIn *, regionsOut **, regionsContext *);
bool regionsEpilogue(regionsIn *, regionsOut *, void *);
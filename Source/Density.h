#pragma once

#include "LoadData.h"

typedef enum {
    DENSITY_TYPE_UNKNOWN = 0,
    DENSITY_TYPE_AVERAGE,
    DENSITY_TYPE_GAUSSIAN
} densityType;

enum {
    DENSITYCONTEXT_BIT_POS_RADIUS = 0,
    DENSITYCONTEXT_BIT_POS_SMOOTH,
    DENSITYCONTEXT_BIT_POS_POS,
    DENSITYCONTEXT_BIT_CNT
};

enum {
    DENSITYSUPP_STAT_BIT_POS_INIT_COMP = 0,
    DENSITYSUPP_STAT_BIT_POS_INIT_SUCC,
    DENSITYSUPP_STAT_BIT_POS_DENSITY_COMP,
    DENSITYSUPP_STAT_BIT_POS_DENSITY_SUCC,
    DENSITYSUPP_STAT_BIT_POS_SORTDNS_COMP,
    DENSITYSUPP_STAT_BIT_POS_SORTDNS_SUCC,
    DENSITYSUPP_STAT_BIT_POS_RANKDNS_COMP,
    DENSITYSUPP_STAT_BIT_POS_RANKDNS_SUCC,
    DENSITYSUPP_STAT_BIT_POS_SORTLPV_COMP,
    DENSITYSUPP_STAT_BIT_POS_SORTLPV_SUCC,
    DENSITYSUPP_STAT_BIT_POS_RANKLPV_COMP,
    DENSITYSUPP_STAT_BIT_POS_RANKLPV_SUCC,
    DENSITYSUPP_STAT_BIT_POS_TASK_COMP,
    DENSITYSUPP_STAT_BIT_POS_TASK_SUCC,
    DENSITYSUPP_STAT_BIT_CNT,
};

typedef struct {
    ptrdiff_t val, rval;
    size_t sortbit, rankbit, loadbit;
    sortMT *smt;
    uint64_t initime;
} densityThreadRanksArgs;

typedef struct densityCallbackArgs densityCallbackArgs;
typedef struct densityCallbackContext densityCallbackContext;

typedef struct {
    size_t *li, *ri, *lc, *rc; // Left and right index, Left and right count
    uintptr_t *rdns, *rlpv;
    double *dns, *lpv;
} densityRes, *densityResPtr;

typedef struct {
    volatile uint8_t *ldstat;
    volatile uint8_t stat[BYTE_CNT(DENSITYSUPP_STAT_BIT_CNT)];
    densityThreadRanksArgs rargs[2];
    volatile size_t succ, fail, hold;
    size_t thresh;

    struct {
        task *tasks;
        size_t taskscnt;
    };
        
    densityCallbackArgs *args;
    densityCallbackContext *context;
} densitySupp;

typedef struct {
    uint32_t radius;
    double smooth;
    densityType type;
    uint8_t bits[BYTE_CNT(DENSITYCONTEXT_BIT_CNT)];
} densityContext, *densityContextPtr;

typedef struct {
    loadDataMetadata meta;
    densityResPtr res;
    densityContextPtr context;
    volatile uint8_t *stat;
    volatile size_t *hold;
} densityMetadata;

#define DENSITY_META(IN) ((densityMetadata *) (IN))
#define DENSITY_META_INIT(IN, OUT, CONTEXT) { .meta = *LOADDATA_META(IN), .res = &(OUT)->res, .context = CONTEXT, .stat = (OUT)->supp.stat, .hold = &(OUT)->supp.hold }

typedef loadDataMetadata densityIn;

typedef struct {
    densityMetadata meta;
    densityRes res;
    densitySupp supp;
} densityOut;

bool densityTypeHandler(const char *, size_t, densityType *, void *);
void densityContextDispose(densityContext *);
bool densityPrologue(densityIn *, densityOut **, densityContext *);
bool densityEpilogue(densityIn *, densityOut *, void *);

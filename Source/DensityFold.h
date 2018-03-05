#pragma once

#include "Density.h"

typedef enum {
    DENSITYFOLD_TYPE_UNKNOWN = 0,
    DENSITYFOLD_TYPE_AVERAGE,
    DENSITYFOLD_TYPE_MAXIMUM
} densityFoldType;

enum {
    DENSITYFOLDSUPP_STAT_BIT_POS_TASK_COMP = 0,
    DENSITYFOLDSUPP_STAT_BIT_POS_TASK_SUCC,
    DENSITYFOLDSUPP_STAT_BIT_CNT,
};

typedef struct {
    struct {
        size_t *tmap;
        size_t tmapcnt;
    };
    double *fdns;
    uintptr_t *rfdns;
} densityFoldRes, *densityFoldResPtr;

typedef struct
{
    volatile uint8_t stat[BYTE_CNT(DENSITYFOLDSUPP_STAT_BIT_CNT)];
    volatile size_t hold;
} densityFoldSupp;

typedef struct
{
    densityFoldType type;
    char *group;
} densityFoldContext, *densityFoldContextPtr;

typedef struct
{
    densityMetadata meta;
    densityFoldResPtr res;
    densityFoldContextPtr context;
    volatile uint8_t *stat;
    volatile size_t *hold;
} densityFoldMetadata;

#define DENSITYFOLD_META(IN) ((densityFoldMetadata *) (IN))
#define DENSITYFOLD_META_INIT(IN, OUT, CONTEXT) { .meta = *DENSITY_META(IN), .res = &(OUT)->res, .context = CONTEXT, .stat = (OUT)->supp.stat, .hold = &(OUT)->supp.hold }

typedef densityMetadata densityFoldIn;

typedef struct {
    densityFoldMetadata meta;
    densityFoldRes res;
    densityFoldSupp supp;
} densityFoldOut;

bool densityFoldHandler(const char *, size_t, densityFoldType *, void *);
void densityFoldContextDispose(densityFoldContext *);
bool densityFoldPrologue(densityFoldIn *in, densityFoldOut **out, densityFoldContext *);
bool densityFoldEpilogue(densityFoldIn *, densityFoldOut *out, void *context);

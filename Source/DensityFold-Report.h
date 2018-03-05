#pragma once

#include "DensityFold.h"

typedef enum {
    DFREPORT_TYPE_UNKNOWN = 0,
    DFREPORT_TYPE_DENSITY,
    DFREPORT_TYPE_NLPV,
    DFREPORT_TYPE_QAS,
    DFREPORT_TYPE_PLOT,
} dfReportType;

enum {
    DFREPORTCONTEXT_BIT_POS_THRESHOLD = 0,
    DFREPORTCONTEXT_BIT_POS_LIMIT,
    DFREPORTCONTEXT_BIT_POS_HEADER,
    DFREPORTCONTEXT_BIT_POS_SEMICOLON,
    DFREPORTCONTEXT_BIT_CNT,
};

enum {
    DFREPORTSUPP_STAT_BIT_POS_TASK_COMP = 0,
    DFREPORTSUPP_STAT_BIT_POS_TASK_SUCC,
    DFREPORTSUPP_STAT_BIT_CNT,
};

typedef struct {
    volatile uint8_t *ldstat, *dfstat;
    volatile uint8_t stat[BYTE_CNT(DFREPORTSUPP_STAT_BIT_CNT)];
    
    struct {
        task *tasks;
        size_t taskscnt;
    };
} dfReportSupp;

typedef struct {
    dfReportType type;
    char *path;    
    double threshold;
    uint32_t limit;
    uint8_t bits[BYTE_CNT(DFREPORTCONTEXT_BIT_CNT)];
} dfReportContext;

typedef struct {
    densityFoldMetadata meta;
} dfReportMetadata;

#define DFREPORT_META(IN) ((dfReportMetadata *) (IN))
#define DFREPORT_META_INIT(IN, OUT, CONTEXT) { .meta = *DENSITYFOLD_META(IN) }

#define DFREPORT_SUPP_INIT(IN, OUT, CONTEXT) { .ldstat = LOADDATA_META(IN)->stat, .dfstat = DENSITYFOLD_META(IN)->stat }

typedef densityFoldMetadata dfReportIn;

typedef struct {
    dfReportMetadata meta;
    dfReportSupp supp;
} dfReportOut;

bool dfReportHandler(const char *, size_t, dfReportType *, void *);
void dfReportContextDispose(dfReportContext *);
bool dfReportPrologue(dfReportIn *, dfReportOut **, dfReportContext *);
bool dfReportEpilogue(dfReportIn *, dfReportOut *, void *);

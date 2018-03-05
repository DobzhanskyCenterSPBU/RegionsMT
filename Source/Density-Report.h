#pragma once

#include "Density.h"

enum {
    DREPORTCONTEXT_BIT_POS_THRESHOLD = 0,
    DREPORTCONTEXT_BIT_POS_LIMIT,
    DREPORTCONTEXT_BIT_POS_HEADER,
    DREPORTCONTEXT_BIT_POS_SEMICOLON,
    DREPORTCONTEXT_BIT_CNT,
};

enum {
    DREPORTSUPP_STAT_BIT_POS_TASK_COMP = 0,
    DREPORTSUPP_STAT_BIT_POS_TASK_SUCC,
    DREPORTSUPP_STAT_BIT_CNT,
};

typedef struct {
    volatile uint8_t stat[BYTE_CNT(DREPORTSUPP_STAT_BIT_CNT)];
} dReportSupp;

typedef struct {
    char *path;
    double threshold;
    uint32_t limit;
    uint8_t bits[BYTE_CNT(DREPORTCONTEXT_BIT_CNT)];
} dReportContext;

typedef struct {
    densityMetadata meta;
} dReportMetadata;

#define DREPORT_META(IN) ((dReportMetadata *) (IN))
#define DREPORT_META_INIT(IN, OUT, CONTEXT) { .meta = *DENSITY_META(IN) }

typedef densityMetadata dReportIn;

typedef struct {
    dReportMetadata meta;
    dReportSupp supp;
} dReportOut;

void dReportContextDispose(dReportContext *);
bool dReportPrologue(dReportIn *, dReportOut **, dReportContext *);
bool dReportEpilogue(dReportIn *, dReportOut *, void *);

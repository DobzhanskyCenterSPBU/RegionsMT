#pragma once

#include "Genotypes.h"
#include <stdio.h>

enum {
    ADJUSTPVALUECONTEXT_BIT_POS_FILENAME = 0,
    ADJUSTPVALUECONTEXT_BIT_POS_TOPHITSNUM,
    ADJUSTPVALUECONTEXT_BIT_POS_MAXREPLICATIONS,
    ADJUSTPVALUECONTEXT_BIT_POS_ISADAPTIVE,
    ADJUSTPVALUECONTEXT_BIT_POS_K,
    ADJUSTPVALUECONTEXT_BIT_CNT
};

enum {
    ADJUSTPVALUESUPP_STAT_BIT_POS_TASK_COMP = 0,
    ADJUSTPVALUESUPP_STAT_BIT_POS_TASK_SUCC,
    ADJUSTPVALUESUPP_STAT_BIT_CNT
};

typedef struct {
    // size_t *li, *ri, *lc, *rc; // Left and right index, Left and right count // probably not required here
    double *apv; // Holds the adjusted p-values for all tests
} adjustPValueRes, *adjustPValueResPtr;

typedef struct {
    char *filename;
    size_t topHitsNum;//size_t!
    size_t maxReplications;
    size_t k;
    //adjustPValueType type; // we don't need this type for APV
    uint8_t bits[BYTE_CNT(ADJUSTPVALUECONTEXT_BIT_CNT)];
} adjustPValueContext, *adjustPValueContextPtr;

typedef struct {
    genotypesMetadata meta;
    adjustPValueResPtr res;
    adjustPValueContextPtr context;
    volatile uint8_t *stat;
    volatile size_t *hold;
} adjustPValueMetadata;

#define ADJUSTPVALUE_META(IN) ((adjustPValueMetadata *) (IN))
#define ADJUSTPVALUE_META_INIT(IN, OUT, CONTEXT) { .meta = *GENOTYPES_META(IN), .res = &(OUT)->res, /*.stat = (OUT)->supp.stat, .hold = &(OUT)->supp.hold*/ }

typedef genotypesMetadata adjustPValueIn; //maybe make a struct and include density here?

typedef struct {
    adjustPValueMetadata meta;
    adjustPValueRes res;
    //adjustPValueSupp supp;
} adjustPValueOut;

void adjustPValueContextDispose(adjustPValueContext *);
bool adjustPValuePrologue(adjustPValueIn *, adjustPValueOut **, adjustPValueContext *);
bool adjustPValueEpilogue(adjustPValueIn *, adjustPValueOut *, void *);

/*
 typedef struct {
 volatile uint8_t *ldstat;
 volatile uint8_t stat[BYTE_CNT(ADJUSTPVALUESUPP_STAT_BIT_CNT)];
 volatile size_t succ, fail, hold;
 size_t thresh;
 
 struct {
 task *tasks;
 size_t taskscnt;
 };
 
 //adjustPValueCallbackArgs *args; // why do we need this?
 //adjustPValueCallbackContext *context;
 } adjustPValueSupp;
 */



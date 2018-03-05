#pragma once

#include "StringTools.h"

#include <stdio.h>

typedef size_t (*tblselector)(char *, size_t, size_t *, void *);

typedef struct {
    handlerCallback handler;
    size_t ind, size;
} tblcolsch;

typedef struct {
    tblcolsch *colsch;
    size_t colschcnt;
} tblsch, *tblschPtr;

void tblClose(void **, tblsch *);
bool tblInit(void **, tblsch *, size_t, bool);

size_t rowCount(FILE *, size_t, size_t);
size_t rowAlign(FILE *, size_t);

typedef enum {
    ROWREAD_SUCC = 0,
    ROWREAD_ERR_QUOT,
    ROWREAD_ERR_VAL,
    ROWREAD_ERR_MEM,
    ROWREAD_ERR_EOL,
    ROWREAD_ERR_EOF,
    ROWREAD_ERR_FORM,
    ROWREAD_ERR_CNT
} rowReadErr;

inline const char *rowReadError(rowReadErr err)
{
    extern const char *rowReadErrStr[];
    return rowReadErrStr[err];
}

typedef struct {
    size_t read, row, col, byte;
    rowReadErr err;
} rowReadRes;

bool rowRead(FILE *, tblsch *, void **, void **, size_t, size_t, size_t, rowReadRes *, char);

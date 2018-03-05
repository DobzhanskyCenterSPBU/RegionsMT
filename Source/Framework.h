#pragma once

#include "Common.h"
#include "Log.h"
#include "Memory.h"
#include "SystemInfo.h"
#include "Threading.h"
#include "x86_64/Spinlock.h"
#include "x86_64/Tools.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    FRAMEWORKIN_BIT_POS_THREADCOUNT = 0,
    FRAMEWORKIN_BIT_CNT,
};

enum {
    FRAMEWORKCONTEXT_BIT_POS_THREADCOUNT = FRAMEWORKIN_BIT_POS_THREADCOUNT,
    FRAMEWORKCONTEXT_BIT_CNT
};

typedef struct {
    size_t **num;
    size_t cap, cnt;
} pnumInfo, *pnumInfoPtr;

typedef struct {
    task *tasks;
    size_t taskscnt, taskscap;
} tasksInfo, *tasksInfoPtr;

typedef struct {
    void **out;
    size_t outcnt, outcap;
} outInfo, *outInfoPtr;

typedef struct {
    logInfoPtr log;
    tasksInfoPtr tasks;
    outInfoPtr out;
    pnumInfoPtr pnum;
    threadPoolPtr pool;
} frameworkMetadata;

// The macro can be used with all types derived from 'frameworkOut'
#define FRAMEWORK_META(IN) ((frameworkMetadata *) (IN))

typedef struct {
    char *logFile;
    uint32_t threadCount;
    logInfoPtr logDef; // Default log
    uint8_t bits[BYTE_CNT(FRAMEWORKIN_BIT_CNT)];
} frameworkContext, frameworkIn;

enum {
    FRAMEWORKOUT_BIT_POS_FOPEN = 0,
    FRAMEWORKOUT_BIT_POS_MYSQL,
    FRAMEWORKOUT_BIT_CNT,
};

typedef struct {
    frameworkMetadata meta; // Meta-data must be the first member of the structure

    logInfo logInfo;
    tasksInfo tasksInfo;
    outInfo outInfo;
    pnumInfo pnumInfo;

    uint64_t initime;
    uint8_t bits[BYTE_CNT(FRAMEWORKOUT_BIT_CNT)];
} frameworkOut, *frameworkOutPtr;

void frameworkContextDispose(frameworkContext *);
bool frameworkPrologue(frameworkIn *, frameworkOut **, frameworkContext *);
bool frameworkEpilogue(frameworkIn *, frameworkOut *, frameworkContext *);

// Result of the function is valid only between this function calls and should never be passed to threads 
inline task *tasksInfoNextTask(tasksInfo* inf)
{
    if (!dynamicArrayTest((void **) &inf->tasks, &inf->taskscap, sizeof *inf->tasks, inf->taskscnt + 1)) return NULL;
    return &inf->tasks[inf->taskscnt++];
}

inline bool outInfoSetNext(outInfo* inf, void *arg)
{
    if (!dynamicArrayTest((void **) &inf->out, &inf->outcap, sizeof *inf->out, inf->outcnt + 1)) return 0;
    inf->out[inf->outcnt++] = arg;
    return 1;
}

// Used to provide pointers to numbers appearing in synchronization data. Pointers are visible from all threads at every time
inline size_t *pnumGet(pnumInfo *pnum, size_t num)
{
    size_t ind = sizeBitScanReverse(num) + 1;
    return ind ? &pnum->num[ind][num - (SIZE_C(1) << (ind - 1))] : pnum->num[0];
}

bool pnumTest(pnumInfo *, size_t);
void pnumClose(pnumInfo *);

// Represents the structure of the thread local storage
typedef struct
{
    size_t tempFileId;
} threadStorage;

// Warning! Never call this from the main thread!
inline size_t getTempFileName(threadPool* pool, char *buff, size_t buffsz, char *prefix)
{
    size_t pid = getProcessId(), tid, fid = ((threadStorage *) threadPoolFetchThreadData(pool, &tid))->tempFileId++;
    int ilen = snprintf(buff, buffsz, "%s%zX-%zX-%zX", prefix, pid, tid, fid); //-V111
    return ilen > 0 ? (unsigned) ilen : 0;
}

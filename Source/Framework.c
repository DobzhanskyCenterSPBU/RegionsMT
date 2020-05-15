#include "Common.h"
#include "Debug.h"
#include "Framework.h"
#include "SystemInfo.h"
#include "x86_64/Tools.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <mysql.h>

extern inline size_t *pnumGet(pnumInfo *, size_t);

extern inline bool outInfoSetNext(outInfo *, void *);
extern inline task *tasksInfoNextTask(tasksInfo *);
extern inline size_t getTempFileName(threadPool *, char *, size_t, char *);

bool pnumTest(pnumInfo *pnum, size_t num)
{
    size_t ind = sizeBitScanReverse(num) + 1;
    if (!dynamicArrayTest((void **) &pnum->num, &pnum->cap, sizeof *pnum->num, ind + 1)) return 0;
    
    for (; !pnum->cnt; pnum->cnt++)
    {
        pnum->num[0] = calloc(1, sizeof **pnum->num);
        if (!pnum->num[0]) return 0;
    }

    for (; pnum->cnt < ind + 1; pnum->cnt++)
    {
        size_t cnt = SIZE_C(1) << (pnum->cnt - 1);
        
        pnum->num[pnum->cnt] = malloc(cnt * sizeof **pnum->num);
        if (!pnum->num[pnum->cnt]) return 0;

        for (size_t i = 0, offset = SIZE_C(1) << (pnum->cnt - 1); i < cnt; pnum->num[pnum->cnt][i] = offset + i, i++);
    }

    return 1;
}

void pnumClose(pnumInfo *pnum)
{
    for (size_t i = 0; i < pnum->cnt; free(pnum->num[i++]));
    free(pnum->num);
}

void frameworkContextDispose(frameworkContext *context)
{
    if (!context) return;
    
    free(context->logFile);
    free(context);
}

bool frameworkPrologue(frameworkIn *in, frameworkOut **pout, frameworkContext *context)
{
    const char *strings[] =
    {
        __FUNCTION__,
        "WARNING (%s): Invalid thread amount specified: %" PRIu32 ". Value set to default: %" PRIu32 "!\n",
        "WARNING (%s): Cannot open specified log file \"%s\". %s. Default stream used!\n",
        "ERROR (%s): %s!\n",

        "Unable to create a thread pool",
        "Unable to initialize MySQL library"
    };

    enum
    {
        STR_FN = 0, STR_FR_WT, STR_FR_WF, STR_FR_EG,
        STR_M_THP, STR_M_SQL
    };

    char tempbuff[TEMP_BUFF] = { '\0' };

    const uint32_t threadCountDef = getProcessorCount();
    uint32_t threadCount = threadCountDef;
    char *logFile = NULL;

    frameworkOut *out = *pout = calloc(1, sizeof *out);
    if (!out) goto ERR();

    out->initime = getTime();

    if (in && in->logFile) logFile = in->logFile;
    else if (context && context->logFile) logFile = context->logFile;
        
    if (logFile && strcmp(logFile, "stderr"))
    {
        FILE *f = fopen(logFile, "w");

        if (f)
        {
            out->logInfo.dev = f;
            FRAMEWORK_META(out)->log = &out->logInfo;
            bitSet(out->bits, FRAMEWORKOUT_BIT_POS_FOPEN);
        }
        else
        {
            strerror_s(tempbuff, sizeof tempbuff, errno);
            
            if (in)
            {
                logMsg(in->logDef, strings[STR_FR_WF], strings[STR_FN], logFile, tempbuff);
                FRAMEWORK_META(out)->log = in->logDef;
            }
            else
            {
                out->logInfo.dev = stderr;
                logMsg(&out->logInfo, strings[STR_FR_WF], strings[STR_FN], logFile, tempbuff);                
                FRAMEWORK_META(out)->log = &out->logInfo;
            }
        }
    }
    else
    {
        if (in) FRAMEWORK_META(out)->log = in->logDef;
        else out->logInfo.dev = stderr, FRAMEWORK_META(out)->log = &out->logInfo;
    }
        
    if (in && bitTest(in->bits, FRAMEWORKCONTEXT_BIT_POS_THREADCOUNT)) threadCount = in->threadCount;
    else if (context && bitTest(context->bits, FRAMEWORKCONTEXT_BIT_POS_THREADCOUNT)) threadCount = context->threadCount;

    if (!threadCount || threadCount > threadCountDef)
    {
        logMsg(FRAMEWORK_META(out)->log, strings[STR_FR_WT], strings[STR_FN], threadCount, threadCountDef);
        threadCount = threadCountDef;
    }
        
    FRAMEWORK_META(out)->tasks = &out->tasksInfo;
    FRAMEWORK_META(out)->out = &out->outInfo;
    FRAMEWORK_META(out)->pnum = &out->pnumInfo;
           
    FRAMEWORK_META(out)->pool = threadPoolCreate(threadCount, 0, sizeof (threadStorage));
    if (!FRAMEWORK_META(out)->pool) goto ERR(Pool);
    
    if (mysql_library_init(0, NULL, NULL)) goto ERR(MySQL); // This should be done only once
    else bitSet(out->bits, FRAMEWORKOUT_BIT_POS_MYSQL);
    
    for (;;)
    {
        return 1;

    ERR():
        strerror_s(tempbuff, sizeof tempbuff, errno);
        logMsg(in->logDef, strings[STR_FR_EG], strings[STR_FN], tempbuff);
        break;
    
    ERR(Pool):
        logMsg(FRAMEWORK_META(out)->log, strings[STR_FR_EG], strings[STR_FN], strings[STR_M_THP]);
        break;

    ERR(MySQL):
        logMsg(FRAMEWORK_META(out)->log, strings[STR_FR_EG], strings[STR_FN], strings[STR_M_SQL]);
        break;
    }

    return 0;
}

bool frameworkEpilogue(frameworkIn *in, frameworkOut *out, frameworkContext *context)
{
    const char *strings[] =
    {
        __FUNCTION__,
        "WARNING (%s): Unable to enqueue tasks!\n",
        "WARNING (%s): %zu thread(s) yielded error(s)!\n",
        "WARNING (%s): Execution of %zu task(s) was canceled!\n",
        "Program execution",
    };

    enum
    {
        STR_FN = 0,  STR_FR_WE, STR_FR_WY, STR_FR_WT, SRT_M_EXE
    };
    
    (void) in;
    (void) context;
    
    size_t fail = 0, pend = 0;    
    if (out)
    {
        if (FRAMEWORK_META(out)->pool)
        {
            if (!threadPoolEnqueueTasks(FRAMEWORK_META(out)->pool, FRAMEWORK_META(out)->tasks->tasks, FRAMEWORK_META(out)->tasks->taskscnt, 0))
                logMsg(FRAMEWORK_META(out)->log, strings[STR_FR_WE], strings[STR_FN]);
            
            fail = threadPoolGetCount(FRAMEWORK_META(out)->pool);
            fail -= threadPoolDispose(FRAMEWORK_META(out)->pool, &pend);

            if (fail) logMsg(FRAMEWORK_META(out)->log, strings[STR_FR_WY], strings[STR_FN], fail);
            if (pend) logMsg(FRAMEWORK_META(out)->log, strings[STR_FR_WT], strings[STR_FN], pend);
        }
        
        logTime(FRAMEWORK_META(out)->log, out->initime, strings[STR_FN], strings[SRT_M_EXE]);

        if (bitTest(out->bits, FRAMEWORKOUT_BIT_POS_FOPEN)) fclose(FRAMEWORK_META(out)->log->dev);
        if (bitTest(out->bits, FRAMEWORKOUT_BIT_POS_MYSQL)) mysql_library_end();
                
        for (size_t i = 0; i < FRAMEWORK_META(out)->out->outcnt; free(FRAMEWORK_META(out)->out->out[i++]));
        free(FRAMEWORK_META(out)->out->out);

        free(FRAMEWORK_META(out)->tasks->tasks);
        
        pnumClose(FRAMEWORK_META(out)->pnum);
        free(out);
    }

    return !fail && !pend;
}

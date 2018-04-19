#include "np.h"
#include "main.h"

#include "module_root.h"

struct module_root_context {
    struct main_args base;
};

struct module_root_in {
    struct main_args *main_args;
    struct log *main_log;
};

bool module_root_prologue(void *In, void **p_Out, void *Context)
{
    struct module_root_in *in = In;
    struct module_root_context *context = Context;
    context->base = main_args_override(context->base, *in->main_args);

    struct module_root_out *out = *p_Out = calloc(1, sizeof(*out));
    if (!out) ;

    out->initime = getTime();
    
    if (context->)
    log_init(out->log, );

    if (in && in->logFile) logFile = in->logFile;
    else if (context && context->logFile) logFile = context->logFile;
        

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

    FRAMEWORK_META(out)->pool = threadPoolCreate(threadCount, 0, sizeof(threadStorage));
    if (!FRAMEWORK_META(out)->pool) goto ERR(Pool);

    if (mysql_library_init(0, NULL, NULL)) goto ERR(MySQL); // This should be done only once
    else bitSet(out->bits, FRAMEWORKOUT_BIT_POS_MYSQL);

    for (;;)
    {
        return 1;

        ERR() :
            strerror_s(tempbuff, sizeof tempbuff, errno);
        logMsg(in->logDef, strings[STR_FR_EG], strings[STR_FN], tempbuff);
        break;

        ERR(Pool) :
            logMsg(FRAMEWORK_META(out)->log, strings[STR_FR_EG], strings[STR_FN], strings[STR_M_THP]);
        break;

        ERR(MySQL) :
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
        STR_FN = 0, STR_FR_WE, STR_FR_WY, STR_FR_WT, SRT_M_EXE
    };

    (void) in;
    (void) context;

    if (out)
    {
        if (FRAMEWORK_META(out)->pool)
        {
            if (!threadPoolEnqueueTasks(FRAMEWORK_META(out)->pool, FRAMEWORK_META(out)->tasks->tasks, FRAMEWORK_META(out)->tasks->taskscnt, 0))
                logMsg(FRAMEWORK_META(out)->log, strings[STR_FR_WE], strings[STR_FN]);

            size_t fail = threadPoolGetCount(FRAMEWORK_META(out)->pool), pend;
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

    return 1;
}
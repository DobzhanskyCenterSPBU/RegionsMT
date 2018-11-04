#include "np.h"
#include "main.h"
#include "memory.h"

#include "module_root.h"

#include <stdlib.h>

void module_root_context_dispose(void *Context)
{
    
    free(Context);
}

void module_root_out_dispose(struct module_root_out *out)
{
    log_multiple_close(out->thread_log, out->thread_cnt);
    log_close(&out->log);
    free(out->thread_log);
    free(out);
}

bool module_root_prologue(void *In, void **p_Out, void *Context)
{
    struct module_root_in *in = In;
    struct module_root_context *context = Context;
    context->base = main_args_override(context->base, *in->main_args);

    struct module_root_out *out = *p_Out = calloc(1, sizeof(*out));
    if (!out) log_message_crt(in->main_log, CODE_METRIC, MESSAGE_ERROR, errno);
    else
    {
        out->initime = get_time();
        size_t thread_cnt = out->thread_cnt = context->base.thread_cnt;
        if (!array_init(&out->thread_log, NULL, thread_cnt, sizeof(*out->thread_log), 0, ARRAY_STRICT | ARRAY_CLEAR)) log_message_crt(in->main_log, CODE_METRIC, MESSAGE_ERROR, errno);
        else
        {
            if (log_init(&out->log, context->base.log_path, BLOCK_WRITE, 0, (struct style) { 0 }, in->main_log) &&
                log_multiple_init(out->thread_log, thread_cnt, NULL, BLOCK_WRITE, (struct style) { 0 }, in->main_log))
            {

            }
            log_multiple_close(out->thread_log, out->thread_cnt);
            log_close(&out->log);
        }
        
/*
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
*/

    }

    return 0;
}

/*
bool frameworkEpilogue(void *In, void *Out, void *Context)
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

*/

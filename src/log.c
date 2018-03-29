#include "np.h"
#include "log.h"
#include "memory.h"

#include <inttypes.h>
#include <stdlib.h>

size_t message_info_time_diff(char *buff, size_t buff_cap, void *Context)
{
    struct message_info_time_diff *context = Context;
    int tmp = 0;
    if (context->stop >= context->start)
    {
        uint64_t diff = context->stop - context->start;
        uint64_t hdq = diff / 3600000000, hdr = diff % 3600000000, mdq = hdr / 60000000, mdr = hdr % 60000000;
        double sec = (double) mdr / 1.e6;
        tmp =
            hdq ? snprintf(buff, buff_cap, "%s took %" PRIu64 " hr %" PRIu64 " min %.4f sec.\n", context->descr, hdq, mdq, sec) :
            mdq ? snprintf(buff, buff_cap, "%s took %" PRIu64 " min %.4f sec.\n", context->descr, mdq, sec) :
            snprintf(buff, buff_cap, "%s took %.4f sec.\n", context->descr, sec);
    }
    else
        tmp = snprintf(buff, buff_cap, "%s took too much time.\n", context->descr);
    return (size_t) MAX(tmp, 0);
}

size_t message_error_crt(char *buff, size_t buff_cap, void *Context)
{
    struct message_error_crt *context = Context;
    if (!Strerror_s(buff, buff_cap, context->code))
    {
        size_t len = Strnlen(buff, buff_cap);
        int tmp = snprintf(buff + len, buff_cap - len, "!\n");
        if (tmp > 0 && (size_t) tmp < buff_cap - len)
        {
            len += (size_t) tmp;
            return len;
        }        
    }
    return 0;
}

size_t message_var_generic(char *buff, size_t buff_cap, void *context, char *format, va_list arg)
{
    (void) context;
    int tmp = vsnprintf(buff, buff_cap, format, arg);
    return (size_t) MAX(tmp, 0);
}

bool log_init(struct log *restrict log, char *restrict path, size_t cnt)
{
    if (array_init(&log->buff, &log->buff_cap, cnt, sizeof(*log->buff), 0, 0))
    {
        if (path)
        {
            log->file = fopen(path, "w");
            if (log->file) return 1;
        }
        else
        {
            log->file = stderr;
            return 1;
        }
        free(log->buff);
    }
    return 0;
}

void log_close(struct log *restrict p_log)
{
    if (p_log->file && p_log->file != stderr) fclose(p_log->file);
    free(p_log->buff);
}

#define TIMESTAMP_FORMAT "[%Y-%m-%d %H:%M:%S UTC%z] "

static size_t log_prefix(char *buff, size_t buff_cap, enum message_type type, const char *func, const char *path, size_t line)
{
    time_t t;
    time(&t);
    struct tm ts;
    Localtime_s(&ts, &t);
    char *title[] = { "MESSAGE", "ERROR", "WARNING", "NOTE", "INFO" };
    size_t len = strftime(buff, buff_cap, TIMESTAMP_FORMAT, &ts);
    if (len)
    {
        int tmp = snprintf(buff + len, buff_cap - len, "%s (%s @ \"%s\":%zu): ", title[type], func, path, line);
        return len + (size_t) MAX(0, tmp);
    }
    return countof(TIMESTAMP_FORMAT) + 4;
}

bool log_message_var(struct log *restrict log, struct message *restrict message, char *format, ...)
{
    if (!log) return 1;
    size_t len;
    enum array_status status;
    do {
        len = log_prefix(log->buff, log->buff_cap, message->type, message->func, message->path, message->line);
        status = array_test(&log->buff, &log->buff_cap, sizeof(*log->buff), 0, 0, ARG_SIZE(len, 1));
    } while (status != ARRAY_NO_CHANGE);
    if (status) // len < log->buff_cap
    {
        size_t tmp;
        do {
            if (format)
            {
                va_list arg;
                va_start(arg, format);
                tmp = message->handler_var(log->buff + len, log->buff_cap - len, message, format, arg);
                va_end(arg);
            }
            else 
                tmp = message->handler(log->buff + len, log->buff_cap - len, message);
            status = array_test(&log->buff, &log->buff_cap, sizeof(*log->buff), 0, 0, ARG_SIZE(tmp, len + 1));
        } while (status != ARRAY_NO_CHANGE);
        if (status) // tmp < log->buff_cap - len
        {
            len += tmp;
            size_t wr = fwrite(log->buff, 1, len, log->file);
            log->file_sz += wr;
            if (wr == len) return 1;
        }
    }
    return 0;
}

bool log_message(struct log *restrict log, struct message *restrict message)
{
    return log_message_var(log, message, NULL);
}

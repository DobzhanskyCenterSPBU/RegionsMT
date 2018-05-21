#include "np.h"
#include "ll.h"
#include "log.h"
#include "memory.h"

#include <inttypes.h>
#include <stdlib.h>

size_t message_time_diff(char *buff, size_t buff_cnt, void *Context)
{
    struct time_diff *context = Context;
    int tmp = 0;
    if (context->stop >= context->start)
    {
        uint64_t diff = context->stop - context->start;
        uint64_t hdq = diff / 3600000000, hdr = diff % 3600000000, mdq = hdr / 60000000, mdr = hdr % 60000000;
        double sec = (double) mdr / 1.e6;
        tmp =
            hdq ? snprintf(buff, buff_cnt, "%s took %" PRIu64 " hr %" PRIu64 " min %.4f sec.\n", context->prefix, hdq, mdq, sec) :
            mdq ? snprintf(buff, buff_cnt, "%s took %" PRIu64 " min %.4f sec.\n", context->prefix, mdq, sec) :
            snprintf(buff, buff_cnt, "%s took %.4f sec.\n", context->prefix, sec);
    }
    else
        tmp = snprintf(buff, buff_cnt, "%s took too much time.\n", context->prefix);
    return (size_t) MAX(tmp, 0);
}

size_t message_crt(char *buff, size_t buff_cnt, void *Context)
{
    if (!Strerror_s(buff, buff_cnt, *(Errno_t *) Context))
    {
        size_t len = Strnlen(buff, buff_cnt);
        int tmp = snprintf(buff + len, buff_cnt - len, "!\n");
        if (tmp > 0 && (size_t) tmp < buff_cnt - len)
        {
            len += (size_t) tmp;
            return len;
        }        
    }
    return size_add_sat(buff_cnt, buff_cnt);
}

size_t message_var_generic(char *buff, size_t buff_cnt, void *context, const char *format, va_list arg)
{
    (void) context;
    int tmp = vsnprintf(buff, buff_cnt, format, arg);
    return (size_t) MAX(tmp, 0);
}

size_t message_var_crt(char *buff, size_t buff_cnt, void *context, const char *format, va_list arg)
{
    size_t len = message_var_generic(buff, buff_cnt, context, format, arg);
    return len < buff_cnt ? len + message_crt(buff + len, buff_cnt - len, context) : len;
}

bool log_init(struct log *restrict log, char *restrict path, size_t cnt)
{
    if (array_init(&log->buff, &log->buff_cap, cnt, sizeof(*log->buff), 0, 0))
    {
        log->buff_cnt = 0;
        log->buff_lim = cnt;
        if (path)
        {
            log->file = fopen(path, "wt");
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

bool log_flush(struct log *restrict log)
{
    size_t cnt = log->buff_cnt;
    if (cnt)
    {
        size_t wr = fwrite(log->buff, 1, cnt, log->file);
        log->file_sz += wr;
        log->buff_cnt = 0;
        fflush(log->file);
        return wr == cnt;
    }
    return 1;
}

void log_close(struct log *restrict log)
{
    log_flush(log);
    if (log->file && log->file != stderr) fclose(log->file);
    free(log->buff);
}

static size_t log_prefix(char *buff, size_t buff_cap, struct code_metric *code_metric, enum message_type type)
{
    time_t t;
    time(&t);
    struct tm ts;
    Localtime_s(&ts, &t);
    char *title[] = { "MESSAGE", "ERROR", "WARNING", "NOTE", "INFO" };
    size_t len = strftime(buff, buff_cap, "[%Y-%m-%d %H:%M:%S UTC%z] ", &ts);
    if (len)
    {
        int tmp = snprintf(buff + len, buff_cap - len, "%s (%s @ \"%s\":%zu): ", title[type], code_metric->func, code_metric->path, code_metric->line);
        return len + (size_t) MAX(0, tmp);
    }
    return size_add_sat(buff_cap, buff_cap);
}

static bool log_message_supp(struct log *restrict log, struct code_metric *code_metric, enum message_type type, message_callback handler, message_callback_var handler_var, void *context, const char *restrict format, va_list *arg)
{
    if (!log) return 1;
    size_t len;
    for (;;) 
    {
        len = log_prefix(log->buff + log->buff_cnt, log->buff_cap - log->buff_cnt, code_metric, type);
        switch (array_test(&log->buff, &log->buff_cap, sizeof(*log->buff), 0, 0, ARG_SIZE(log->buff_cnt, len, 1)))
        {
        case ARRAY_SUCCESS: 
            continue;
        case ARRAY_FAILURE:
            return 0;
        default: // len < log->buff_cap
            break;
        }
        break;
    }
    size_t len2;
    for (;;)
    {
        len2 = 
            handler ? handler(log->buff + log->buff_cnt + len, log->buff_cap - log->buff_cnt - len, context) :
            handler_var && format ? handler_var(log->buff + log->buff_cnt + len, log->buff_cap - log->buff_cnt - len, context, format, *arg) : 0;
        switch (array_test(&log->buff, &log->buff_cap, sizeof(*log->buff), 0, 0, ARG_SIZE(log->buff_cnt, len2, len, 1)))
        {
        case ARRAY_SUCCESS:
            continue;
        case ARRAY_FAILURE:
            return 0;
        default: // len2 < log->buff_cap - len
            break;
        }
        break;
    }
    if ((log->buff_cnt += len2 + len) >= log->buff_lim) return log_flush(log);
    return 1;
}

bool log_message(struct log *restrict log, struct code_metric *restrict code_metric, enum message_type type, message_callback handler, void *context)
{
    return log_message_supp(log, code_metric, type, handler, NULL, context, NULL, NULL);
}

bool log_message_var(struct log *restrict log, struct code_metric *restrict code_metric, enum message_type type, message_callback_var handler_var, void *context, const char *restrict format, ...)
{
    va_list arg;
    va_start(arg, format);
    bool res = log_message_supp(log, code_metric, type, NULL, handler_var, context, format, &arg);
    va_end(arg);
    return res;
}

bool log_message_generic(struct log *restrict log, struct code_metric *restrict code_metric, enum message_type type, const char *restrict format, ...)
{
    va_list arg;
    va_start(arg, format);
    bool res = log_message_supp(log, code_metric, type, NULL, message_var_generic, NULL, format, &arg);
    va_end(arg);
    return res;
}

bool log_message_time_diff(struct log *restrict log, struct code_metric *restrict code_metric, enum message_type type, uint64_t start, uint64_t stop, const char *restrict prefix)
{
    return log_message(log, code_metric, type, message_time_diff, &(struct time_diff) { .start = start, .stop = stop, .prefix = prefix });
}

bool log_message_crt(struct log *restrict log, struct code_metric *restrict code_metric, enum message_type type, Errno_t err)
{
    return log_message(log, code_metric, type, message_crt, &err);
}

bool log_message_fopen(struct log *restrict log, struct code_metric *restrict code_metric, enum message_type type, const char *restrict path, Errno_t err)
{
    return log_message_var(log, code_metric, type, message_var_crt, &err, "Unable to open the file \"%s\": ", path);
}

bool log_message_fseek(struct log *restrict log, struct code_metric *restrict code_metric, enum message_type type, int64_t offset, const char *restrict path)
{
    return log_message_generic(log, code_metric, type, "Unable to seek into the position " PRId64 " while reading the file \"%s\"!\n", offset, path);
}

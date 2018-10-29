#include "np.h"
#include "ll.h"
#include "log.h"
#include "memory.h"

#include <inttypes.h>
#include <stdlib.h>

DECLARE_PATH

static bool time_diff(char *buff, size_t *p_cnt, uint64_t start, uint64_t stop)
{
    size_t cnt = *p_cnt;
    uint64_t diff = DIST(stop, start);
    uint64_t hdq = diff / 3600000000, hdr = diff % 3600000000, mdq = hdr / 60000000, mdr = hdr % 60000000;
    double sec = 1.e-6 * (double) mdr;
    int tmp =
        hdq ? snprintf(buff, cnt, "%" PRIu64 " hr %" PRIu64 " min %.6f sec.\n", hdq, mdq, sec) :
        mdq ? snprintf(buff, cnt, "%" PRIu64 " min %.6f sec.\n", mdq, sec) :
        sec >= 1.e-6 ? snprintf(buff, cnt, "%.6f sec.\n", sec) :
        snprintf(buff, cnt, "less than %.6f sec.\n", 1.e-6);
    if (tmp < 0) return 0;
    *p_cnt = (size_t) tmp;
    return 1;
}

static bool message_time_diff(char *buff, size_t *p_cnt, void *Context)
{
    struct time_diff *context = Context;
    return time_diff(buff, p_cnt, context->start, context->stop);
}

bool message_crt(char *buff, size_t *p_cnt, void *Context)
{
    size_t cnt = *p_cnt;
    if (!Strerror_s(buff, cnt, *(Errno_t *) Context))
    {
        size_t len = Strnlen(buff, cnt); // 'len' is not greater than 'cnt' due to the previous condition
        int tmp = snprintf(buff + len, cnt - len, "!\n");
        if (tmp < 0) return 0;
        *p_cnt = size_add_sat(len, (size_t) tmp);
        return 1; 
    }
    *p_cnt = size_add_sat(cnt, cnt);
    return 1;
}

bool message_var_generic(char *buff, size_t *p_cnt, void *context, const char *format, va_list arg)
{
    (void) context;
    int tmp = vsnprintf(buff, *p_cnt, format, arg);
    if (tmp < 0) return 0;
    *p_cnt = (size_t) tmp;
    return 1;
}

bool message_var_staged(char *buff, size_t *p_cnt, void *Thunk, const char *format, va_list arg)
{
    struct message_thunk *thunk = Thunk;
    size_t len = *p_cnt, cnt = 0;
    for (unsigned i = 0;; i++)
    {
        size_t tmp = len;
        switch (i)
        {
        case 0:
            if (!message_var_generic(buff + cnt, &tmp, NULL, format, arg)) return 0;
            break;
        case 1:
            if (!thunk->handler(buff + cnt, &tmp, thunk->context)) return 0;
            break;
        }
        cnt = size_add_sat(cnt, tmp);
        if (i == 1) break;
        len = size_sub_sat(len, tmp);
    }
    *p_cnt = cnt;
    return 1;
}

// Last argument may be 'NULL'
bool log_init(struct log *restrict log, char *restrict path, size_t cap, bool append, struct log *restrict log_error)
{
    if (!array_init(&log->buff, &log->cap, cap, sizeof(*log->buff), 0, 0)) log_message_crt(log_error, CODE_METRIC, MESSAGE_ERROR, errno);
    else
    {
        log->cnt = 0;
        log->lim = cap;
        if (path)
        {
            log->file = fopen(path, append ? "at" : "wt");
            if (!log->file) log_message_fopen(log_error, CODE_METRIC, MESSAGE_ERROR, path, errno);
            else return 1;
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
    size_t cnt = log->cnt;
    if (cnt)
    {
        size_t wr = fwrite(log->buff, 1, cnt, log->file);
        log->tot += wr;
        log->cnt = 0;
        fflush(log->file);
        return wr == cnt;
    }
    return 1;
}

// May be used for 'log' allocated by 'calloc' (or filled with zeros statically)
void log_close(struct log *restrict log)
{
    log_flush(log);
    Fclose(log->file);
    free(log->buff);
}

bool log_multiple_init(struct log *restrict arr, size_t cnt, char *restrict path, size_t cap, struct log *restrict log_error)
{
    size_t i = 0;
    for (; i < cnt && log_init(arr + i, path, cap, 1, log_error); i++);
    if (i == cnt) return 1;
    for (; --i; log_close(arr + i));
    return 0;
}

void log_multiple_close(struct log *restrict arr, size_t cnt)
{
    if (cnt) for (size_t i = cnt; --i; log_close(arr + i));
}

static bool log_prefix(char *buff, size_t *p_cnt, struct code_metric code_metric, enum message_type type)
{
    const char *title[] = { 
        ANSI_COLOR_CYAN "MESSAGE" ANSI_COLOR_RESET,
        ANSI_COLOR_BRIGHT_RED "ERROR" ANSI_COLOR_RESET,
        ANSI_COLOR_YELLOW "WARNING" ANSI_COLOR_RESET,
        ANSI_COLOR_BLUE "NOTE" ANSI_COLOR_RESET,
        ANSI_COLOR_GREEN "INFO" ANSI_COLOR_RESET,
        ANSI_COLOR_BLACK "DAMNATION" ANSI_COLOR_RESET
    };
    time_t t;
    time(&t);
    struct tm ts;
    Localtime_s(&ts, &t);
    size_t cnt = *p_cnt, len = strftime(buff, cnt, ANSI_COLOR_BRIGHT_BLACK "[%Y-%m-%d %H:%M:%S UTC%z]" ANSI_COLOR_RESET " ", &ts);
    if (len)
    {
        int tmp = snprintf(buff + len, cnt - len, "%s " ANSI_COLOR_BRIGHT_BLACK "(%s @ \"%s\":%zu):" ANSI_COLOR_RESET " ", title[type], code_metric.func, code_metric.path, code_metric.line);
        if (tmp < 0) return 0;
        *p_cnt = size_add_sat(len, (size_t) tmp);
        return 1;
    }
    *p_cnt = size_add_sat(cnt, cnt);
    return 1;
}

static bool log_message_impl(struct log *restrict log, struct code_metric code_metric, enum message_type type, message_callback handler, message_callback_var handler_var, void *context, const char *restrict format, va_list *arg)
{
    if (!log) return 1;
    size_t cnt = log->cnt, len;
    for (unsigned i = 0; i < 2; i++)
    {
        for (;;)
        {
            len = log->cap - cnt;
            switch (i)
            {
            case 0:
                if (!log_prefix(log->buff + cnt, &len, code_metric, type)) return 0;
                break;
            case 1:
                if (handler ? !handler(log->buff + cnt, &len, context) : handler_var && format && !handler_var(log->buff + cnt, &len, context, format, *arg)) return 0;
                break;
            }
            unsigned res = array_test(&log->buff, &log->cap, sizeof(*log->buff), 0, 0, ARG_SIZE(cnt, len, 1));
            if (!res) return 0;
            if (res & ARRAY_UNTOUCHED) break;
        }
        cnt += len;
    }
    log->cnt = cnt;
    return cnt >= log->lim ? log_flush(log) : 1;
}

bool log_message(struct log *restrict log, struct code_metric code_metric, enum message_type type, message_callback handler, void *context)
{
    return log_message_impl(log, code_metric, type, handler, NULL, context, NULL, NULL);
}

bool log_message_var(struct log *restrict log, struct code_metric code_metric, enum message_type type, message_callback_var handler_var, void *context, const char *restrict format, ...)
{
    va_list arg;
    va_start(arg, format);
    bool res = log_message_impl(log, code_metric, type, NULL, handler_var, context, format, &arg);
    va_end(arg);
    return res;
}

bool log_message_generic(struct log *restrict log, struct code_metric code_metric, enum message_type type, const char *restrict format, ...)
{
    va_list arg;
    va_start(arg, format);
    bool res = log_message_impl(log, code_metric, type, NULL, message_var_generic, NULL, format, &arg);
    va_end(arg);
    return res;
}

bool log_message_time_diff(struct log *restrict log, struct code_metric code_metric, enum message_type type, uint64_t start, uint64_t stop, const char *restrict format, ...)
{
    va_list arg;
    va_start(arg, format);
    bool res = log_message_impl(log, code_metric, type, NULL, message_var_staged, &(struct message_thunk) { .handler = message_time_diff, .context = &(struct time_diff) { .start = start, .stop = stop } }, format, &arg);
    va_end(arg);
    return res;
}

bool log_message_crt(struct log *restrict log, struct code_metric code_metric, enum message_type type, Errno_t err)
{
    return log_message(log, code_metric, type, message_crt, &err);
}

bool log_message_fopen(struct log *restrict log, struct code_metric code_metric, enum message_type type, const char *restrict path, Errno_t err)
{
    return log_message_var(log, code_metric, type, message_var_staged, &(struct message_thunk) { .handler = message_crt, .context = &err }, "Unable to open the file \"%s\": ", path);
}

bool log_message_fseek(struct log *restrict log, struct code_metric code_metric, enum message_type type, int64_t offset, const char *restrict path)
{
    return log_message_generic(log, code_metric, type, "Unable to seek into the position " PRId64 " while reading the file \"%s\"!\n", offset, path);
}

#include "np.h"
#include "ll.h"
#include "argv.h"
#include "memory.h"
#include "sort.h"
#include "utf8.h"

#include <string.h>
#include <stdlib.h>

enum argv_status {
    ARGV_WARNING_MISSING_VALUE_LONG = 0,
    ARGV_WARNING_MISSING_VALUE_SHRT,
    ARGV_WARNING_UNHANDLED_PAR_LONG,
    ARGV_WARNING_UNHANDLED_PAR_SHRT,
    ARGV_WARNING_UNHANDLED_OPT_LONG,
    ARGV_WARNING_UNHANDLED_OPT_SHRT,
    ARGV_WARNING_UNEXPECTED_VALUE_LONG,
    ARGV_WARNING_UNEXPECTED_VALUE_SHRT,
    ARGV_WARNING_INVALID_PAR_LONG,
    ARGV_WARNING_INVALID_PAR_SHRT,
    ARGV_WARNING_INVALID_UTF,
};

struct argv_context {
    char *name_str, *val_str;
    size_t name_len, val_len, ind;
    enum argv_status status;
};

static bool message_argv(char *buff, size_t *p_cnt, void *Context)
{
    const char *fmt[] = {
        "Expected a value for",
        "Unable to handle the value \"%.*s\" of",
        "Unable to handle",
        "Unused value of",
        "Invalid %s",
        "Invalid UTF-8 byte sequence at the byte %zu of",
    };
    struct argv_context *context = Context;
    size_t cnt = 0, len = *p_cnt;
    char quote = context->status & 1 ? '\'' : '\"';
    for (unsigned i = 0;; i++)
    {
        int tmp = -1;
        switch (i)
        {
        case 0:
            switch (context->status)
            {
            case ARGV_WARNING_UNHANDLED_PAR_LONG:
            case ARGV_WARNING_UNHANDLED_PAR_SHRT:
                tmp = snprintf(buff + cnt, len, fmt[context->status / 2], INTP(context->val_len), context->val_str);
                break;
            case ARGV_WARNING_INVALID_PAR_LONG:
                tmp = snprintf(buff + cnt, len, fmt[context->status / 2], "name");
                break;
            case ARGV_WARNING_INVALID_PAR_SHRT:
                tmp = snprintf(buff + cnt, len, fmt[context->status / 2], "character");
                break;
            case ARGV_WARNING_INVALID_UTF:
                tmp = snprintf(buff + cnt, len, fmt[context->status / 2], context->name_len + 1);
                break;
            default:
                tmp = snprintf(buff + cnt, len, "%s", fmt[context->status / 2]);
            }
            break;
        case 1:
            switch (context->status)
            {
            case ARGV_WARNING_INVALID_PAR_LONG:
            case ARGV_WARNING_INVALID_PAR_SHRT:
                tmp = snprintf(buff + cnt, len, " %c%.*s%c in", quote, INTP(context->name_len), context->name_str, quote);
                break;
            default:
                tmp = 0;
            }
            break;
        case 2:
            tmp = snprintf(buff + cnt, len, " the command-line parameter");
            break;
        case 3:
            switch (context->status)
            {
            case ARGV_WARNING_INVALID_PAR_LONG:
            case ARGV_WARNING_INVALID_PAR_SHRT:
            case ARGV_WARNING_INVALID_UTF:
                tmp = 0;
                break;
            default:
                tmp = snprintf(buff + cnt, len, " %c%.*s%c", quote, INTP(context->name_len), context->name_str, quote);
            }
            break;
        case 4:
            tmp = snprintf(buff + cnt, len, " no. %zu!\n", context->ind);
        }
        if (tmp < 0) return 0;
        cnt = size_add_sat(cnt, (size_t) tmp);
        if (i == 4) break;
        len = size_sub_sat(len, (size_t) tmp);
    }
    *p_cnt = cnt;
    return 1;
}

static bool log_message_warning_argv(struct log *restrict log, struct code_metric code_metric, char *name_str, size_t name_len, char *val_str, size_t val_len, size_t ind, enum argv_status status)
{
    return log_message(log, code_metric, MESSAGE_WARNING, message_argv, &(struct argv_context) { .status = status, .name_str = name_str, .val_str = val_str, .name_len = name_len, .val_len = val_len, .ind = ind });
}

static bool utf8_decode_len(char *str, size_t tot, size_t *p_len)
{
    uint32_t val; // Never used
    uint8_t len = 0, context = 0;
    for (size_t i = 0; i < tot; i++)
    {
        if (!utf8_decode(str[i], &val, NULL, &len, &context))
        {
            *p_len = i;
            return 0;
        }
        if (!context) break;
    }
    *p_len = len;
    return 1;
}

bool argv_parse(par_selector_callback selector, void *context, void *res, char **argv, size_t argc, char ***p_arr, size_t *p_cnt, struct log *log)
{
    struct par par = { 0 };
    char *str = NULL;
    size_t cnt = 0, len = 0;
    bool halt = 0, succ = 1;
    int capture = 0;
    *p_arr = NULL;
    *p_cnt = 0;
    for (size_t i = 1; i < argc; i++)
    {
        if (!halt)
        {
            bool hyph = argv[i][0] == '-';
            if (capture)
            {
                bool shrt = capture < 0;
                capture = 0;
                if (par.mode != PAR_VALUED_OPTION || !hyph)
                {
                    size_t tmp = strlen(argv[i]);
                    if (par.handler && !par.handler(argv[i], tmp, par.ptr, par.context)) log_message_warning_argv(log, CODE_METRIC, str, len, argv[i], tmp, i - 1, shrt ? ARGV_WARNING_UNHANDLED_PAR_SHRT : ARGV_WARNING_UNHANDLED_PAR_LONG);
                    continue;
                }
                else if (par.handler && !par.handler(NULL, 0, par.ptr, par.context)) log_message_warning_argv(log, CODE_METRIC, str, len, NULL, 0, i, shrt ? ARGV_WARNING_UNHANDLED_OPT_SHRT : ARGV_WARNING_UNHANDLED_OPT_LONG);
            }
            if (hyph)
            {
                if (argv[i][1] == '-') // Long mode
                {
                    str = argv[i] + 2;
                    if (!*str) // halt on "--"
                    {
                        halt = 1;
                        continue;
                    }
                    len = strcspn(str, "=");
                    if (!selector(&par, str, len, res, context, 0)) log_message_warning_argv(log, CODE_METRIC, str, len, NULL, 0, i, ARGV_WARNING_INVALID_PAR_LONG);
                    else
                    {
                        if (par.mode == PAR_OPTION)
                        {
                            if (str[len]) log_message_warning_argv(log, CODE_METRIC, str, len, NULL, 0, i, ARGV_WARNING_UNEXPECTED_VALUE_LONG);
                            if (par.handler && !par.handler(NULL, 0, par.ptr, par.context)) log_message_warning_argv(log, CODE_METRIC, str, len, NULL, 0, i, ARGV_WARNING_UNHANDLED_OPT_LONG);
                        }
                        else if (!str[len]) capture = 1;
                        else
                        {
                            size_t tmp = strlen(str + len + 1);
                            if (par.handler && !par.handler(str + len + 1, tmp, par.ptr, par.context)) log_message_warning_argv(log, CODE_METRIC, str, len, str + len + 1, tmp, i, ARGV_WARNING_UNHANDLED_PAR_LONG);
                        }
                    }
                }
                else // Short mode
                {
                    size_t tot = strlen(argv[i] + 1);
                    for (size_t k = 1; argv[i][k];) // Inner loop for handling multiple option-like parameters
                    {
                        str = argv[i] + k;
                        if (!utf8_decode_len(str, tot, &len)) log_message_warning_argv(log, CODE_METRIC, NULL, k + len, NULL, 0, i, ARGV_WARNING_INVALID_UTF);
                        else
                        {
                            if (!selector(&par, str, len, res, context, 1)) log_message_warning_argv(log, CODE_METRIC, str, len, NULL, 0, i, ARGV_WARNING_INVALID_PAR_SHRT);
                            else
                            {
                                if (par.mode != PAR_OPTION) // Parameter expects value
                                {
                                    if (!str[len]) capture = -1;
                                    else
                                    {
                                        size_t tmp = strlen(str + len);
                                        if (par.handler && !par.handler(str + len, tmp, par.ptr, par.context)) log_message_warning_argv(log, CODE_METRIC, str, len, str + len, tmp, i, ARGV_WARNING_UNHANDLED_PAR_SHRT);
                                    }
                                    break; // Exiting from the inner loop
                                }
                                else if (par.handler && !par.handler(NULL, 0, par.ptr, par.context)) log_message_warning_argv(log, CODE_METRIC, str, len, NULL, 0, i, ARGV_WARNING_UNHANDLED_OPT_SHRT);
                            }
                            if ((k += len) > tot) break;
                        }                     
                    }
                }
                continue;
            }            
        }
        if (!array_test(p_arr, p_cnt, sizeof(**p_arr), 0, 0, cnt, 1)) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
        else
        {
            (*p_arr)[cnt++] = argv[i]; // Storing positional parameters
            continue;
        }       
        succ = 0;
        break;
    }
    if (succ)
    {
        if (capture && par.mode == PAR_VALUED) log_message_warning_argv(log, CODE_METRIC, str, len, NULL, 0, argc - 1, capture > 0 ? ARGV_WARNING_MISSING_VALUE_LONG : ARGV_WARNING_MISSING_VALUE_SHRT);
        if (!array_test(p_arr, p_cnt, sizeof(**p_arr), 0, ARRAY_REDUCE, cnt)) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
        else return 1;
    }
    free(*p_arr);
    *p_cnt = 0;
    return 0;    
}

bool argv_par_selector(struct par *par, const char *str, size_t len, void *res, void *Context, bool shrt)
{
    struct argv_par_sch *context = Context;
    struct tag *tag = NULL;
    size_t cnt = 0, ind;
    if (shrt) tag = context->stag, cnt = context->stag_cnt;
    else tag = context->ltag, cnt = context->ltag_cnt;
    if (binary_search(&ind, str, tag, cnt, sizeof(*tag), str_strl_stable_cmp, &len, 0))
    {
        size_t id = tag[ind].id;
        if (id < context->par_sch_cnt)
        {
            struct par_sch par_sch = context->par_sch[id];
            *par = (struct par) { .ptr = (char *) res + par_sch.off, .context = par_sch.context, .handler = par_sch.handler, .mode = par_sch.mode };
            return 1;
        }
    }
    return 0;
}

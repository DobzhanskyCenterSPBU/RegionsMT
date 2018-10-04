#include "np.h"
#include "ll.h"
#include "argv.h"
#include "memory.h"
#include "sort.h"
#include "utf8.h"

#include <string.h>
#include <stdlib.h>

DECLARE_PATH

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
    ARGV_MAX
};

struct argv_context {
    enum argv_status status;
    char *str, *par;
    size_t len, ind;
};

static bool message_argv(char *buff, size_t *p_buff_cnt, void *Context)
{
    const char *fmt[] = {
        "Expected a value for",
        "Unable to handle the value \"%s\" of",
        "Unable to handle",
        "Unused value of",
        "Invalid %s"
    };
    struct argv_context *context = Context;
    if (context->status >= ARGV_MAX) return 0;
    size_t cnt = 0, len = *p_buff_cnt;
    for (unsigned i = 0; i < 2; i++)
    {
        int tmp = -1;
        char quote;
        switch (i)
        {
        case 0:
            switch (context->status)
            {
            case ARGV_WARNING_UNHANDLED_PAR_LONG:
            case ARGV_WARNING_UNHANDLED_PAR_SHRT:
                tmp = snprintf(buff + cnt, len, fmt[context->status / 2], context->par);
                break;
            case ARGV_WARNING_INVALID_PAR_LONG:
                tmp = snprintf(buff + cnt, len, fmt[context->status / 2], "name");
                break;
            case ARGV_WARNING_INVALID_PAR_SHRT:
                tmp = snprintf(buff + cnt, len, fmt[context->status / 2], "character");
                break;
            default:
                tmp = snprintf(buff + cnt, len, "%s", fmt[context->status / 2]);
            }
            break;
        case 1:
            quote = context->status & 1 ? '\'' : '\"';
            switch (context->status)
            {
            case ARGV_WARNING_INVALID_PAR_LONG:
            case ARGV_WARNING_INVALID_PAR_SHRT:
                tmp = snprintf(buff + cnt, len, " %c%.*s%c in the command-line parameter no. %zu!\n", quote, (int) context->len, context->str, quote, context->ind);
                break;
            default:
                tmp = snprintf(buff + cnt, len, " the command-line parameter %c%.*s%c no. %zu!\n", quote, (int) context->len, context->str, quote, context->ind);
            }
            break;
        }
        if (tmp < 0) return 0;
        len = size_sub_sat(len, (size_t) tmp);
        cnt = size_add_sat(cnt, (size_t) tmp);
    }
    *p_buff_cnt = cnt;
    return 1;
}

static bool log_message_warning_argv(struct log *restrict log, struct code_metric code_metric, char *str, size_t len, size_t ind, enum argv_status status, char *par)
{
    return log_message(log, code_metric, MESSAGE_WARNING, message_argv, &(struct argv_context) { .status = status, .str = str, .len = len, .ind = ind, .par = par });
}

bool argv_parse(par_selector_callback selector_long, par_selector_callback selector_shrt, void *context, void *res, char **argv, size_t argv_cnt, char ***p_input, size_t *p_input_cnt, struct log *log)
{
    struct par par;
    char *str = NULL;
    size_t input_cnt = 0, len = 0;
    bool halt = 0;
    int8_t capture = 0;
    for (size_t i = 1; i < argv_cnt; i++)
    {
        if (!halt)
        {
            if (capture)
            {
                if (par.handler && !par.handler(argv[i], SIZE_MAX, (char *) res + par.offset, par.context)) log_message_warning_argv(log, CODE_METRIC, str, len, i, capture > 0 ? ARGV_WARNING_UNHANDLED_PAR_LONG : ARGV_WARNING_UNHANDLED_PAR_SHRT, argv[i]);
                capture = 0;
            }
            else if (argv[i][0] == '-')
            {
                if (argv[i][1] == '-') // Long mode
                {
                    str = argv[i] + 2;
                    if (!*str) // halt on "--"
                    {
                        halt = 1;
                        continue;
                    }
                    char *tmp = strchr(str, '=');
                    len = tmp ? (size_t) (tmp - str) : SIZE_MAX;
                    if (!selector_long(&par, str, len, context)) log_message_warning_argv(log, CODE_METRIC, str, len, i, ARGV_WARNING_INVALID_PAR_LONG, NULL);
                    else
                    {
                        if (par.option)
                        {
                            if (tmp) log_message_warning_argv(log, CODE_METRIC, str, len, i, ARGV_WARNING_UNEXPECTED_VALUE_LONG, NULL);
                            if (par.handler && !par.handler(NULL, SIZE_MAX, (char *) res + par.offset, par.context)) log_message_warning_argv(log, CODE_METRIC, str, len, i, ARGV_WARNING_UNHANDLED_OPT_LONG, NULL);
                        }
                        else
                        {
                            if (tmp)
                            {
                                if (par.handler && !par.handler(argv[i] + len + 1, SIZE_MAX, (char *) res + par.offset, par.context)) log_message_warning_argv(log, CODE_METRIC, str, len, i, ARGV_WARNING_UNHANDLED_PAR_LONG, argv[i] + len + 1);
                            }
                            else capture = 1;
                        }
                    }
                }
                else // Short mode
                {
                    size_t tot = strlen(argv[i] + 1);
                    for (size_t k = 1; argv[i][k];) // Inner loop for handling multiple option-like parameters
                    {
                        str = argv[i] + k;
                        uint8_t utf8_len;
                        uint32_t utf8_val; // Never used
                        if (!utf8_decode_once((uint8_t *) str, tot, &utf8_val, &utf8_len)) log_message_generic(log, CODE_METRIC, MESSAGE_ERROR, "Incorrect UTF-8 byte sequence at the command-line parameter no. %zu (byte: %zu)!\n", i, k + utf8_len + 1);
                        else
                        {
                            len = utf8_len;
                            if (!selector_shrt(&par, str, len, context)) log_message_warning_argv(log, CODE_METRIC, str, len, i, ARGV_WARNING_INVALID_PAR_SHRT, NULL);
                            else
                            {
                                if (par.option) // Parameter expects value
                                {
                                    if (par.handler && !par.handler(NULL, SIZE_MAX, (char *) res + par.offset, par.context)) log_message_warning_argv(log, CODE_METRIC, str, len, i, ARGV_WARNING_UNHANDLED_OPT_SHRT, NULL);
                                }
                                else
                                {
                                    if (str[len]) // Executing valued parameter handler
                                    {
                                        if (par.handler && !par.handler(str + len, SIZE_MAX, (char *) res + par.offset, par.context)) log_message_warning_argv(log, CODE_METRIC, str, len, i, ARGV_WARNING_UNHANDLED_PAR_SHRT, str + len);
                                    }
                                    else capture = -1;
                                    break; // Exiting from the inner loop
                                }                                
                            }
                            if ((k += len) > tot) break;
                        }                     
                    }
                }
                continue;
            }            
        }
        if (!array_test(p_input, p_input_cnt, sizeof(**p_input), 0, 0, ARG_SIZE(input_cnt, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
        else
        {
            (*p_input)[input_cnt++] = argv[i]; // Storing input file path
            continue;
        }       
        goto error;
    }
    if (capture) log_message_warning_argv(log, CODE_METRIC, str, len, argv_cnt - 1, capture > 0 ? ARGV_WARNING_MISSING_VALUE_LONG : ARGV_WARNING_MISSING_VALUE_SHRT, NULL);
    if (!array_test(p_input, p_input_cnt, sizeof(**p_input), 0, ARRAY_REDUCE, ARG_SIZE(input_cnt))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
    else return 1;

error:
    free(*p_input);
    *p_input = NULL;
    *p_input_cnt = 0;
    return 0;    
}

bool argv_par_selector_long(struct par *par, char *str, size_t len, void *Context)
{
    struct argv_par_sch *context = Context;
    size_t id = binary_search(str, context->ltag, sizeof(*context->ltag), context->ltag_cnt, len + 1 ? str_strl_stable_cmp_len : str_strl_stable_cmp, &len);
    if (id + 1)
    {
        id = context->ltag[id].id;
        if (id < context->par_cnt)
        {
            *par = context->par[id];
            return 1;
        }
    }
    return 0;
}

bool argv_par_selector_shrt(struct par *par, char *str, size_t len, void *Context)
{
    struct argv_par_sch *context = Context;
    size_t id = binary_search(str, context->stag, sizeof(*context->stag), context->stag_cnt, str_strl_stable_cmp_len, &len);
    if (id + 1)
    {
        id = context->stag[id].id;
        if (id < context->par_cnt)
        {
            *par = context->par[id];
            return 1;
        }
    }
    return 0;
}

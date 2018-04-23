#include "np.h"
#include "argv.h"
#include "memory.h"
#include "sort.h"
#include "utf8.h"

#include <string.h>
#include <stdlib.h>

DECLARE_PATH

enum argv_parse_status {
    ARGV_WARNING_MISSING_VALUE,
    ARGV_WARNING_UNHANDLED_PAR,
    ARGV_WARNING_UNEXPECTED_VALUE,
    ARGV_WARNING_INVALID_PAR,
    ARGV
};

struct message_warning_argv_parse {
    struct message base;
    enum argv_parse_status status;
    char *name;
    size_t len, ind;    
};

struct message_error_argv_parse {
    struct message base;
    size_t pos, ind;
};

#define MESSAGE_WARNING_ARGV_PARSE(Name, Len, Ind, Status) \
    ((struct message_warning_argv_parse) { \
        .base = MESSAGE(message_warning_argv_parse, MESSAGE_TYPE_WARNING), \
        .status = (Status), \
        .name = (Name), \
        .len = (Len), \
        .ind = (Ind), \
    })

size_t message_warning_argv_parse(char *buff, size_t buff_cnt, void *Context)
{
    struct message_warning_argv_parse *context = Context;
    char *str[] = { 
        "Expected a value for the command-line parameter no.",
        "Unable to handle the value of the command-line parameter no.",
        "Unused value for the command-line parameter no.",
        "Invalid command-line parameter no."
    };    
    int tmp = context->status < countof(str) ? snprintf(buff, buff_cnt, "%s %zu \"%.*s\"!\n", str[context->status], context->ind, (int) context->len, context->name) : 0;
    return MAX(0, tmp);
}

bool argv_parse(par_selector_callback selector_long, par_selector_callback selector_shrt, void *context, void *res, char **argv, size_t argv_cnt, char ***p_input, size_t *p_input_cnt, struct log *log)
{
    struct par par;
    char *str = NULL;
    size_t input_cnt = 0, len = 0;
    bool halt = 0, capture = 0;
    for (size_t i = 1; i < argv_cnt; i++)
    {
        if (!halt)
        {
            if (capture)
            {
                if (par.handler && !par.handler(argv[i], SIZE_MAX, (char *) res + par.offset, par.context))
                    log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_WARNING_UNHANDLED_PAR).base);
                capture = 0;
            }
            else if (argv[i][0] == '-')
            {
                if (argv[i][1] == '-') // Long mode
                {
                    str = argv[i] + 2;
                    if (!*str) // halt on '--'
                    {
                        halt = 1;
                        continue;
                    }
                    char *tmp = strchr(str, '=');
                    len = tmp ? (size_t) (tmp - str) : SIZE_MAX;
                    if (!selector_long(&par, str, len, context))
                        log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_WARNING_INVALID_PAR).base);
                    else
                    {
                        if (par.option)
                        {
                            if (tmp) 
                                log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_WARNING_UNEXPECTED_VALUE).base);
                            if (par.handler && !par.handler(NULL, SIZE_MAX, (char *) res + par.offset, par.context))
                                log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_WARNING_UNHANDLED_PAR).base);
                        }
                        else
                        {
                            if (tmp)
                            {
                                if (par.handler && !par.handler(argv[i] + len + 1, SIZE_MAX, (char *) res + par.offset, par.context))
                                    log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_WARNING_UNHANDLED_PAR).base);
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
                        if (!utf8_decode_once((uint8_t *) str, tot, &utf8_val, &utf8_len))
                            log_message_var(log, &MESSAGE_VAR_GENERIC(MESSAGE_TYPE_ERROR), "Incorrect UTF-8 byte sequence at command-line parameter no. %zu, character %zu!\n", i, k);
                        else
                        {
                            len = utf8_len;
                            if (!selector_shrt(&par, str, len, context))
                                log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_WARNING_INVALID_PAR).base);
                            else
                            {
                                if (par.option) // Parameter expects value
                                {
                                    if (par.handler && !par.handler(NULL, SIZE_MAX, (char *) res + par.offset, par.context))
                                        log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_WARNING_UNHANDLED_PAR).base);
                                }
                                else
                                {
                                    if (str[len]) // Executing valued parameter handler
                                    {
                                        if (par.handler && !par.handler(str + len, SIZE_MAX, (char *) res + par.offset, par.context))
                                            log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_WARNING_UNHANDLED_PAR).base);
                                    }
                                    else capture = 1;
                                    break; // We need to exit from the inner loop
                                }                                
                            }
                            if ((k += len) > tot) break;
                        }                     
                    }
                }
                continue;
            }            
        }
        if (!array_test(p_input, p_input_cnt, sizeof(**p_input), 0, 0, ARG_SIZE(input_cnt, 1))) log_message(log, &MESSAGE_ERROR_CRT(errno).base);
        else
        {
            (*p_input)[input_cnt++] = argv[i]; // Storing input file path
            continue;
        }       
        goto error;
    }
    if (capture) log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, argv_cnt - 1, ARGV_WARNING_MISSING_VALUE).base);
    if (!array_test(p_input, p_input_cnt, sizeof(**p_input), 0, ARRAY_REDUCE, ARG_SIZE(input_cnt))) log_message(log, &MESSAGE_ERROR_CRT(errno).base);
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
    size_t id = binary_search(str, context->ltag, sizeof(*context->ltag), context->ltag_cnt, len + 1 ? str_strl_cmp_len : str_strl_cmp, &len);
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
    size_t id = binary_search(str, context->stag, sizeof(*context->stag), context->stag_cnt, str_strl_cmp_len, &len);
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

#include "np.h"
#include "argv.h"
#include "memory.h"
#include "sort.h"
#include "utf8.h"

#include <string.h>
#include <stdlib.h>

DECLARE_PATH

enum argv_parse_status {
    ARGV_PARSE_SUCCESS = 0,
    ARGV_PARSE_WARNING_MISSING_VALUE,
    ARGV_PARSE_WARNING_UNHANDLED_PAR,
    ARGV_PARSE_WARNING_UNEXPECTED_VALUE,
    ARGV_PARSE_WARNING_INVALID_PAR,
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

#define MESSAGE_ERROR_ARGV_PARSE(Pos, Ind) \
    ((struct message_error_argv_parse) { \
        .base = MESSAGE(message_error_argv_parse, MESSAGE_TYPE_ERROR), \
        .pos = (Pos), \
        .ind = (Ind), \
    })

size_t message_warning_argv_parse(char *buff, size_t buff_cnt, void *Context)
{
    struct message_warning_argv_parse *context = Context;
    char *str[] = { 
        "Expected a value for the command-line parameter no.",
        "Unable to handle the command-line parameter no.",
        "Unexpected value for the command-line parameter no.",
        "Invalid command-line parameter no."
    };    
    int tmp = snprintf(buff, buff_cnt, "%s %zu \"%.*s\"!\n", str[context->status - 1], context->ind, (int) context->len, context->name);
    return MAX(0, tmp);
}

size_t message_error_argv_parse(char *buff, size_t buff_cnt, void *Context)
{
    struct message_error_argv_parse *context = Context;
    int tmp = snprintf(buff, buff_cnt, "Incorrect UTF-8 byte sequence at command-line parameter no. %zu, character %zu!\n", context->ind, context->pos);
    return MAX(0, tmp);
}

bool argv_parse(struct argv_sch *sch, void *res, char **argv, size_t argv_cnt, char ***p_input, size_t *p_input_cnt, struct log *log)
{
    char *str = NULL;
    size_t input_cnt = 0, id = 0, len = 0;
    bool halt = 0, capture = 0;
    for (size_t i = 1; i < argv_cnt; i++)
    {
        if (!halt)
        {
            if (capture)
            {
                if (!sch->par[id].handler(argv[i], SIZE_MAX, (char *) res + sch->par[id].offset, sch->par[id].context))
                    log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_PARSE_WARNING_UNHANDLED_PAR).base);
                capture = 0;
            }
            else if (argv[i][0] == '-')
            {
                if (argv[i][1] == '-') // Long mode
                {
                    char *tmp = strchr(str = argv[i] + 2, '=');
                    len = tmp ? (size_t) (tmp - str) : SIZE_MAX;
                    if (!len) // halt on '--'
                    {
                        halt = 1;
                        continue;
                    }
                    size_t j = binary_search(str, sch->ltag, sizeof(*sch->ltag), sch->ltag_cnt, tmp ? str_strl_cmp_len : str_strl_cmp, &len);
                    if (j + 1)
                    {
                        id = sch->ltag[j].id;
                        if (sch->par[id].mode & PAR_MODE_OPTION)
                        {
                            if (tmp) log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_PARSE_WARNING_UNEXPECTED_VALUE).base);
                            else
                            {
                                if (!sch->par[id].handler(NULL, SIZE_MAX, (char *) res + sch->par[id].offset, sch->par[id].context))
                                    log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_PARSE_WARNING_UNHANDLED_PAR).base);
                            }
                        }
                        else
                        {
                            if (tmp)
                            {
                                if (!sch->par[id].handler(argv[i] + len + 1, SIZE_MAX, (char *) res + sch->par[id].offset, sch->par[id].context))
                                    log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_PARSE_WARNING_UNHANDLED_PAR).base);
                            }
                            else capture = 1;
                        }
                    }
                    else log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_PARSE_WARNING_INVALID_PAR).base);
                }
                else // Short mode
                {
                    size_t tot = strlen(argv[i]);
                    for (size_t k = 1; argv[i][k];) // Inner loop for handling multiple option-like parameters
                    {
                        str = &argv[i][k];
                        size_t j = binary_search(str, sch->stag, sizeof *sch->stag, sch->stag_cnt, str_strl_cmp, NULL);
                        if (j + 1)
                        {
                            id = sch->stag[j].id;
                            len = sch->stag[j].name.len;
                            if (sch->par[id].mode == PAR_MODE_OPTION) // Parameter expects value
                            {
                                if (!sch->par[id].handler(NULL, SIZE_MAX, (char *) res + sch->par[id].offset, sch->par[id].context))
                                    log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_PARSE_WARNING_UNHANDLED_PAR).base);
                            }
                            else
                            {
                                if (str + len) // Executing valued parameter handler
                                {
                                    if (!sch->par[id].handler(argv[i] + k + len, SIZE_MAX, (char *) res + sch->par[id].offset, sch->par[id].context))
                                        log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, i, ARGV_PARSE_WARNING_UNEXPECTED_VALUE).base);
                                }
                                else capture = 1;
                                break; // We need to exit from the inner loop
                            }
                            k += len;
                        }
                        else
                        {
                            uint32_t utf8_val = 0;
                            uint8_t utf8_context = 0, utf8_len;
                            for (; k < tot; k++)
                            {
                                if (!utf8_decode(argv[i][k], &utf8_val, NULL, &utf8_len, &utf8_context)) log_message(log, &MESSAGE_ERROR_ARGV_PARSE(k, id).base);
                                else
                                {
                                    if (utf8_context) continue;
                                    break;
                                }
                                goto error;
                            }
                            log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, utf8_len, i, ARGV_PARSE_WARNING_INVALID_PAR).base);
                        }
                        if (k >= tot) break;
                    }
                } 
            }
            continue;
        }
        if (!array_test(p_input, p_input_cnt, sizeof(**p_input), 0, 0, ARG_SIZE(input_cnt, 1))) log_message(log, &MESSAGE_ERROR_CRT(errno).base);
        else
        {
            (*p_input)[input_cnt++] = argv[i]; // Storing input file path
            continue;
        }       
        goto error;
    }
    if (capture) log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, argv_cnt - 1, ARGV_PARSE_WARNING_MISSING_VALUE).base);
    if (!array_test(p_input, p_input_cnt, sizeof(**p_input), 0, ARRAY_REDUCE, ARG_SIZE(input_cnt))) log_message(log, &MESSAGE_ERROR_CRT(errno).base);
    else return 1;

error:
    free(p_input);
    *p_input = NULL;
    *p_input_cnt = 0;
    return 0;    
}

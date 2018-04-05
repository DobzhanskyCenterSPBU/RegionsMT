#include "argv.h"
#include "memory.h"
#include "sort.h"

#include <string.h>

DECLARE_PATH

enum argv_parse_status {
    ARGV_PARSE_SUCCESS = 0,
    ARGV_PARSE_WARNING_MISSING_VALUE,
    ARGV_PARSE_WARNING_UNHANDLED_PAR,
    ARGV_PARSE_WARNING_UNEXPECTED_VALUE,
    ARGV_PARSE_WARNING_INVALID_PAR,
};

struct message_warning_argv_parse {
    struct message base;
    enum argv_parse_status status;
    char *name;
    size_t len, id;    
};

#define MESSAGE_WARNING_ARGV_PARSE(Name, Len, Id, Status) \
    ((struct message_warning_argv_parse) { \
        .base = MESSAGE(message_warning_argv_parse, MESSAGE_TYPE_WARNING), \
        .status = (Status), \
        .name = (Name), \
        .len = (Len), \
        .id = (Id), \
    })

size_t message_warning_argv_parse(char *buff, size_t buff_cnt, void *Context)
{
    struct message_warning_argv_parse *context = Context;
    char *str;
    
    switch (context->status)
    {
    case ARGV_PARSE_WARNING_MISSING_VALUE_SHRT:
        str = "Expected a value for the command-line parameter";
        break;
    case ARGV_PARSE_WARNING_UNHANDLED_PAR_SHRT:
    case ARGV_PARSE_WARNING_UNHANDLED_PAR_LONG:
        str = "Unable to handle the command-line parameter";
        break;
    case ARGV_PARSE_WARNING_UNEXPECTED_VALUE_SHRT:
    case ARGV_PARSE_WARNING_UNEXPECTED_VALUE_LONG:
        str = "Unexpected value for the command-line parameter";
        break;
    case ARGV_PARSE_WARNING_INVALID_PAR_SHRT:
    case ARGV_PARSE_WARNING_INVALID_PAR_LONG:
        str = "Invalid command-line parameter";;
        break;

    
    case ARGV_PARSE_WARNING_EXPVAL_SHRT:
        
    case ARGV_PARSE_WARNING_EXPVAL_LONG:
        str = "No value is provided for the command-line parameter";
        break;
    case ARGV_PARSE_WARNING_INVVAL:
        str = "Unable to handle the value";
        break;
    case ARGV_PARSE_WARNING_INVSYM:
        
        str = "Unexpected symbol";
        break;
    case ARGV_PARSE_WARNING_INVSTCK:
        
        str = "Unexpected command-line parameter";
        break;
    default:
        return SIZE_MAX; // Do not print any messages
    }

    int tmp;
    switch (context->status)
    {
    case ARGV_PARSE_WARNING_MISSING_VALUE_SHRT:
    case ARGV_PARSE_WARNING_UNHANDLED_PAR_SHRT:
    case ARGV_PARSE_WARNING_UNEXPECTED_VALUE_SHRT:
        tmp = snprintf(buff, buff_cnt, "%s %c%.*s%c (arg. no. %zu)!\n", str, (int) context->len, context->name->str, context->ind);


    case ARGV_PARSE_WARNING_MISSING_VALUE_LONG:
    case ARGV_PARSE_WARNING_UNHANDLED_PAR_LONG:
    case ARGV_PARSE_WARNING_UNEXPECTED_VALUE_LONG:

    case ARGV_PARSE_WARNING_INVALID_PAR_SHRT:
        //quote = '\'';
    default:
        //quote = '\"';
        break;
    }


    int tmp = 
        context->len ? snprintf(buff, buff_cnt, "%s %c%.*s%c (arg. no. %zu)!\n", str, quote, (int) context->len, context->argv[context->ind] + context->off, quote, context->ind) :
        snprintf(buff, buff_cnt, "%s %c%s%c (arg. no. %zu)!\n", str, quote, context->argv[context->ind] + context->off, quote, context->ind);
    
    return MAX(0, tmp);
}

bool argv_parse(struct argv_sch *sch, void *res, char **argv, size_t argv_cnt, char ***p_input, size_t *p_input_cnt, struct log *log, struct queue *message_queue)
{
    char *str = NULL;
    size_t input_cnt = 0, id = 0, len = 0;
    bool halt = 0, capture = 0;
    enum { 
        CAPTURE_NONE = 0, 
        CAPTURE_SHRT,
        CAPTURE_LONG,
    } capture10 = 0;
    for (size_t i = 1; i < argv_cnt; i++)
    {
        if (!halt)
        {
            /*switch (capture)
            {
            case CAPTURE_STRICT_SHRT:
                if (!argv_parse_message_queue_enqueue(&MESSAGE_WARNING_ARGV_PARSE(argv, i - 1, 2, 0, ARGV_PARSE_WARNING_MISSING_VALUE_SHRT), log, message_queue)) return 0;
                break;
            case CAPTURE_STRICT_LONG:
                log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i - 1, 2, 0, ARGV_PARSE_WARNING_MISSING_VALUE_LONG).base);
                break;
            case CAPTURE_OPTIONAL_SHRT:

                break;
            case CAPTURE_OPTIONAL_LONG:
                if (!sch->par[prev_id].handler(NULL, 0, (char *)res + sch->par[prev_id].offset, sch->par[prev_id].context))
                    log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i - 1, 2, 0, ARGV_PARSE_WARNING_UNHANDLED_PAR_LONG).base);
                break;
            default:
                break;
            }*/
            if (capture)
            {
                if (!sch->par[id].handler(argv[i], 0, (char *) res + sch->par[id].offset, sch->par[id].context))
                    log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, id, ARGV_PARSE_WARNING_INVVAL).base);
                capture = CAPTURE_NONE;
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
                            if (tmp) log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, id, ARGV_PARSE_WARNING_UNEXPECTED_VALUE).base);
                            else
                            {
                                if (!sch->par[id].handler(NULL, 0, (char *) res + sch->par[id].offset, sch->par[id].context))
                                    log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, id, ARGV_PARSE_WARNING_UNHANDLED_PAR).base);
                            }
                        }
                        else
                        {
                            if (tmp)
                            {
                                if (!sch->par[id].handler(argv[i] + len + 1, 0, (char *) res + sch->par[id].offset, sch->par[id].context))
                                    log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, id, ARGV_PARSE_WARNING_UNHANDLED_PAR).base);
                            }
                            else capture = 1;
                        }
                    }
                    else log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, id, ARGV_PARSE_WARNING_INVALID_PAR).base);
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
                            if (sch->par[id].mode == PAR_MODE_VALUE) // Parameter expects value
                            {
                                if (str + len) // Executing valued parameter handler
                                {
                                    if (!sch->par[id].handler(argv[i] + k + len, 0, (char *) res + sch->par[id].offset, sch->par[id].context))
                                        log_message(log, &MESSAGE_WARNING_ARGV_PARSE(str, len, id, ARGV_PARSE_WARNING_UNEXPECTED_VALUE).base);
                                }
                                else capture = 1;
                                break; // We need to exit from the inner loop
                            }
                            else sch->par[id].handler(NULL, 0, (char *)res + sch->par[id].offset, sch->par[id].context);
                        }
                        else log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i, k, 0, ARGV_PARSE_WARNING_INVPAR_SHRT).base);
                        k += len;
                        if (k >= tot) break;
                    }
                }
            
                continue;
            }
        }
        if (!array_test(p_input, p_input_cnt, sizeof(**p_input), 0, ARRAY_REDUCE, ARG_SIZE(input_cnt, 1))) log_message(log, &MESSAGE_ERROR_CRT(errno).base);
        else
        {
            (*p_input)[input_cnt++] = argv[i]; // Storing input file path
            continue;
        }
        return 0;
    }



    if (!array_test(p_input, p_input_cnt, sizeof(**p_input), 0, ARRAY_REDUCE, ARG_SIZE(input_cnt))) log_message(log, &MESSAGE_ERROR_CRT(errno).base);
    else return 1;
    return 0;    
}

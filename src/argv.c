#include "argv.h"
#include "memory.h"
#include "sort.h"

#include <string.h>

DECLARE_PATH

enum argv_parse_status {
    ARGV_PARSE_SUCCESS = 0,
    ARGV_PARSE_WARNING_MISSING_VALUE_SHRT,
    ARGV_PARSE_WARNING_MISSING_VALUE_LONG,
    ARGV_PARSE_WARNING_UNHANDLED_PAR_SHRT,
    ARGV_PARSE_WARNING_UNHANDLED_PAR_LONG,
    ARGV_PARSE_WARNING_UNEXPECTED_VALUE_SHRT,
    ARGV_PARSE_WARNING_UNEXPECTED_VALUE_LONG,
    ARGV_PARSE_WARNING_INVALID_PAR_SHRT,
    ARGV_PARSE_WARNING_INVALID_PAR_LONG,    

    ARGV_PARSE_WARNING_INVPAR_LONG,
    ARGV_PARSE_WARNING_INVPAR_SHRT,
    ARGV_PARSE_WARNING_EXPVAL_LONG,
    ARGV_PARSE_WARNING_EXPVAL_SHRT,
    
    ARGV_PARSE_WARNING_INVVAL,
    ARGV_PARSE_WARNING_INVSYM,
    ARGV_PARSE_WARNING_INVSTCK
};

struct message_warning_argv_parse {
    struct message base;
    enum argv_parse_status status;
    char *name;
    size_t len, ind;    
};

#define MESSAGE_WARNING_ARGV_PARSE(Argv, Ind, Off, Len, Status) \
    ((struct message_warning_argv_parse) { \
        .base = MESSAGE(message_warning_argv_parse, MESSAGE_TYPE_WARNING), \
        .status = (Status), \
        .argv = (Argv), \
        .ind = (Ind), \
        .off = (Off), \
        .len = (Len), \
    })

size_t message_warning_argv_parse(char *buff, size_t buff_cnt, struct message_warning_argv_parse *context)
{
    char *str;
    
    switch (context->status)
    {
    case ARGV_PARSE_WARNING_MISSING_VALUE_SHRT:
    case ARGV_PARSE_WARNING_MISSING_VALUE_LONG:
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
        tmp = snprintf(buff, buff_cnt, "%s %c%.*s%c (arg. no. %zu)!\n", str, (int) context->name->len, context->name->str, context->ind);


    case ARGV_PARSE_WARNING_MISSING_VALUE_LONG:
    case ARGV_PARSE_WARNING_UNHANDLED_PAR_LONG:
    case ARGV_PARSE_WARNING_UNEXPECTED_VALUE_LONG:

    case ARGV_PARSE_WARNING_INVALID_PAR_SHRT:
        quote = '\'';
    default:
        quote = '\"';
        break;
    }


    int tmp = 
        context->len ? snprintf(buff, buff_cnt, "%s %c%.*s%c (arg. no. %zu)!\n", str, quote, (int) context->len, context->argv[context->ind] + context->off, quote, context->ind) :
        snprintf(buff, buff_cnt, "%s %c%s%c (arg. no. %zu)!\n", str, quote, context->argv[context->ind] + context->off, quote, context->ind);
    
    return MAX(0, tmp);
}

bool argv_parse(struct argv_sch *sch, void *res, char **argv, size_t argv_cnt, char ***p_input, size_t *p_input_cap, struct log *log)
{
    
    size_t input_cnt = 0, prev_id = 0;
    bool stop = 0;
    enum { 
        CAPTURE_NONE = 0, 
        CAPTURE_STRICT_SHRT,
        CAPTURE_STRICT_LONG,
        CAPTURE_OPTIONAL_SHRT,
        CAPTURE_OPTIONAL_LONG,
    } capture = 0;

    for (size_t i = 1; i < argv_cnt; i++)
    {
        if (!stop && argv[i][0] == '-')
        {
            switch (capture)
            {
            case CAPTURE_STRICT_SHRT:
                log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i - 1, 2, 0, ARGV_PARSE_WARNING_MISSING_VALUE_SHRT).base);
                break;
            case CAPTURE_STRICT_LONG:
                log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i - 1, 2, 0, ARGV_PARSE_WARNING_MISSING_VALUE_LONG).base);
                break;
            case CAPTURE_OPTIONAL_SHRT:

                break;
            case CAPTURE_OPTIONAL_LONG:
                if (!sch->par[prev_id].handler(NULL, 0, (char *) res + sch->par[prev_id].offset, sch->par[prev_id].context))
                    log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i - 1, 2, 0, ARGV_PARSE_WARNING_UNHANDLED_PAR_LONG).base);
                break;
            default:
                break;
            }

            if (capture) capture = CAPTURE_NONE;
                
            if (argv[i][1] == '-') // Long mode
            {
                char *str = argv[i] + 2, *tmp = strchr(str, '=');
                size_t len = (size_t) (tmp - str);
                if (!len) // Stop on '--'
                {
                    stop = 1;
                    continue;
                }
                size_t j = binary_search(str, sch->ltag, sizeof(*sch->ltag), sch->ltag_cnt, tmp ? (stable_cmp_callback) str_strl_cmp_len : (stable_cmp_callback) str_strl_cmp, &len);
                if (j + 1)
                {
                    size_t id = sch->ltag[j].id;
                    if (sch->par[id].mode & PAR_MODE_OPTION_ONLY)
                    {
                        if (tmp) log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i, 2, len - 2, ARGV_PARSE_WARNING_UNEXPECTED_VALUE_LONG).base);
                        else
                        {
                            if (!sch->par[id].handler(NULL, 0, (char *) res + sch->par[id].offset, sch->par[id].context))
                                log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i, 2, 0, ARGV_PARSE_WARNING_UNHANDLED_PAR_LONG).base);
                        }
                    }
                    else
                    {
                        if (tmp)
                        {
                            if (!sch->par[id].handler(argv[i] + len + 1, 0, (char *) res + sch->par[id].offset, sch->par[id].context))
                                log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i, 2, len - 2, ARGV_PARSE_WARNING_UNHANDLED_PAR_LONG).base);
                        }
                        else
                        {
                            capture = sch->par[id].mode & PAR_MODE_VALUE_ONLY ? CAPTURE_STRICT_LONG : CAPTURE_OPTIONAL_LONG;
                            prev_id = id;
                        }
                    }
                }
                else log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i, 2, len - 2, ARGV_PARSE_WARNING_INVALID_PAR_LONG).base);
            }
            else // Short mode
            {
                size_t len = strlen(argv[i]);
                for (size_t k = 1; argv[i][k];) // Inner loop for handling multiple option-like parameters
                {
                    size_t j = binary_search(&argv[i][k], sch->stag, sizeof *sch->stag, sch->stag_cnt, (stable_cmp_callback) str_strl_cmp, NULL);
                    if (j + 1)
                    {
                        size_t id = sch->stag[j].id;
                        if (sch->par[id].mode == PAR_MODE_VALUE_ONLY) // Parameter expects value
                        {
                            if (k == 1) // Valued parameter can't be stacked
                            {
                                if (argv[i][2]) // Executing valued parameter handler
                                {
                                    if (!sch->par[id].handler(argv[i] + 2, 0, (char *) res + sch->par[id].offset, sch->par[id].context))
                                        log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i, 2, 0, ARGV_PARSE_WARNING_INVVAL).base);
                                }
                                else if (i + 1 < argv_cnt) capture = -1, prev_id = id; // Parameter value is separated by whitespace
                                else log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i, k, 0, ARGV_PARSE_WARNING_EXPVAL_SHRT).base);
                                break; // We need to exit from the inner loop
                            }
                            else log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i, k, 0, ARGV_PARSE_WARNING_INVSTCK).base);
                        }
                        else sch->par[id].handler(NULL, 0, (char *) res + sch->par[id].offset, sch->par[id].context);
                    }
                    else log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i, k, 0, ARGV_PARSE_WARNING_INVPAR_SHRT).base);
                    k += sch->stag[j].name.len;
                    if (k >= len) break;
                }
            }
        }
        else
        {
            if (capture) // Capturing parameters separated by whitespace with corresponding title
            {
                if (!sch->par[prev_id].handler(argv[i], 0, (char *) res + sch->par[prev_id].offset, sch->par[prev_id].context))
                    log_message(log, &MESSAGE_WARNING_ARGV_PARSE(argv, i, 0, 0, ARGV_PARSE_WARNING_INVVAL).base);
                capture = 0;
            }
            else
            {
                if (!array_resize((void **) p_input, p_input_cap,  sizeof (**p_input), 0, ARRAY_RESIZE_EXTEND_ONLY, ARG_S(input_cnt, 1)))
                    log_message(log, &MESSAGE_ERROR_CRT(errno).base);
                else
                {
                    (*p_input)[input_cnt++] = argv[i]; // Storing input files
                    continue;
                }
                return 0;
            }
        }
    }

    if (!array_resize((void **) p_input, p_input_cap, sizeof(**p_input), 0, ARRAY_RESIZE_REDUCE_ONLY, ARG_S(input_cnt))) 
        log_message(log, &MESSAGE_ERROR_CRT(errno).base);
    else return 1;
    return 0;
}
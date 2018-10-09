#include "np.h"
#include "common.h"
#include "memory.h"
#include "object.h"
#include "sort.h"
#include "ll.h"
#include "utf8.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

DECLARE_PATH

struct program_object
{
    struct program_object_node
    {
        void *context;
        prologue_callback prologue;
        epilogue_callback epilogue;
        disposer_callback dispose;
        struct {
            ptrdiff_t *text_off; // Offset in the text array
            size_t *text_len; // Length of the text chunk
            struct program_object_node *dsc;
            size_t dsc_cnt;
        };
    };
    char *text;
};

// Program object destuctor makes use of stack-less recursion
void program_object_dispose(struct program_object *obj)
{
    if (!obj) return;
    struct program_object_node *root = (struct program_object_node *) obj;
    for (struct program_object_node *prev = NULL;;)
    {
        if (root->dsc_cnt)
        {
            struct program_object_node *temp = root->dsc + root->dsc_cnt - 1;
            root->dsc = prev;
            prev = root;
            root = temp;
        }
        else
        {
            free(root->text_off);
            free(root->text_len);
            root->dispose(root->context);
            if (!prev) break;
            if (--prev->dsc_cnt) root--;
            else
            {
                struct program_object_node *temp = prev->dsc;
                free(root);
                root = prev;
                prev = temp;
            }
        }
    }
    free(obj->text);
    free(obj);
}

bool program_object_execute(struct program_object *obj, void *in)
{
    bool res = 1;
    void *temp;
    res &= obj->prologue(in, &temp, obj->context);
    for (size_t i = 0; res && i < obj->dsc_cnt; res &= program_object_execute(obj->dsc + i++, temp));
    res &= obj->epilogue(in, temp, obj->context);
    return res;
}

///////////////////////////////////////////////////////////////////////////////

enum xml_status {
    XML_ERROR_UTF,
    XML_ERROR_HEADER,
    XML_ERROR_ROOT,
    XML_ERROR_COMPILER,
    XML_ERROR_CHAR_UNEXPECTED_EOF,
    XML_ERROR_CHAR_INVALID_SYMBOL,
    XML_ERROR_STR_UNEXPECTED_TAG,
    XML_ERROR_STR_UNEXPECTED_ATTRIBUTE,
    XML_ERROR_STR_ENDING,
    XML_ERROR_STR_DUPLICATED_ATTRIBUTE,
    XML_ERROR_STR_UNHANDLED_VALUE,
    XML_ERROR_STR_CONTROL,
    XML_ERROR_VAL_RANGE
};

struct text_metric {
    uint64_t byte;
    size_t col, line;
};

struct xml_context {
    struct text_metric *text_metric;
    const char *path;
    char *str;
    uint32_t val;
    size_t len;
    enum xml_status status;
};

static bool message_xml(char *buff, size_t *p_buff_cnt, void *Context)
{
    struct xml_context *restrict context = Context;
    const char *str[] = {
        "Incorrect UTF-8 byte sequence",
        "Invalid XML header",
        "No root element found",
        "Compiler malfunction",
        "Unexpected end of file",
        "Invalid character",
        "Unexpected tag",
        "Unexpected attribute",
        "Unexpected closing tag",
        "Duplicated attribute",
        "Unable to handle value",
        "Invalid control sequence"
    };
    size_t cnt = 0, len = *p_buff_cnt, col_disp = 0, byte_disp = 0;
    for (unsigned i = 0; i < 2; i++)
    {
        int tmp = -1;
        switch (i)
        {
        case 0:
            switch (context->status)
            {
            case XML_ERROR_UTF:
            case XML_ERROR_HEADER:
            case XML_ERROR_ROOT:
            case XML_ERROR_COMPILER:
                tmp = snprintf(buff + cnt, len, "%s", str[context->status]);
                break;
            case XML_ERROR_CHAR_UNEXPECTED_EOF:
            case XML_ERROR_CHAR_INVALID_SYMBOL:
                byte_disp = context->len, col_disp = 1; // Returning to the previous symbol
                tmp = snprintf(buff + cnt, len, "%s \'%.*s\'", str[context->status], INTP(context->len), context->str);
                break;
            case XML_ERROR_STR_UNEXPECTED_TAG:
            case XML_ERROR_STR_UNEXPECTED_ATTRIBUTE:
            case XML_ERROR_STR_DUPLICATED_ATTRIBUTE:
            case XML_ERROR_STR_ENDING:
            case XML_ERROR_STR_UNHANDLED_VALUE:
            case XML_ERROR_STR_CONTROL:
                tmp = snprintf(buff + cnt, len, "%s \"%.*s\"", str[context->status], INTP(context->len), context->str);
                break;
            case XML_ERROR_VAL_RANGE:
                tmp = snprintf(buff + cnt, len, "Numeric value %" PRIu32 " is out of range", context->val);
                break;
            }
        case 1:
            tmp = snprintf(buff + cnt, len, " (file: \"%s\"; line: %zu; character: %zu; byte: %" PRIu64 ")!\n",
                context->path,
                context->text_metric->line + 1,
                context->text_metric->col - col_disp + 1,
                context->text_metric->byte - byte_disp + 1
            );
        }
        if (tmp < 0) return 0;
        cnt = size_add_sat(cnt, (size_t) tmp);
        len = size_sub_sat(len, (size_t) tmp);
    }
    *p_buff_cnt = cnt;
    return 1;
}

static bool log_message_error_xml(struct log *restrict log, struct code_metric code_metric, struct text_metric *restrict text_metric, const char *path, enum xml_status status)
{
    return log_message(log, code_metric, MESSAGE_ERROR, message_xml, &(struct xml_context) {.text_metric = text_metric, .path = path, .status = status });
}

static bool log_message_error_str_xml(struct log *restrict log, struct code_metric code_metric, struct text_metric *restrict text_metric, const char *path, char *str, size_t len, enum xml_status status)
{
    return log_message(log, code_metric, MESSAGE_ERROR, message_xml, &(struct xml_context) { .text_metric = text_metric, .path = path, .str = str, .len = len, .status = status });
}

static bool log_message_error_char_xml(struct log *restrict log, struct code_metric code_metric, struct text_metric *restrict text_metric, const char *path, uint8_t *utf8_byte, size_t utf8_len, enum xml_status status)
{
    return log_message(log, code_metric, MESSAGE_ERROR, message_xml, &(struct xml_context) {.text_metric = text_metric, .path = path, .str = (char *) utf8_byte, .len = utf8_len, .status = status });
}

static bool log_message_error_val_xml(struct log *restrict log, struct code_metric code_metric, struct text_metric *restrict text_metric, const char *path, uint32_t val, enum xml_status status)
{
    return log_message(log, code_metric, MESSAGE_ERROR, message_xml, &(struct xml_context) {.text_metric = text_metric, .path = path, .val = val, .status = status });
}

///////////////////////////////////////////////////////////////////////////////
//
//  XML syntax analyzer
//

// This is ONLY required for the line marked with '(*)'
_Static_assert(BLOCK_READ > 2, "'BLOCK_READ' constant is assumed to be greater than 2!");

#define MAX_PRINT 16 

#define XML_HEADER_BEGINNING "<?xml " 
#define XML_HEADER_VERSION "version" 
#define XML_HEADER_VERSION_VALUE "1.0"
#define XML_HEADER_ENCODING "encoding"
#define XML_HEADER_ENCODING_VALUE "UTF-8"
#define XML_HEADER_STANDALODE "standalone"
#define XML_HEADER_STANDALODE_VALUE "no"
#define XML_HEADER_ENDING "?>"

enum proc_state {
    PROC_FAIL = 0,
    PROC_CONTINUE = 1,
    PROC_COMPLETE
};

// 'metric' and 'st' are public variables and should be properly initialized 
struct ctrl_context {
    struct text_metric metric;
    uint32_t st, val;
    struct {
        char *buff;
        size_t len, cap;
    };
};

static enum proc_state xml_ctrl_impl(struct ctrl_context *context, uint8_t *utf8_byte, uint32_t utf8_val, uint8_t utf8_len, char **p_buff, size_t *p_len, size_t *p_cap, const char *path, struct log *log)
{
    const struct { struct strl name; struct strl subs; } ctrl_subs[] = {
        { STRI("amp"), STRI("&") },
        { STRI("apos"), STRI("\'") },
        { STRI("gt"), STRI(">") },
        { STRI("lt"), STRI("<") },
        { STRI("quot"), STRI("\"") }
    };
    
    for (;;) switch (context->st)
    {
    case 0:
        if (utf8_val == '#')
        {
            context->metric.col++, context->metric.byte++, context->st++;
            return 1;
        }
        context->len = 0, context->st = 7;
        continue;

    case 1:
        if (utf8_val == 'x')
        {
            context->metric.col++, context->metric.byte++, context->st++;
            return 1;
        }
        context->st = 4;
        continue;

    case 2:
        if ('0' <= utf8_val && utf8_val <= '9')
        {
            context->val = utf8_val - '0', context->st++;
            return 1;
        }
        else if ('A' <= utf8_val && utf8_val <= 'F')
        {
            context->val = utf8_val - 'A' + 10, context->st++;
            return 1;
        }
        else if ('a' <= utf8_val && utf8_val <= 'f')
        {
            context->val = utf8_val - 'a' + 10, context->st++;
            return 1;
        }
        log_message_error_char_xml(log, CODE_METRIC, &context->metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
        return 0;

    case 3:
        if ('0' <= utf8_val && utf8_val <= '9')
        {
            if (uint32_fused_mul_add(&context->val, 16, utf8_val - '0')) log_message_error_val_xml(log, CODE_METRIC, &context->metric, path, utf8_val, XML_ERROR_VAL_RANGE);
            else return 1;
        }
        else if ('A' <= utf8_val && utf8_val <= 'F')
        {
            if (uint32_fused_mul_add(&context->val, 16, utf8_val - 'A' + 10)) log_message_error_val_xml(log, CODE_METRIC, &context->metric, path, utf8_val, XML_ERROR_VAL_RANGE);
            else return 1;
        }
        else if ('a' <= utf8_val && utf8_val <= 'f')
        {
            if (uint32_fused_mul_add(&context->val, 16, utf8_val - 'a' + 10)) log_message_error_val_xml(log, CODE_METRIC, &context->metric, path, utf8_val, XML_ERROR_VAL_RANGE);
            else return 1;
        }
        else
        {
            context->st = 6;
            continue;
        }
        return 0;

    case 4:
        if ('0' <= utf8_val && utf8_val <= '9')
        {
            context->val = utf8_val - '0', context->st++;
            return 1;
        }
        log_message_error_char_xml(log, CODE_METRIC, &context->metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
        return 0;

    case 5:
        if ('0' <= utf8_val && utf8_val <= '9')
        {
            if (uint32_fused_mul_add(&context->val, 10, utf8_val - '0')) log_message_error_val_xml(log, CODE_METRIC, &context->metric, path, utf8_val, XML_ERROR_VAL_RANGE);
            else return 1;
        }
        else
        {
            context->st++;
            continue;
        }
        return 0;

    case 6:
        if (utf8_val == ';')
        {
            if (context->val >= UTF8_BOUND) log_message_error_val_xml(log, CODE_METRIC, &context->metric, path, utf8_val, XML_ERROR_VAL_RANGE);
            else
            {
                uint8_t ctrl_byte[UTF8_COUNT], ctrl_len;
                size_t len = *p_len;
                utf8_encode(context->val, ctrl_byte, &ctrl_len);
                if (!array_test(p_buff, p_cap, sizeof(**p_buff), 0, 0, ARG_SIZE(len, ctrl_len, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                else
                {
                    strncpy(*p_buff + len, (char *) ctrl_byte, ctrl_len);
                    *p_len = len + ctrl_len;
                    return PROC_COMPLETE;
                }
            }
        }
        else log_message_error_char_xml(log, CODE_METRIC, &context->metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
        return 0;

    case 7:
        if ((context->len ? utf8_is_xml_name_char : utf8_is_xml_name_start_char)(utf8_val, utf8_len))
        {
            if (!array_test(&context->buff, &context->cap, sizeof(*context->buff), 0, 0, ARG_SIZE(context->len, utf8_len))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
            else
            {
                strncpy(context->buff, (char *) utf8_byte, utf8_len);
                context->len += utf8_len;
                return 1;
            }
        }
        else if (context->len)
        {
            context->st++;
            continue;
        }
        else log_message_error_char_xml(log, CODE_METRIC, &context->metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
        return 0;

    case 8:
        if (utf8_val == ';')
        {
            size_t ctrl_ind = binary_search(context->buff, ctrl_subs, sizeof(*ctrl_subs), countof(ctrl_subs), str_strl_stable_cmp_len, &context->len);
            if (!(ctrl_ind + 1)) log_message_error_str_xml(log, CODE_METRIC, &context->metric, path, context->buff, context->len, XML_ERROR_STR_CONTROL);
            else
            {
                struct strl subs = ctrl_subs[ctrl_ind].subs;
                size_t len = *p_len;
                if (!array_test(p_buff, p_cap, sizeof(*p_buff), 0, 0, ARG_SIZE(len, subs.len + 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                else
                {
                    strncpy(*p_buff + len, subs.str, subs.len);
                    *p_len = len + subs.len;
                    return PROC_COMPLETE;
                }
            }
        }
        else log_message_error_char_xml(log, CODE_METRIC, &context->metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
        return 0;
    } 
}

struct comment_context {
    size_t len;
    uint32_t st;
};

static enum proc_state xml_comment_impl(struct comment_context *context, uint8_t *utf8_byte, uint32_t utf8_val, uint8_t utf8_len, struct text_metric *metric, const char *path, struct log *log)
{
    for (;;) switch (context->st)
    {
    case 0:
        context->len = 0, context->st++;
        continue;

    case 1:
        if (utf8_val == '-')
        {
            context->st++;
            return 1;
        }
        log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
        return 0;

    case 2:
        if (context->len == 2)
        {
            context->st++;
            continue;
        }
        else if (utf8_val == '-') context->len++;
        else context->len = 0;
        return 1;

    case 3:
        if (utf8_val == '>') return PROC_COMPLETE;
        log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
        return 0;
    }
}

struct program_object *program_object_from_xml(const char *path, xml_node_selector_callback xml_node_selector, xml_att_selector_callback xml_att_selector, void *context, struct log *log)
{
    FILE *f = NULL;
    if (path)
    {
        f = fopen(path, "r");
        if (!f)
        {
            log_message_fopen(log, CODE_METRIC, MESSAGE_ERROR, path, errno); 
            return 0;
        }
    }
    else f = stdin;
    
    struct { char *buff; size_t cap; } temp = { 0 }, name = { 0 };
    struct { uint8_t *buff; size_t cap; } attb = { 0 };
    struct { struct frame { struct program_object *obj; size_t off, len, dsc_cap; } *frame; size_t cap; } stack = { 0 };
    
    struct ctrl_context ctrl_context = { 0 };
    struct comment_context comment_context = { 0 };

    uint8_t quot = 0;
    size_t len = 0, ind = 0, dep = 0;
    char buff[BLOCK_READ], *text = NULL;
    uint8_t utf8_byte[UTF8_COUNT];
    struct text_metric txt = { 0 }, str = { 0 }; // Text metrics        
    struct xml_att xml_att = { 0 };
    struct xml_node xml_node = { 0 };
    
    size_t rd = fread(buff, 1, sizeof(buff), f), pos = 0;    
    if (rd >= 3 && !strncmp(buff, "\xef\xbb\xbf", 3)) pos += 3, txt.byte += 3; // (*) Reading UTF-8 BOM if it is present
    
    enum {
        ST_HEADER_A = 0, 
        ST_WHITESPACE_A, 
        ST_HEADER_B, 
        ST_WHITESPACE_B, 
        ST_EQUALS_A, 
        ST_WHITESPACE_C, 
        ST_QUOTE_OPENING_A,       
        ST_QUOTE_CLOSING_A, 
        ST_HEADER_HANDLING_A, 
        ST_WHITESPACE_D, 
        ST_HEADER_ENDING_A,
        ST_HEADER_C, 
        ST_WHITESPACE_E, 
        ST_EQUALS_B, 
        ST_WHITESPACE_F, 
        ST_QUOTE_OPENING_B,
        ST_QUOTE_CLOSING_B, 
        ST_HEADER_HANDLING_B, 
        ST_WHITESPACE_G, 
        ST_HEADER_ENDING_B,
        ST_HEADER_D, 
        ST_WHITESPACE_H, 
        ST_EQUALS_C, 
        ST_WHITESPACE_I,
        ST_QUOTE_OPENING_C, 
        ST_QUOTE_CLOSING_C, 
        ST_HEADER_HANDLING_C, 
        ST_WHITESPACE_J, 
        ST_HEADER_ENDING_C,
        ST_HEADER_E, 
        ST_HEADER_ENDING_D, 
        ST_HEADER_ENDING_E,
        ST_WHITESPACE_K,
        ST_ANGLE_LEFT_A,
        ST_TAG_BEGINNING_A,
        ST_NAME_A, 
        ST_TAG_ROOT_A,
        ST_WHITESPACE_L,
        ST_TAG_ENDING_A,       
        ST_TAG_ENDING_B,        
        ST_NAME_B, 
        ST_ATTRIBUTE_A, 
        ST_WHITESPACE_M, 
        ST_EQUALS_D, 
        ST_WHITESPACE_N, 
        ST_QUOTE_OPENING_D, 
        ST_QUOTE_CLOSING_D, 
        ST_ATTRIBUTE_HANDLING_A,       
        ST_TEXT_A,
        ST_TAG_BEGINNING_B, 
        ST_NAME_C, 
        ST_TAG_A, 
        ST_NAME_D, 
        ST_CLOSING_TAG_A, 
        ST_TAG_ENDING_C,        
        ST_WHITESPACE_O, 
        ST_ANGLE_LEFT_B,
        ST_TAG_BEGINNING_C,     
        ST_SPECIAL_A,
        ST_SPECIAL_B, 
        ST_SPECIAL_C, 
        ST_SPECIAL_D, 
        ST_COMMENT_A,
        ST_COMMENT_B,
        ST_COMMENT_C,
        ST_COMMENT_D,
        ST_COMMENT_E,
        ST_COMMENT_F
    };
    
    uint8_t utf8_len = 0, utf8_context = 0;
    uint32_t utf8_val = 0;
    bool halt = 0, root = 0;
    uint32_t st = ST_HEADER_A;

    for (bool upd = 1; !halt && rd; rd = fread(buff, 1, sizeof buff, f), pos = 0) while (!halt)
    {
        if (upd) // UTF-8 decoder coroutine
        {
            if (pos >= rd) break;
            if (utf8_decode(buff[pos], &utf8_val, utf8_byte, &utf8_len, &utf8_context))
            {
                pos++, txt.byte++;
                if (utf8_context) continue;
                if (utf8_is_invalid(utf8_val, utf8_len)) log_message_error_xml(log, CODE_METRIC, &txt, path, XML_ERROR_UTF), halt = 1;
                else if (utf8_val == '\n') txt.line++, txt.col = 0; // Updating text metrics
                else txt.col++;
            }
            else log_message_error_xml(log, CODE_METRIC, &txt, path, XML_ERROR_UTF), halt = 1;
        }
        else upd = 1;

        if (halt) break;

        switch (st)
        {
            ///////////////////////////////////////////////////////////////
            //
            //  XML header handling
            //

        case ST_HEADER_A:
        case ST_HEADER_B:
        case ST_HEADER_C:
        case ST_HEADER_D:
        case ST_HEADER_E:
            if (utf8_val == (uint32_t) (XML_HEADER_BEGINNING XML_HEADER_VERSION XML_HEADER_ENCODING XML_HEADER_STANDALODE)[ind])
            {
                switch (++ind)
                {
                case strlenof(XML_HEADER_BEGINNING):
                case strlenof(XML_HEADER_BEGINNING XML_HEADER_VERSION):
                case strlenof(XML_HEADER_BEGINNING XML_HEADER_VERSION XML_HEADER_ENCODING):
                case strlenof(XML_HEADER_BEGINNING XML_HEADER_VERSION XML_HEADER_ENCODING XML_HEADER_STANDALODE):
                    st++;
                    break;
                }
            }
            else
            {
                switch (ind)
                {
                case strlenof(XML_HEADER_BEGINNING XML_HEADER_VERSION):
                    ind += strlenof(XML_HEADER_ENCODING);
                    st = ST_HEADER_D, upd = 0;
                    break;
                case strlenof(XML_HEADER_BEGINNING XML_HEADER_VERSION XML_HEADER_ENCODING):
                    ind += strlenof(XML_HEADER_STANDALODE);
                    st = ST_HEADER_E, upd = 0;
                    break;
                case strlenof(XML_HEADER_BEGINNING XML_HEADER_VERSION XML_HEADER_ENCODING XML_HEADER_STANDALODE):
                    ind = 0, st++, upd = 0;
                    break;
                default:
                    log_message_error_xml(log, CODE_METRIC, &txt, path, XML_ERROR_HEADER);
                    halt = 1;
                }
            }
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Reading whitespaces
            //

        case ST_WHITESPACE_A:
        case ST_WHITESPACE_B:
        case ST_WHITESPACE_C:
        case ST_WHITESPACE_D:
        case ST_WHITESPACE_E:
        case ST_WHITESPACE_F:
        case ST_WHITESPACE_G:
        case ST_WHITESPACE_H:        
        case ST_WHITESPACE_I:
        case ST_WHITESPACE_J:
        case ST_WHITESPACE_K:
        case ST_WHITESPACE_L:        
        case ST_WHITESPACE_M:
        case ST_WHITESPACE_N:
        case ST_WHITESPACE_O:
            if (!utf8_is_whitespace(utf8_val, utf8_len)) st++, upd = 0;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Reading '=' sign
            //

        case ST_EQUALS_A:
        case ST_EQUALS_B:
        case ST_EQUALS_C:
        case ST_EQUALS_D:
            if (utf8_val == '=')
            {
                st++;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Reading opening quote
            //

        case ST_QUOTE_OPENING_A: 
        case ST_QUOTE_OPENING_B: 
        case ST_QUOTE_OPENING_C: 
        case ST_QUOTE_OPENING_D:
            quot = 0;
            switch (utf8_val)
            {
            case '\'':
                quot++;
            case '\"':
                str = txt, len = 0, st++;
                break;
            default:
                log_message_error_char_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
                halt = 1;
            }
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Reading closing quote
            //

        case ST_QUOTE_CLOSING_A:
        case ST_QUOTE_CLOSING_B:
        case ST_QUOTE_CLOSING_C:
        case ST_QUOTE_CLOSING_D:
            if (utf8_val == (uint32_t) "\"\'"[quot]) st++;
            else switch (utf8_val)
            {
            case '&':                
                if (st == ST_QUOTE_CLOSING_D) // Special sequences should not appear in the header
                {
                    st = ST_SPECIAL_A;
                    break;
                }                                
            case '<':
                log_message_error_char_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
                halt = 1;
                break;
            default:
                if (!array_test(&temp.buff, &temp.cap, sizeof(*temp.buff), 0, 0, ARG_SIZE(len, utf8_len, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                else
                {
                    strncpy(temp.buff + len, (char *) utf8_byte, utf8_len);
                    len += utf8_len;
                    break;
                }
                halt = 1;
            }
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Reading header attributes
            //

        case ST_HEADER_HANDLING_A:
            if (len == strlenof(XML_HEADER_VERSION_VALUE) && !strncmp(temp.buff, XML_HEADER_VERSION_VALUE, len))
            {
                st++, upd = 0;
                break;
            }
            log_message_error_str_xml(log, CODE_METRIC, &txt, path, temp.buff, len, XML_ERROR_STR_UNHANDLED_VALUE);
            halt = 1;
            break;

        case ST_HEADER_HANDLING_B:
            if (len == strlenof(XML_HEADER_ENCODING_VALUE) && !Strnicmp(temp.buff, XML_HEADER_ENCODING_VALUE, len))
            {
                st++, upd = 0;
                break;
            }
            log_message_error_str_xml(log, CODE_METRIC, &txt, path, temp.buff, len, XML_ERROR_STR_UNHANDLED_VALUE);
            halt = 1;
            break;

        case ST_HEADER_HANDLING_C:
            if (len == strlenof(XML_HEADER_STANDALODE_VALUE) && !strncmp(temp.buff, XML_HEADER_STANDALODE_VALUE, len))
            {
                st++, upd = 0;
                break;
            }
            log_message_error_str_xml(log, CODE_METRIC, &txt, path, temp.buff, len, XML_ERROR_STR_UNHANDLED_VALUE);
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Handling the ending of the XML header
            //

        case ST_HEADER_ENDING_A: 
        case ST_HEADER_ENDING_B: 
        case ST_HEADER_ENDING_C:
            st = ST_HEADER_ENDING_D, upd = 0;
            break;
        
        case ST_HEADER_ENDING_D:
        case ST_HEADER_ENDING_E:
            if (utf8_val == (uint32_t) (XML_HEADER_ENDING)[ind])
            {
                ind++, st++;
                break;
            }
            log_message_error_xml(log, CODE_METRIC, &txt, path, XML_ERROR_HEADER);
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Reading '<' sign
            //

        case ST_ANGLE_LEFT_A:
        case ST_ANGLE_LEFT_B:
            if (utf8_val == '<')
            {
                st++;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Tag beginning handling
            //

        case ST_TAG_BEGINNING_A:
            if (utf8_val == '!') st = ST_COMMENT_A;
            else str = txt, str.col--, str.byte -= utf8_len, len = 0, st++, upd = 0;
            break;

        case ST_TAG_BEGINNING_B:
            switch (utf8_val)
            {
            case '/':
                str = txt, len = 0, st = ST_NAME_D;
                break;
            case '!':
                st = ST_COMMENT_C;
                break;
            default:
                str = txt, str.col--, str.byte -= utf8_len, len = 0, st++, upd = 0;
            }
            break;

        case ST_TAG_BEGINNING_C:
            if (utf8_val == '!') // Only comments are allowed after the root tag
            {
                st = ST_COMMENT_E;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Tag or attribute name reading and storing
            //

        case ST_NAME_A: 
        case ST_NAME_B: 
        case ST_NAME_C:
        case ST_NAME_D:
            if ((len ? utf8_is_xml_name_char : utf8_is_xml_name_start_char)(utf8_val, utf8_len))
            {
                if (!array_test(&temp.buff, &temp.cap, sizeof(*temp.buff), 0, 0, ARG_SIZE(utf8_len, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                else
                {
                    strncpy(temp.buff, (char *) utf8_byte, utf8_len);
                    len += utf8_len;
                    break;
                }
            }
            else if (len)
            {
                st++; 
                upd = 0;
                break;
            }
            else log_message_error_char_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Tag name handling for the first time
            //

        case ST_TAG_ROOT_A:
            root = 1;
            if (!xml_node_selector(&xml_node, temp.buff, len, context)) log_message_error_str_xml(log, CODE_METRIC, &txt, path, temp.buff, len, XML_ERROR_STR_UNEXPECTED_TAG);
            {
                if (!array_init(&stack.frame, &stack.cap, 1, sizeof(*stack.frame), 0, 0)) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                else
                {
                    stack.frame[0] = (struct frame) { .obj = malloc(sizeof(*stack.frame[0].obj)), .len = len };
                    if (!stack.frame[0].obj) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                    else
                    {
                        if (!array_init(&name.buff, &name.cap, len + 1, sizeof(*name.buff), 0, 0)) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                        else
                        {
                            strcpy(name.buff, temp.buff);
                            *stack.frame[0].obj = (struct program_object) { .prologue = xml_node.prologue, .epilogue = xml_node.epilogue, .dispose = xml_node.dispose, .context = calloc(1, xml_node.sz) };
                            if (!stack.frame[0].obj->context) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                            else
                            {
                                st++, upd = 0;
                                break;
                            }
                        }
                    }
                }
            }
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Tag name handling
            //

        case ST_TAG_A:
            if (!xml_node_selector(&xml_node, temp.buff, len, context)) log_message_error_str_xml(log, CODE_METRIC, &txt, path, temp.buff, len, XML_ERROR_STR_UNEXPECTED_TAG);
            {
                if (!array_test(&stack.frame, &stack.cap, sizeof(*stack.frame), 0, 0, ARG_SIZE(dep))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                else
                {
                    struct program_object *obj = stack.frame[dep - 1].obj;
                    if (!array_test(&obj->dsc, &stack.frame[dep - 1].dsc_cap, sizeof(*obj->dsc), 0, 0, ARG_SIZE(obj->dsc_cnt, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                    {
                        obj->dsc[obj->dsc_cnt] = (struct program_object_node) { .prologue = xml_node.prologue, .epilogue = xml_node.epilogue, .dispose = xml_node.dispose, .context = calloc(1, xml_node.sz) };
                        if (!obj->dsc[obj->dsc_cnt].context) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                        else
                        {
                            if (!array_test(&name.buff, &name.cap, sizeof(*name.buff), 0, 0, ARG_SIZE(stack.frame[dep - 1].off, stack.frame[dep - 1].len, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                            else
                            {
                                strcpy(name.buff, temp.buff);
                                stack.frame[dep] = (struct frame) { .obj = (struct program_object *) &obj->dsc[obj->dsc_cnt], .len = len, .off = stack.frame[dep - 1].off + stack.frame[dep - 1].len + 1 };
                                obj->dsc_cnt++;
                                st = OFF_LB, upd = 0;
                                break;
                            }
                        }
                    }
                }
            }
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Tag ending handling
            // 

        case ST_TAG_ENDING_A:
            if (utf8_val == '/') st++;
            else if (utf8_val == '>') dep++, st = ST_TEXT_A;
            else str = txt, str.col--, str.byte -= utf8_len, len = 0, st += 2, upd = 0;
            break;

        case ST_TAG_ENDING_B: 
        case ST_TAG_ENDING_C:
            if (utf8_val == '>')
            {
                switch (st)
                {
                case ST_TAG_ENDING_B:
                    st = ST_TEXT_A;
                    break;
                case ST_TAG_ENDING_C:
                    st = ST_WHITESPACE_O;
                }
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Text handling
            // 

        case ST_TEXT_A:
            switch (utf8_val)
            {
            case '<':
                st++;
                break;
            case '&':
                st = ST_SPECIAL_A;
            default:;
                //if (!array_test(&text, )
            }
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Closing tag handling
            //

        case ST_CLOSING_TAG_A:
            if (len != stack.frame[dep].len || strncmp(temp.buff, name.buff + stack.frame[dep].off, len)) log_message_error_str_xml(log, CODE_METRIC, &txt, path, temp.buff, len, XML_ERROR_STR_ENDING);
            else
            {
                struct program_object *obj = stack.frame[dep - 1].obj;
                if (!array_test(&obj->dsc, &stack.frame[dep - 1].dsc_cap, sizeof(*obj->dsc), 0, ARRAY_REDUCE, ARG_SIZE(obj->dsc_cnt))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                else
                {
                    st = ST_TAG_ENDING_B;
                    upd = 0;
                    break;
                }                
            }
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Selecting attribute
            //

        case ST_ATTRIBUTE_A:
            if (!xml_att_selector(&xml_att, temp.buff, len, context, &ind)) log_message_error_str_xml(log, CODE_METRIC, &txt, path, temp.buff, len, XML_ERROR_STR_UNEXPECTED_ATTRIBUTE);
            else
            {
                if (ind >= attb.cap)
                {
                    if (!array_test(&attb.buff, &attb.cap, sizeof(*attb.buff), 0, 0, ARG_SIZE(UINT8_CNT(ind + 1)))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                    else
                    {
                        uint8_bit_set(attb.buff, ind);
                        st++, upd = 0;
                        break;
                    }
                }
                else if (uint8_bit_test_set(attb.buff, ind)) log_message_error_str_xml(log, CODE_METRIC, &txt, path, temp.buff, len, XML_ERROR_STR_DUPLICATED_ATTRIBUTE);
                else
                {
                    st++, upd = 0;
                    break;
                }
            }
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Executing attribute handler
            //

        case ST_ATTRIBUTE_HANDLING_A:
            temp.buff[len] = '\0'; // There is always room for the zero-terminator
            if (!xml_att.handler(temp.buff, len, (char *) stack.frame[dep].obj->context + xml_att.offset, xml_att.context)) log_message_error_str_xml(log, CODE_METRIC, &str, path, temp.buff, len, XML_ERROR_STR_UNHANDLED_VALUE);
            else
            {
                upd = 0;
                st = OFF_LB;
                break;
            }
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Control sequence handling
            //

        case ST_SPECIAL_A:
        case ST_SPECIAL_C:
            ctrl_context.metric = str;
            ctrl_context.val = ctrl_context.st = 0;
            st++;
            
        case ST_SPECIAL_B:
            switch (xml_ctrl_impl(&ctrl_context, utf8_byte, utf8_val, utf8_len, &temp.buff, &len, &temp.cap, path, log))
            {
            case PROC_CONTINUE:
                break;
            case PROC_COMPLETE:
                st = ST_QUOTE_CLOSING_D;
                break;
            case PROC_FAIL:
                halt = 1;
            }
            break;

        case ST_SPECIAL_D:
            switch (xml_ctrl_impl(&ctrl_context, utf8_byte, utf8_val, utf8_len, &temp.buff, &len, &temp.cap, path, log))
            {
            case PROC_CONTINUE:
                break;
            case PROC_COMPLETE:
                st = ST_TEXT_A;
                break;
            case PROC_FAIL:
                halt = 1;
            }
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Comment handling
            //

        case ST_COMMENT_A: // Comments before the root tag
        case ST_COMMENT_C: // Comments inside the root tag
        case ST_COMMENT_E: // Comments after the root tag
            comment_context.st = 0;
            st++;

        case ST_COMMENT_B:
        case ST_COMMENT_D: 
        case ST_COMMENT_F:
            switch (xml_comment_impl(&comment_context, utf8_byte, utf8_val, utf8_len, &str, path, log))
            {
            case PROC_CONTINUE:
                break;
            case PROC_COMPLETE:
                switch (st)
                {
                case ST_COMMENT_B:
                    st = ST_WHITESPACE_K;
                    break;
                case ST_COMMENT_D:
                    st = ST_TEXT_A;
                    break;
                case ST_COMMENT_F:
                    st = ST_WHITESPACE_O;
                }
                break;
            case PROC_FAIL:
                halt = 1;
            }
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Various stubs
            //

        default:
            log_message_error_xml(log, CODE_METRIC, &txt, path, XML_ERROR_COMPILER);
            halt = 1;
        }
    }
    
    
    if (dep) log_message_error_char_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_EOF);
    if (!root) log_message_error_xml(log, CODE_METRIC, &txt, path, XML_ERROR_ROOT);
    if ((halt || dep) && stack.frame) program_object_dispose(stack.frame[0].obj), stack.frame[0].obj = NULL;

    Fclose(f);
    struct program_object *res = stack.frame ? stack.frame[0].obj : NULL;
    free(stack.frame);
    free(name.buff);
    free(attb.buff);
    free(ctrl_context.buff);
    free(temp.buff);
    return res;
}

/*
bool xml_node_selector(struct xml_node *node, char *str, size_t len, void *context)
{
    
    
    return 1;
}

bool xml_att_selector(struct xml_node *node, char *str, size_t len, void *context, size_t *p_ind)
{
    //ind = binary_search(temp.buff, stack.frame[dep].node->att, sizeof *stack.frame[dep].node->att, stack.frame[dep].node->att_cnt, str_strl_stable_cmp_len, &len);
    //att = stack.frame[dep].node->att + ind;
   
    return 1;
}
*/
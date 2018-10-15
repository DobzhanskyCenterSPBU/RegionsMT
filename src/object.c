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

struct xml_object_node
{
    void *context;
    prologue_callback prologue;
    epilogue_callback epilogue;
    disposer_callback dispose;
    text_handler_callback text_handler;
    ptrdiff_t *text_off; // Offset in the text array
    size_t *text_len; // Length of the text chunk
    struct {
        struct xml_object_node *dsc;
        size_t dsc_cnt;
    };
};

struct xml_object
{
    struct xml_object_node root;
    char *text;
};

// Program object destuctor makes use of stack-less recursion
void xml_object_dispose(struct xml_object *obj)
{
    if (!obj) return;
    for (struct xml_object_node *root = &obj->root, *prev = NULL;;)
    {
        if (root->dsc_cnt)
        {
            struct xml_object_node *temp = root->dsc + root->dsc_cnt - 1;
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
                struct xml_object_node *temp = prev->dsc;
                free(root);
                root = prev;
                prev = temp;
            }
        }
    }
    free(obj->text);
    free(obj);
}

static bool xml_object_execute_impl(struct xml_object_node *node, void *in, char *text)
{
    bool res = 1;
    void *temp;
    res &= node->prologue(in, &temp, node->context);
    for (size_t i = 0; res && i < node->dsc_cnt; i++)
    {
        if (node->text_handler) res &= node->text_handler(in, temp, text + node->text_off[i], node->text_len[i]);
        res &= xml_object_execute_impl(node->dsc + i, temp, text);
    }
    if (node->text_handler) res &= node->text_handler(in, temp, text + node->text_off[node->dsc_cnt], node->text_len[node->dsc_cnt]);
    res &= node->epilogue(in, temp, node->context);
    return res;
}

bool xml_object_execute(struct xml_object *obj, void *in)
{
    return xml_object_execute_impl((struct xml_object_node *) obj, in, obj->text);
}

///////////////////////////////////////////////////////////////////////////////

enum xml_status {
    XML_ERROR_UTF,
    XML_ERROR_INVALID_CHAR,
    XML_ERROR_DECL,
    XML_ERROR_ROOT,
    XML_ERROR_COMPILER,
    XML_ERROR_CHAR_UNEXPECTED_EOF,
    XML_ERROR_CHAR_UNEXPECTED_CHAR,
    XML_ERROR_STR_UNEXPECTED_TAG,
    XML_ERROR_STR_UNEXPECTED_ATTRIBUTE,
    XML_ERROR_STR_ENDING,
    XML_ERROR_STR_DUPLICATED_ATTRIBUTE,
    XML_ERROR_STR_UNHANDLED_VALUE,
    XML_ERROR_STR_CONTROL,
    XML_ERROR_VAL_RANGE,
    XML_ERROR_VAL_REFERENCE
};

struct text_metric {
    uint64_t byte;
    size_t col, line;
};

struct message_xml_context {
    struct text_metric metric;
    const char *path;
    char *str;
    uint32_t val;
    size_t len;
    enum xml_status status;
};

static bool message_xml(char *buff, size_t *p_buff_cnt, void *Context)
{
    struct message_xml_context *restrict context = Context;
    const char *str[] = {
        "Incorrect UTF-8 byte sequence",
        "Invalid character",
        "Invalid XML declaration",
        "No root element found",
        "Compiler malfunction",
        "Unexpected end of file",
        "Unexpected character",
        "Unexpected tag",
        "Unexpected attribute",
        "Unexpected closing tag",
        "Duplicated attribute",
        "Unable to handle value",
        "Invalid control sequence",
        "is out of range",
        "referencing to invalid character"
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
            case XML_ERROR_INVALID_CHAR:
            case XML_ERROR_DECL:
            case XML_ERROR_ROOT:
            case XML_ERROR_COMPILER:
                tmp = snprintf(buff + cnt, len, "%s", str[context->status]);
                break;
            case XML_ERROR_CHAR_UNEXPECTED_EOF:
            case XML_ERROR_CHAR_UNEXPECTED_CHAR:
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
            case XML_ERROR_VAL_REFERENCE:
                tmp = snprintf(buff + cnt, len, "Numeric value %" PRIu32 " %s", context->val, str[context->status]);
                break;
            }
        case 1:
            tmp = snprintf(buff + cnt, len, " (file: \"%s\"; line: %zu; character: %zu; byte: %" PRIu64 ")!\n",
                context->path,
                context->metric.line + 1,
                context->metric.col - col_disp + 1,
                context->metric.byte - byte_disp + 1
            );
        }
        if (tmp < 0) return 0;
        cnt = size_add_sat(cnt, (size_t) tmp);
        len = size_sub_sat(len, (size_t) tmp);
    }
    *p_buff_cnt = cnt;
    return 1;
}

static bool log_message_error_xml(struct log *restrict log, struct code_metric code_metric, struct text_metric metric, const char *path, enum xml_status status)
{
    return log_message(log, code_metric, MESSAGE_ERROR, message_xml, &(struct message_xml_context) { .metric = metric, .path = path, .status = status });
}

static bool log_message_error_str_xml(struct log *restrict log, struct code_metric code_metric, struct text_metric metric, const char *path, char *str, size_t len, enum xml_status status)
{
    return log_message(log, code_metric, MESSAGE_ERROR, message_xml, &(struct message_xml_context) { .metric = metric, .path = path, .str = str, .len = len, .status = status });
}

static bool log_message_error_char_xml(struct log *restrict log, struct code_metric code_metric, struct text_metric metric, const char *path, uint8_t *utf8_byte, size_t utf8_len, enum xml_status status)
{
    return log_message(log, code_metric, MESSAGE_ERROR, message_xml, &(struct message_xml_context) { .metric = metric, .path = path, .str = (char *) utf8_byte, .len = utf8_len, .status = status });
}

static bool log_message_error_val_xml(struct log *restrict log, struct code_metric code_metric, struct text_metric metric, const char *path, uint32_t val, enum xml_status status)
{
    return log_message(log, code_metric, MESSAGE_ERROR, message_xml, &(struct message_xml_context) { .metric = metric, .path = path, .val = val, .status = status });
}

///////////////////////////////////////////////////////////////////////////////
//
//  XML syntax analyzer
//

enum {
    STATUS_FAILURE = 0,
    STATUS_SUCCESS,
    STATUS_REPEAT,
};

struct xml_ctrl_context {
    size_t len, cap;
    char *buff;
    struct text_metric metric;
    uint32_t val;
};

struct xml_att_context {
    size_t len, cap;
    char *buff;
    struct text_metric metric;
    struct xml_val val;
    bool quot;
};

struct xml_context {
    size_t context, bits_cap;
    uint8_t *bits;
    struct xml_ctrl_context ctrl_contex;
    struct xml_att_context val_context;
    uint32_t st, ctrl_st, val_st;
};

static void xml_ctrl_context_reset(struct xml_ctrl_context *context)
{
    context->len = 0;
}

static void xml_val_context_reset(struct xml_att_context *context)
{
    context->len = context->quot = 0;
}

static unsigned xml_name_impl(uint8_t *utf8_byte, uint32_t utf8_val, uint8_t utf8_len, char **p_buff, size_t *p_len, size_t *p_cap, bool terminator, struct text_metric metric, const char *path, struct log *log)
{
    size_t len = *p_len;
    if ((len ? utf8_is_xml_name_char_len : utf8_is_xml_name_start_char_len)(utf8_val, utf8_len))
    {
        if (!array_test(p_buff, p_cap, sizeof(**p_buff), 0, 0, ARG_SIZE(len, utf8_len, terminator))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
        else
        {
            strncpy(*p_buff, (char *) utf8_byte, utf8_len);
            *p_len += utf8_len;
            return 1 | STATUS_REPEAT;
        }
    }
    else if (len)
    {
        if (terminator) (*p_buff)[len] = '\0';
        return 1;
    }
    else log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
    return 0;
}

static bool xml_ref_impl()
{

}

static bool xml_ctrl_impl(uint32_t *p_st, struct xml_ctrl_context *context, uint8_t *utf8_byte, uint32_t utf8_val, uint8_t utf8_len, char **p_buff, size_t *p_len, size_t *p_cap, struct text_metric metric, const char *path, struct log *log)
{
    const struct { struct strl name; struct strl subs; } ctrl_subs[] = {
        { STRI("amp"), STRI("&") },
        { STRI("apos"), STRI("\'") },
        { STRI("gt"), STRI(">") },
        { STRI("lt"), STRI("<") },
        { STRI("quot"), STRI("\"") }
    };

    enum {
        ST_CTRL_IF_NUM = 0,
        ST_CTRL_IF_NUM_HEX,
        ST_CTRL_DIGIT_HEX_FIRST,
        ST_CTRL_DIGIT_HEX_NEXT,
        ST_CTRL_DIGIT_DEC_FIRST,
        ST_CTRL_DIGIT_DEC_NEXT,
        ST_CTRL_NUM_HANDLER,
        ST_CTRL_TEXT,
        ST_CTRL_TEXT_HANDLER
    };
     
    size_t st = *p_st;
    for (;;)
    {
        switch (st)
        {
        case ST_CTRL_IF_NUM:
            context->metric = metric;
            if (utf8_val == '#')
            {
                context->metric.byte++, context->metric.col++, st = ST_CTRL_IF_NUM_HEX;
                break;
            }
            st = ST_CTRL_TEXT;
            continue;
        case ST_CTRL_IF_NUM_HEX:
            if (utf8_val == 'x')
            {
                context->metric.byte++, context->metric.col++, st = ST_CTRL_DIGIT_HEX_FIRST;
                break;
            }
            st++;
            continue;
        case ST_CTRL_DIGIT_HEX_FIRST:
            if ('0' <= utf8_val && utf8_val <= '9')
            {
                context->val = utf8_val - '0', st++;
                break;
            }
            else if ('A' <= utf8_val && utf8_val <= 'F')
            {
                context->val = utf8_val - 'A' + 10, st++;
                break;
            }
            else if ('a' <= utf8_val && utf8_val <= 'f')
            {
                context->val = utf8_val - 'a' + 10, st++;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, context->metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
        case ST_CTRL_DIGIT_HEX_NEXT:
            if ('0' <= utf8_val && utf8_val <= '9')
            {
                if (uint32_fused_mul_add(&context->val, 16, utf8_val - '0')) log_message_error_val_xml(log, CODE_METRIC, context->metric, path, utf8_val, XML_ERROR_VAL_RANGE);
                else break;
            }
            else if ('A' <= utf8_val && utf8_val <= 'F')
            {
                if (uint32_fused_mul_add(&context->val, 16, utf8_val - 'A' + 10)) log_message_error_val_xml(log, CODE_METRIC, context->metric, path, utf8_val, XML_ERROR_VAL_RANGE);
                else break;
            }
            else if ('a' <= utf8_val && utf8_val <= 'f')
            {
                if (uint32_fused_mul_add(&context->val, 16, utf8_val - 'a' + 10)) log_message_error_val_xml(log, CODE_METRIC, context->metric, path, utf8_val, XML_ERROR_VAL_RANGE);
                else break;
            }
            else
            {
                st = ST_CTRL_NUM_HANDLER;
                continue;
            }
            return 0;
        case ST_CTRL_DIGIT_DEC_FIRST:
            if ('0' <= utf8_val && utf8_val <= '9')
            {
                context->val = utf8_val - '0', st++;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
        case ST_CTRL_DIGIT_DEC_NEXT:
            if ('0' <= utf8_val && utf8_val <= '9')
            {
                if (uint32_fused_mul_add(&context->val, 10, utf8_val - '0')) log_message_error_val_xml(log, CODE_METRIC, context->metric, path, utf8_val, XML_ERROR_VAL_RANGE);
                else break;
            }
            else
            {
                st = ST_CTRL_NUM_HANDLER;
                continue;
            }
            return 0;
        case ST_CTRL_NUM_HANDLER:
            if (utf8_val == ';')
            {
                if (!utf8_is_xml_char(context->val)) log_message_error_val_xml(log, CODE_METRIC, context->metric, path, context->val, XML_ERROR_VAL_REFERENCE);
                else
                {
                    size_t len = *p_len;
                    uint8_t ctrl_byte[UTF8_COUNT], ctrl_len;
                    utf8_encode(context->val, ctrl_byte, &ctrl_len);
                    if (!array_test(p_buff, p_cap, sizeof(**p_buff), 0, 0, ARG_SIZE(len, ctrl_len, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                    else
                    {
                        strncpy(*p_buff + len, (char *) ctrl_byte, ctrl_len);
                        *p_len = len + ctrl_len, st = 0;
                        break;
                    }
                }
            }
            else log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
        case ST_CTRL_TEXT: {
            unsigned res = xml_name_impl(utf8_byte, utf8_val, utf8_len, &context->buff, &context->len, &context->cap, 0, context->metric, path, log);
            if (!res) return 0;
            if (res & STATUS_REPEAT) break;
            st++;
            continue;
        }
        case ST_CTRL_TEXT_HANDLER:
            if (utf8_val == ';')
            {
                size_t ind = binary_search(context->buff, ctrl_subs, sizeof(*ctrl_subs), countof(ctrl_subs), str_strl_stable_cmp_len, &context->len);
                if (!(ind + 1)) log_message_error_str_xml(log, CODE_METRIC, context->metric, path, context->buff, context->len, XML_ERROR_STR_CONTROL);
                else
                {
                    size_t len = *p_len;
                    struct strl subs = ctrl_subs[ind].subs;
                    if (!array_test(p_buff, p_cap, sizeof(*p_buff), 0, 0, ARG_SIZE(len, subs.len, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                    else
                    {
                        strncpy(*p_buff + len, subs.str, subs.len);
                        *p_len = len + subs.len, st = 0;
                        break;
                    }
                }
            }
            else log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
        }
        break;
    }
    *p_st = st;
    return 1;
}

static bool xml_val_impl(uint32_t *p_st, uint32_t *p_ctrl_st, struct xml_ctrl_context *ctrl_context, char **p_buff, size_t *p_len, size_t *p_cap, uint8_t *utf8_byte, uint32_t utf8_val, uint8_t utf8_len, struct text_metric metric, const char *path, struct log *log)
{
    enum {
        ST_QUOTE_OPENING,
        ST_QUOTE_CLOSING,
        ST_CTRL
    };
    
    uint32_t st = *p_st;
    for (;;)
    {
        switch (st)
        {
        case ST_QUOTE_OPENING:
            switch (utf8_val)
            {
            case '\'':
                context->quot++;
            case '\"':
                context->metric = metric, context->len = 0, st++;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
        case ST_QUOTE_CLOSING:
            if (utf8_val == (uint32_t) "\"\'"[context->quot])
            {
                if (!array_test(&context->buff, &context->cap, sizeof(*context->buff), 0, 0, ARG_SIZE(context->len, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                else
                {
                    context->buff[context->len] = '\0', st++;
                    continue;
                }
                return 0;
            }
            switch (utf8_val)
            {
            case '&':
                if (p_ctrl_st)
                {
                    st = ST_CTRL;
                    break;
                }
            case '<':
                log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
                return 0;
            }
            if (!array_test(&context->buff, &context->cap, sizeof(*context->buff), 0, 0, ARG_SIZE(context->len, utf8_len))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
            else
            {
                strncpy(context->buff + context->len, (char *) utf8_byte, utf8_len);
                context->len += utf8_len;
                break;
            }
            return 0;
        case ST_CTRL:
            if (!xml_ctrl_impl(p_ctrl_st, ctrl_context, utf8_byte, utf8_val, utf8_len, &context->buff, &context->len, &context->cap, metric, path, log)) return 0;
            if (!*p_ctrl_st) st = ST_QUOTE_CLOSING;
        }
    }
}

static bool xml_att_impl(uint32_t *p_st, struct xml_att_context *context, uint32_t *p_ctrl_st, struct xml_ctrl_context *ctrl_context, xml_val_selector_callback val_selector, void *val_res, void *val_selector_context, uint8_t **p_bits, size_t *p_bits_cap, uint8_t *utf8_byte, uint32_t utf8_val, uint8_t utf8_len, struct text_metric metric, const char *path, struct log *log)
{
    enum {
        ST_INIT = 0,
        ST_NAME,
        ST_NAME_HANDLER,
        ST_WHITESPACE_A,
        ST_EQUALS,
        ST_WHITESPACE_B,
        
        ST_VAL_HANDLER,
        
    };
    
    uint32_t st = *p_st;
    for (;;) 
    {
        switch (st)
        {
        case ST_INIT:
            context->metric = metric, st++;
            continue;
        case ST_NAME: {
            unsigned res = xml_name_impl(utf8_byte, utf8_val, utf8_len, &context->buff, &context->len, &context->cap, 1, metric, path, log);
            if (!res) return 0;
            if (res & STATUS_REPEAT) break;
            st++;
            continue;
        }
        case ST_NAME_HANDLER: {
            size_t ind;
            if (!val_selector(&context->val, context->buff, context->len, val_res, val_selector_context, &ind) && !(ind + 1)) log_message_error_str_xml(log, CODE_METRIC, metric, path, context->buff, context->len, XML_ERROR_STR_UNEXPECTED_ATTRIBUTE);
            else
            {
                size_t bits_cap = *p_bits_cap;
                if (ind >= UINT8_CNT(bits_cap))
                {
                    if (!array_test(p_bits, p_bits_cap, sizeof(**p_bits), 0, ARRAY_CLEAR, ARG_SIZE(UINT8_CNT(ind + 1)))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                    else
                    {
                        uint8_bit_set(*p_bits, ind);
                        st++;
                        continue;
                    }
                }
                else if (uint8_bit_test_set(*p_bits, ind)) log_message_error_str_xml(log, CODE_METRIC, metric, path, context->buff, context->len, XML_ERROR_STR_DUPLICATED_ATTRIBUTE);
                else
                {
                    st++;
                    continue;
                }
            }
            return 0;
        }
        case ST_WHITESPACE_A:
        case ST_WHITESPACE_B:
            if (utf8_is_whitespace_len(utf8_val, utf8_len)) break;
            st++;
            continue;
        case ST_EQUALS:
            if (utf8_val == '=')
            {
                st++;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
        
        case ST_VAL_HANDLER:
            if (!context->val.handler(context->buff, context->len, context->val.ptr, context->val.context)) log_message_error_str_xml(log, CODE_METRIC, context->metric, path, context->buff, context->len, XML_ERROR_STR_UNHANDLED_VALUE);
            else
            {
                st = 0;
                break;
            }
            return 0;
        }
        break;
    }
    *p_st = st;
    return 1;
}

static bool xml_match_impl(size_t *p_context, const char *str, size_t len, uint8_t *utf8_byte, uint32_t utf8_val, uint8_t utf8_len, struct text_metric metric, const char *path, struct log *log)
{
    size_t ind = *p_context;
    if (utf8_val == (uint32_t) str[ind])
    {
        *p_context = ind < len ? ind + 1 : 0;
        return 1;
    }
    log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
    return 0;
}

static bool *xml_decl_read(const char *str, size_t len, void *ptr, void *Context)
{
    const struct strl decl_val[] = { STRI("1.0"), STRI("UTF-8"), STRI("no") }; // Encoding value is not case-sensetive 
    size_t pos = *(size_t *) Context;
    if (len == decl_val[pos].len && (pos == 1 ? !Strnicmp(str, decl_val[pos].str, len) : !strncmp(str, decl_val[pos].str, len))) return 1;
    return 0;
}

static bool *xml_decl_val_selector(struct xml_val *restrict val, const char *restrict str, size_t len, void *restrict res, void *Context, size_t *restrict p_ind)
{
    (void) res;
    const struct strl decl_name[] = { STRI("version"), STRI("encoding"), STRI("standalone") };
    size_t *p_pos = Context, pos = *p_pos;
    for (size_t i = pos; i < countof(decl_name); i++)
    {
        if (len == decl_name[i].len && !strncmp(str, decl_name[i].str, len))
        {
            *p_ind = *p_pos = i;
            *val = (struct xml_val) { .context = Context, .handler = xml_decl_read };
            return 1;
        }
        if (!i) break; // First attribute may not be missing!
    }
    return 0;    
}

static bool xml_decl_impl(uint32_t *restrict p_st, size_t *restrict p_context, uint32_t *restrict p_val_st, struct xml_att_context *restrict val_context, uint8_t **restrict p_bits, size_t *restrict p_bits_cap, uint8_t *restrict utf8_byte, uint32_t utf8_val, uint8_t utf8_len, struct text_metric metric, const char *restrict path, struct log *restrict log)
{
    enum {
        ST_DECL_BEGINNING = 0,
        ST_WHITESPACE_MANDATORY_FIRST,
        ST_WHITESPACE_FIRST,
        ST_ATTRIBUTE_FIRST,
        ST_WHITESPACE_MANDATORY_NEXT,
        ST_WHITESPACE_NEXT,
        ST_ATTRIBUTE_NEXT,
        ST_DECL_ENDING_CHECK,
        ST_DECL_ENDING
    };

    uint32_t st = *p_st;
    for (;;)
    {
        switch (st)
        {
        case ST_DECL_BEGINNING:
            if (!xml_match_impl(p_context, STRC("<?xml"), utf8_byte, utf8_val, utf8_len, metric, path, log)) return 0;
            if (!*p_context) st++;
            break;
        case ST_WHITESPACE_MANDATORY_FIRST:
        case ST_WHITESPACE_MANDATORY_NEXT:
            if (utf8_is_whitespace_len(utf8_val, utf8_len))
            {
                st++;
                break;
            }
            st = ST_DECL_ENDING_CHECK;
            continue;
        case ST_WHITESPACE_FIRST:
        case ST_WHITESPACE_NEXT:
            if (utf8_is_whitespace_len(utf8_val, utf8_len)) break;
            st++;
            continue;
        case ST_ATTRIBUTE_NEXT:
            xml_val_context_reset(val_context);
            st = ST_ATTRIBUTE_FIRST;
            continue;
        case ST_ATTRIBUTE_FIRST:
            if (!xml_att_impl(p_val_st, val_context, NULL, NULL, xml_decl_val_selector, NULL, p_context, p_bits, p_bits_cap, utf8_byte, utf8_val, utf8_len, metric, path, log)) return 0;
            if (!*p_val_st) st++;
            break;
        case ST_DECL_ENDING_CHECK:
            if (*p_context) // Check if at least one attribute is present
            {
                *p_context = 0, st++;
                continue;
            }
            log_message_error_xml(log, CODE_METRIC, metric, path, XML_ERROR_DECL);
            return 0;
        case ST_DECL_ENDING:
            if (!xml_match_impl(p_context, STRC("?>"), utf8_byte, utf8_val, utf8_len, metric, path, log)) return 0;
            if (!*p_context) st = 0;
            break;
        }
        break;
    }
    *p_st = st;
    return 1;
}

static bool xml_comment_impl(uint32_t *restrict p_st, uint8_t *restrict utf8_byte, uint32_t utf8_val, uint8_t utf8_len, struct text_metric metric, const char *restrict path, struct log *restrict log)
{
    uint32_t st = *p_st;
    for (;;) 
    {
        switch (st)
        {
        case 0:           
        case 1:
            if (utf8_val == '-')
            {
                st++;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
        case 2:
            if (utf8_val == '-') st++;
            break;
        case 3:
            if (utf8_val == '-') st++;
            else st--;
            break;
        case 4:
            if (utf8_val == '>')
            {
                st = 0;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
        } 
        break;
    }
    *p_st = st;
    return 1;
}

static bool xml_doc_impl(uint32_t *p_st, struct xml_context *context, uint8_t *utf8_byte, uint32_t utf8_val, uint8_t utf8_len, struct text_metric metric, const char *path, struct log *log)
{
    enum {
        ST_DECL = 0,
        ST_WHITESPACE_A,
        ST_TAG_BEGINNING,
        ST_TAG_ROUTING,
        ST_TAG_NAME,
        ST_ATT,
        ST_DECL_ENDING_CHECK,
        ST_DECL_ENDING,
        ST_COMMENT
    };
    
    uint32_t st = *p_st;
    for (;;)
    {
        switch (st)
        {
        case ST_DECL:
            if (!xml_decl_impl(&context->st, &context->context, &context->val_st, &context->val_context, &context->bits, &context->bits_cap, utf8_byte, utf8_val, utf8_val, metric, path, log)) return 0;
            if (!context->st) st++;
            break;
        case ST_WHITESPACE_A: // Optional whitespaces
            if (!utf8_is_whitespace_len(utf8_val, utf8_len)) break;
            st++;
            continue;
        case ST_TAG_BEGINNING:
            if (utf8_val == '<')
            {
                st++;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
        case ST_TAG_ROUTING:
            switch (utf8_val)
            {
            case '/':
                st = ST_NAME_D;
                break;
            case '!':
                st = ST_COMMENT;
                break;
            default:
                st++;
                continue;
            }
            break;
        case ST_TAG_NAME: {
            unsigned res = xml_name_impl(utf8_byte, utf8_val, utf8_len, &context->val_context.buff, &context->val_context.len, &context->val_context.cap, 1, metric, path, log);
        }
        case ST_COMMENT:
            if (!xml_comment_impl(&context->st, utf8_byte, utf8_val, utf8_len, metric, path, log)) return 0;
            if (!context->st) st = ST_WHITESPACE_A;
        }
        break;
    }
    *p_st = st;
    return 1;
}

// This is ONLY required for the line marked with '(*)'
_Static_assert(BLOCK_READ > 2, "'BLOCK_READ' constant is assumed to be greater than 2!");

struct xml_object *xml_compile(const char *path, xml_node_selector_callback xml_node_selector, xml_val_selector_callback xml_val_selector, void *context, struct log *log)
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
    
    
    struct { struct frame { struct xml_object *obj; size_t off, len, dsc_cap; } *frame; size_t cap; } stack = { 0 };
    
    struct xml_context xml_context = { 0 };
    struct xml_att_context xml_val_context = { 0 };
    struct xml_ctrl_context xml_ctrl_context = { 0 };
    
    size_t dep = 0;
    char buff[BLOCK_READ], *text = NULL;
    struct text_metric metric = { 0 }; // Text metrics        
    struct xml_att xml_val = { 0 };
    struct xml_node xml_node = { 0 };
    
    size_t rd = fread(buff, 1, sizeof(buff), f), pos = 0;    
    if (rd >= 3 && !strncmp(buff, "\xef\xbb\xbf", 3)) pos += 3, metric.byte += 3; // (*) Reading UTF-8 BOM if it is present
    
    enum {
        ST_DECL = 0,
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
        ST_COMMENT_A,
        ST_COMMENT_B,
        ST_COMMENT_C,
    };
    
    struct utf8 {
        uint8_t utf8_byte[UTF8_COUNT], utf8_len, utf8_context;
        uint32_t utf8_val;
    };

    uint8_t utf8_byte[UTF8_COUNT];
    uint8_t utf8_len = 0, utf8_context = 0;
    uint32_t utf8_val = 0;
    bool halt = 0, root = 0;
    uint32_t st = 0;

    for (; !halt && rd; rd = fread(buff, 1, sizeof buff, f), pos = 0) for (halt = 1; pos < rd; halt = 1)
    {
        // UTF-8 decoder coroutine
        if (!utf8_decode(buff[pos], &utf8_val, utf8_byte, &utf8_len, &utf8_context)) log_message_error_xml(log, CODE_METRIC, metric, path, XML_ERROR_UTF);
        else
        {
            pos++, metric.byte++;
            if (utf8_context) continue;
            if (!utf8_is_valid(utf8_val, utf8_len)) log_message_error_xml(log, CODE_METRIC, metric, path, XML_ERROR_UTF);
            else
            {
                if (utf8_val == '\n') metric.line++, metric.col = 0; // Updating text metrics
                else metric.col++;
                if (!utf8_is_xml_char_len(utf8_val, utf8_len)) log_message_error_xml(log, CODE_METRIC, metric, path, XML_ERROR_INVALID_CHAR);
                else halt = 0;
            }
        }
        
        if (halt) break;

        for (;;)
        {
            switch (st)
            {
            

                ///////////////////////////////////////////////////////////////
                //
                //  Tag beginning handling
                //

            case ST_TAG_BEGINNING_A:
                if (utf8_val == '!') st = ST_COMMENT_A;
                else str = metric, str.col--, str.byte -= utf8_len, len = 0, st++, upd = 0;
                break;

            case ST_TAG_BEGINNING_B:
                switch (utf8_val)
                {
                case '/':
                    str = metric, len = 0, st = ST_NAME_D;
                    break;
                case '!':
                    st = ST_COMMENT_B;
                    break;
                default:
                    str = metric, str.col--, str.byte -= utf8_len, len = 0, st++, upd = 0;
                }
                break;

            case ST_TAG_BEGINNING_C:
                if (utf8_val == '!') // Only comments are allowed after the root tag
                {
                    st = ST_COMMENT_C;
                    break;
                }
                log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
                halt = 1;
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Tag or attribute name reading and storing
                //

            case ST_NAME_A:
            case ST_NAME_B:
            case ST_NAME_C:


                ///////////////////////////////////////////////////////////////
                //
                //  Tag name handling for the first time
                //

            case ST_TAG_ROOT_A:
                root = 1;
                if (!xml_node_selector(&xml_node, temp.buff, len, context)) log_message_error_str_xml(log, CODE_METRIC, metric, path, temp.buff, len, XML_ERROR_STR_UNEXPECTED_TAG);
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
                                *stack.frame[0].obj = (struct xml_object) { .prologue = xml_node.prologue, .epilogue = xml_node.epilogue, .dispose = xml_node.dispose, .context = calloc(1, xml_node.sz) };
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
                if (!xml_node_selector(&xml_node, temp.buff, len, context)) log_message_error_str_xml(log, CODE_METRIC, metric, path, temp.buff, len, XML_ERROR_STR_UNEXPECTED_TAG);
                {
                    if (!array_test(&stack.frame, &stack.cap, sizeof(*stack.frame), 0, 0, ARG_SIZE(dep))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                    else
                    {
                        struct xml_object *obj = stack.frame[dep - 1].obj;
                        if (!array_test(&obj->dsc, &stack.frame[dep - 1].dsc_cap, sizeof(*obj->dsc), 0, 0, ARG_SIZE(obj->dsc_cnt, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                        {
                            obj->dsc[obj->dsc_cnt] = (struct xml_object_node) { .prologue = xml_node.prologue, .epilogue = xml_node.epilogue, .dispose = xml_node.dispose, .context = calloc(1, xml_node.sz) };
                            if (!obj->dsc[obj->dsc_cnt].context) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                            else
                            {
                                if (!array_test(&name.buff, &name.cap, sizeof(*name.buff), 0, 0, ARG_SIZE(stack.frame[dep - 1].off, stack.frame[dep - 1].len, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                                else
                                {
                                    strcpy(name.buff, temp.buff);
                                    stack.frame[dep] = (struct frame) { .obj = (struct xml_object *) &obj->dsc[obj->dsc_cnt], .len = len, .off = stack.frame[dep - 1].off + stack.frame[dep - 1].len + 1 };
                                    obj->dsc_cnt++;
                                    st = ST_TAG_ENDING_A, upd = 0;
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
                else str = metric, str.col--, str.byte -= utf8_len, len = 0, st += 2, upd = 0;
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
                log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
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
                if (len != stack.frame[dep].len || strncmp(temp.buff, name.buff + stack.frame[dep].off, len)) log_message_error_str_xml(log, CODE_METRIC, metric, path, temp.buff, len, XML_ERROR_STR_ENDING);
                else
                {
                    struct xml_object *obj = stack.frame[dep - 1].obj;
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

                ///////////////////////////////////////////////////////////////
                //
                //  Executing attribute handler
                //

            case ST_ATTRIBUTE_HANDLING_A:
                ; // There is always room for the zero-terminator
                if (!xml_val.handler(temp.buff, len, (char *) stack.frame[dep].obj->context + xml_val.offset, xml_val.context)) log_message_error_str_xml(log, CODE_METRIC, str, path, temp.buff, len, XML_ERROR_STR_UNHANDLED_VALUE);
                else
                {
                    upd = 0;
                    st = ST_TAG_ENDING_A;
                    break;
                }
                halt = 1;
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Control sequence handling
                //

            case ST_SPECIAL_B:
                switch (xml_ctrl_impl(&ctrl_context, utf8_byte, utf8_val, utf8_len, &temp.buff, &len, &temp.cap, str, path, log))
                {
                case STATUS_CONTINUE:
                    break;
                case STATUS_COMPLETE:
                    st = ST_TEXT_A;
                    break;
                case STATUS_FAIL:
                    halt = 1;
                }
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Comment handling
                //

            case ST_COMMENT_A: // Comments before the root tag
            case ST_COMMENT_B: // Comments inside the root tag
            case ST_COMMENT_C: // Comments after the root tag
                switch (xml_comment_impl(NULL, utf8_byte, utf8_val, utf8_len, str, path, log))
                {
                case STATUS_CONTINUE:
                    break;
                case STATUS_COMPLETE:
                    switch (st)
                    {
                    case ST_COMMENT_A:
                        st = ST_WHITESPACE_K;
                        break;
                    case ST_COMMENT_B:
                        st = ST_TEXT_A;
                        break;
                    case ST_COMMENT_C:
                        st = ST_WHITESPACE_O;
                    }
                    break;
                case STATUS_FAIL:
                    halt = 1;
                }
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Various stubs
                //

            default:
                log_message_error_xml(log, CODE_METRIC, metric, path, XML_ERROR_COMPILER);
                halt = 1;
            }
            break;
        }
    }
    
    
    if (dep) log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_EOF);
    if (!root) log_message_error_xml(log, CODE_METRIC, metric, path, XML_ERROR_ROOT);
    if ((halt || dep) && stack.frame) xml_object_dispose(stack.frame[0].obj), stack.frame[0].obj = NULL;

    Fclose(f);
    struct xml_object *res = stack.frame ? stack.frame[0].obj : NULL;
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
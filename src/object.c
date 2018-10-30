#include "np.h"
#include "common.h"
#include "memory.h"
#include "object.h"
#include "sort.h"
#include "ll.h"
#include "strproc.h"
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

struct utf8 {
    uint8_t byte[UTF8_COUNT], len, context;
    uint32_t val;
};

struct buff {
    char *str;
    size_t len, cap;
};

enum xml_status {
    XML_ERROR_INVALID_UTF,
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
    XML_ERROR_STR_INVALID_PI,
    XML_ERROR_VAL_RANGE,
    XML_ERROR_VAL_REFERENCE
};

struct message_xml_context {
    char *path;
    struct text_metric metric;
    union {
        struct {
            char *str;
            size_t len;
        };
        struct {
            uint8_t *utf8_byte;
            uint8_t utf8_len;
        };
        uint32_t val;
    };
    enum xml_status status;
};

static bool message_xml(char *buff, size_t *p_buff_cnt, void *Context)
{
    struct message_xml_context *restrict context = Context;
    const char *str[] = {
        "Invalid UTF-8 byte sequence",
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
        "Invalid processing instruction",
        "is out of range",
        "referencing to invalid character"
    };
    size_t cnt = 0, len = *p_buff_cnt;
    for (unsigned i = 0;; i++)
    {
        int tmp = -1;
        switch (i)
        {
        case 0:
            switch (context->status)
            {
            case XML_ERROR_INVALID_UTF:
            case XML_ERROR_INVALID_CHAR:
            case XML_ERROR_DECL:
            case XML_ERROR_ROOT:
            case XML_ERROR_COMPILER:
                tmp = snprintf(buff + cnt, len, "%s", str[context->status]);
                break;
            case XML_ERROR_CHAR_UNEXPECTED_EOF:
            case XML_ERROR_CHAR_UNEXPECTED_CHAR:
                tmp = snprintf(buff + cnt, len, "%s \'%.*s\'", str[context->status], context->utf8_len, context->utf8_byte);
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
                context->metric.row + 1,
                context->metric.col + 1,
                context->metric.byte + 1
            );
        }
        if (tmp < 0) return 0;
        cnt = size_add_sat(cnt, (size_t) tmp);
        if (i == 1) break;
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
    return log_message(log, code_metric, MESSAGE_ERROR, message_xml, &(struct message_xml_context) { .metric = metric, .path = path, .utf8_byte = utf8_byte, .utf8_len = utf8_len, .status = status });
}

static bool log_message_error_val_xml(struct log *restrict log, struct code_metric code_metric, struct text_metric metric, const char *path, uint32_t val, enum xml_status status)
{
    return log_message(log, code_metric, MESSAGE_ERROR, message_xml, &(struct message_xml_context) { .metric = metric, .path = path, .val = val, .status = status });
}

///////////////////////////////////////////////////////////////////////////////
//
//  XML syntax analyzer
//

enum xml_node_type {
    XML_NODE_DECL,
    XML_NODE_ELEMENT,
    XML_NODE_ATTRIBUTE,
    XML_NODE_TEXT,
    XML_NODE_ENTITY_REF,
    XML_NODE_PI,
};

struct xml_pool {
    struct xml_node *arr;
    size_t cap, cnt;
};

struct xml_node_base {
    struct text_metric metric;
    ptrdiff_t off;
    size_t len;
};

struct xml_node_decl {
    struct xml_node_base version, encoding, standalone;
};

struct xml_node_attribute {
    struct xml_node_base name;
    struct xml_node_base val;
};

struct xml_node_element {
    struct xml_node_base name;
    struct xml_pool pool;
};

struct xml_node_text {
    struct xml_node_base text;
};

struct xml_node_entity_ref {
    struct xml_node_base text;
};

struct xml_node {
    enum xml_node_type type;
    union {
        struct xml_node_decl decl;
        struct xml_node_element element;
        struct xml_node_attribute attribute;
        struct xml_node_text text;
    };
};

struct xml_context {
    uint32_t st;
    size_t dep;
    struct xml_node *cur;
    struct buff buff;
    struct xml_pool pool;
    bool header; // if 'false' header MAY NOT appear in the document
};

struct bits {
    char *arr;
    size_t cap;
};

enum {
    STATUS_FAILURE = 0,
    STATUS_SUCCESS,
    STATUS_REPEAT,
};

struct xml_ctrl_context {
    struct buff buff;
    struct text_metric snapshot;
    uint32_t val;
};

/*struct xml_context {
    size_t context, bits_cap;
    uint8_t *bits;
    struct xml_ctrl_context ctrl_contex;
    //struct xml_att_context val_context;
    uint32_t st, ctrl_st, val_st;
};*/

/*static void xml_val_context_reset(struct xml_att_context *context)
{
    context->len = context->quot = 0;
}*/

static unsigned xml_name_impl(bool terminator, struct utf8 *utf8, struct buff *restrict buff, struct text_metric metric, const char *restrict path, struct log *restrict log)
{
    size_t len = buff->len;
    if ((len ? utf8_is_xml_name_char_len : utf8_is_xml_name_start_char_len)(utf8->val, utf8->len))
    {
        if (!array_test(&buff->str, &buff->cap, sizeof(*buff->str), 0, 0, ARG_SIZE(len, utf8_len, terminator))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
        else
        {
            strncpy(buff->str, (char *) utf8->byte, utf8->len);
            buff->len += utf8->len;
            return 1 | STATUS_REPEAT;
        }
    }
    else if (len)
    {
        if (terminator) buff->str[len] = '\0';
        return 1;
    }
    else log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8->byte, utf8->len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
    return 0;
}

static bool xml_ref_impl()
{

}

static unsigned xml_char_ref_impl(uint32_t *p_val, bool hex, struct utf8 *utf8, struct text_metric metric, const char *path, struct log *log)
{
    uint32_t val = utf8->val;
    if ('0' <= val && val <= '9') val -= '0';
    else if (hex)
    {
        if ('A' <= val && val <= 'F') val -= 'A' - 10;
        else if ('a' <= val && val <= 'f') val -= 'a' - 10;
    }
    if (*p_val)
    {
        if (val != utf8->val)
        {
            *p_val &= 0x7fffffff; // 'init' flag should be resetted        
            if (uint32_fused_mul_add(p_val, hex ? 16 : 10, val)) log_message_error_val_xml(log, CODE_METRIC, metric, path, utf8->val, XML_ERROR_VAL_RANGE);
            else
            {
                *p_val |= 0x80000000;
                return 1 | STATUS_REPEAT;
            }
        }
        else if (val == ';') return 1;
        log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8->byte, utf8->len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
    }
    else
    {
        if (val != utf8->val)
        {
            *p_val = val | 0x80000000; // Setting 'init' flag
            return 1 | STATUS_REPEAT;
        }
        log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8->byte, utf8->len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
    }
    return 0;
}

/*
static bool xml_ref_impl(uint32_t *p_st, struct xml_ctrl_context *context, uint8_t *utf8_byte, uint32_t utf8_val, uint8_t utf8_len, char **p_buff, size_t *p_len, size_t *p_cap, struct text_metric metric, const char *path, struct log *log)
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
            
        case ST_CTRL_DIGIT_HEX_NEXT:
            
        case ST_CTRL_DIGIT_DEC_FIRST:
            
        case ST_CTRL_DIGIT_DEC_NEXT:
            
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
                size_t ind = binary_search(context->buff, ctrl_subs, sizeof(*ctrl_subs), countof(ctrl_subs), str_strl_stable_cmp, &context->len);
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
*/

enum xml_val_flags {
    XML_VAL_SUPRESS_CTRL = 1,
    XML_VAL_SUPRESS_REF = 2,
    XML_VAL_SINGLE_QUOTE = 4
};

enum {
    XML_VAL_ST_A,
    XML_VAL_ST_B,
    XML_VAL_ST_QUOTE_CLOSING,
    XML_VAL_ST_CTRL
};

struct xml_val_context {
    struct text_metric *snapshot;
    struct xml_ctrl_context *ctrl_context;
};
/*
static bool xml_val_impl(uint32_t *p_st, enum xml_val_flags flags, struct xml_val_context context,  struct buff *buff, struct utf8 *utf8, struct text_metric metric, const char *path, struct log *log)
{
    uint32_t st = *p_st;
    for (;;)
    {
        switch (st)
        {
        case XML_VAL_ST_A:
            *context.snapshot = metric;
            st++;
            continue;
        case XML_VAL_ST_B:
            switch (utf8->val)
            {
            case '\'':
                if (flags & XML_VAL_SINGLE_QUOTE) st++;
                break;
            case '\"':
                if (!(flags & XML_VAL_SINGLE_QUOTE)) st++;
                break;
            case '&':
                if (!(flags & XML_VAL_SUPRESS_CTRL) && !(flags & XML_VAL_SUPRESS_REF)) st = XML_VAL_ST_CTRL;
                break;
            case '<':
                log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8->byte, utf8->len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
                return 0;
            }
            log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8->byte, utf8->len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
        case ST_QUOTE_CLOSING:
            if (utf8_val == (uint32_t) quot)
            {
                if (!array_test(&buff->str, &buff->cap, sizeof(*buff->str), 0, 0, ARG_SIZE(buff->len, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                else
                {
                    buff->str[buff->len] = '\0', st++;
                    continue;
                }
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
}*/

enum {
    XML_ATT_ST_INIT = 0,
    XML_ATT_ST_NAME,
    XML_ATT_ST_NAME_HANDLER,
    XML_ATT_ST_WHITESPACE_A,
    XML_ATT_ST_EQUALS,
    XML_ATT_ST_WHITESPACE_B,
    XML_ATT_ST_VAL_HANDLER,
};

struct xml_att_context {
    struct text_metric *snapshot;
    struct xml_ctrl_context *ctrl_context;
    struct xml_val *val;
};
/*
static bool xml_att_impl(uint32_t *p_st, struct xml_att_context context, xml_val_selector_callback val_selector, void *val_res, void *val_selector_context, struct bits *bits, struct utf8 *utf8, struct buff *buff, struct text_metric metric, const char *path, struct log *log)
{
    uint32_t st = *p_st;
    for (;;) 
    {
        size_t ind;
        unsigned res;
        switch (st)
        {
        case XML_ATT_ST_INIT:
            *context.snapshot = metric;
            st++;
            continue;
        case XML_ATT_ST_NAME:
            res = xml_name_impl(utf8, buff, 1, metric, path, log);
            if (!res) return 0;
            if (res & STATUS_REPEAT) break;
            st++;
            continue;
        case XML_ATT_ST_NAME_HANDLER: {
            if (!val_selector(&att_context->val, buff->str, buff->len, val_res, val_selector_context, &ind) && !(ind + 1)) log_message_error_str_xml(log, CODE_METRIC, att_context->metric, path, buff->str, buff->len, XML_ERROR_STR_UNEXPECTED_ATTRIBUTE);
            else
            {
                if (ind >= UINT8_CNT(bits->cap))
                {
                    if (!array_test(&bits->arr, &bits->cap, sizeof(*bits->arr), 0, ARRAY_CLEAR, ARG_SIZE(UINT8_CNT(ind + 1)))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                    else
                    {
                        uint8_bit_set(bits->arr, ind);
                        st++;
                        continue;
                    }
                }
                else if (uint8_bit_test_set(bits->arr, ind)) log_message_error_str_xml(log, CODE_METRIC, att_context->metric, path, buff->str, buff->len, XML_ERROR_STR_DUPLICATED_ATTRIBUTE);
                else
                {
                    st++;
                    continue;
                }
            }
            return 0;
        }
        case XML_ATT_ST_WHITESPACE_A:
        case XML_ATT_ST_WHITESPACE_B:
            if (utf8_is_whitespace_len(utf8->val, utf8->len)) break;
            st++;
            continue;
        case XML_ATT_ST_EQUALS:
            if (utf8->val == '=')
            {
                st++;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8->byte, utf8->len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
        case XML_ATT_ST_VAL_HANDLER:
            switch (utf8->val)
            {
            case '\'':
                context->quot++;
            case '\"':
                context->metric = metric, context->len = 0, st++;
                break;
            }

            if (utf8_val == '\'')


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
}*/

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

enum {
    XML_PI_ST_DECL_BEGINNING = 0,
    XML_PI_ST_WHITESPACE_MANDATORY_FIRST,
    XML_PI_ST_WHITESPACE_FIRST,
    XML_PI_ST_ATTRIBUTE_FIRST,
    XML_PI_ST_WHITESPACE_MANDATORY_NEXT,
    XML_PI_ST_WHITESPACE_NEXT,
    XML_PI_ST_ATTRIBUTE_NEXT,
    XML_PI_ST_DECL_ENDING_CHECK,
    XML_PI_ST_DECL_ENDING
};

enum {
    XML_DECL_ST_BEGIN_A,
    XML_DECL_ST_WHITESPACE_A,
    XML_DECL_ST_QUES_A,
    XML_DECL_ST_BEGIN_B,
    XML_DECL_ST_WHITESPACE_B,
    XML_DECL_ST_QUES_B,
    
    XML_DECL_ST_ATTRIBUTE_A_BEGIN,
    XML_DECL_ST_ATTRIBUTE_A_END,

    XML_DECL_ST_END_A,
    XML_DECL_ST_WHITESPACE_NEXT,
    XML_DECL_ST_ATTRIBUTE_NEXT,
    XML_DECL_ST_DECL_ENDING_CHECK,
    XML_DECL_ST_DECL_ENDING,
    XML_DECL_ST_DECL_CNT
};

struct xml_decl_context {
    struct text_metric *snapshot;
};

static bool xml_decl_impl(uint32_t *restrict p_st, struct xml_decl_context context, struct bits *bits, struct utf8 *restrict utf8, struct buff *restrict buff, struct text_metric metric, const char *restrict path, struct log *restrict log)
{
    uint32_t st = *p_st;
    for (;;)
    {
        switch (st)
        {
        case XML_DECL_ST_BEGIN_A:
        case XML_DECL_ST_BEGIN_B:
            if (utf8_is_whitespace_len(utf8->val, utf8->len))
            {
                st++;
                break;
            }
            st += 2;
            continue;
        case XML_DECL_ST_WHITESPACE_A:
        case XML_DECL_ST_WHITESPACE_B:
            if (utf8_is_whitespace_len(utf8->val, utf8->len)) break;
            st++;
            continue;
        case XML_DECL_ST_QUES_A:
        case XML_DECL_ST_QUES_B:
            if (utf8->val == '?')
            {
                st++;
                break;
            }
            else
            {
                st = XML_DECL_ST_ATTRIBUTE_A_BEGIN;
                continue;
            }
            log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8->byte, utf8->len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
        case XML_DECL_ST_END_A:
            if (utf8->val == '>')
            {
                // Checking if version number is provided in the XML declaration
                if (bits->arr && !uint8_bit_test(bits->arr, 0)) log_message_error_xml(log, CODE_METRIC, metric, path, XML_ERROR_DECL);
                else
                {
                    st = 0;
                    break;
                }
            }
            else log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8->byte, utf8->len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
            
            /*
        case XML_DECL_ST_ATTRIBUTE_A:


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
            break;*/
        //default:
            if (XML_DECL_ST_ATTRIBUTE_A_BEGIN <= st && st <= XML_DECL_ST_ATTRIBUTE_A_END)
            {
                st -= XML_DECL_ST_ATTRIBUTE_A_BEGIN;
                if (!xml_att_impl(&st, (struct xml_att_context) { .snapshot = &context.snapshot }, NULL, NULL, xml_decl_val_selector, NULL, NULL, bits, utf8, buff, metric, path, log)) return 0;
                if (!st) st++;
            }
        }
        break;
    }
    *p_st = st;
    return 1;
}

enum {
    XML_PI_ST_INIT = 0,
    XML_PI_ST_NAME,
    XML_PI_ST_DECL_BEGIN,
    XML_PI_ST_DECL_END = XML_PI_ST_DECL_BEGIN + XML_DECL_ST_DECL_CNT
};

static bool xml_pi_impl(uint32_t *restrict p_st, struct xml_context *context, struct utf8 *utf8, struct text_metric metric, const char *restrict path, struct log *restrict log)
{
    uint32_t st = *p_st;
    for (;;)
    {
        unsigned res;
        switch (st)
        {
        case XML_PI_ST_INIT:
            //*context.snapshot = metric;
            st++;
            continue;
        case XML_PI_ST_NAME:
            //res = xml_name_impl(1, utf8, buff, metric, path, log);
            if (!res) return 0;
            if (res & STATUS_REPEAT) break;
            //if (buff->len == 3 && !Strnicmp(buff->str, "xml", buff->len))
            {
                //if (!decl || strncmp(buff->str, "xml", buff->len)) log_message_error_str_xml(log, CODE_METRIC, *context.snapshot, path, buff->str, buff->len, XML_ERROR_STR_INVALID_PI);
                //else st = XML_PI_ST_DECL_BEGIN;
            }
        default:
            if (XML_PI_ST_DECL_BEGIN <= st && st <= XML_PI_ST_DECL_END)
            {
                st -= XML_PI_ST_DECL_BEGIN;
                //if (xml_decl_impl(&st, )
            }
            //st = str_strl_stable_cmp(buff->str, &(struct strl) STRI("xml"), &buff->len) ? XML_DOC_ST_PI_BEGIN : XML_DOC_ST_DECL_BEGIN;
            //buff->len = 0;
            continue;
        }
    }
}

enum {
    XML_DOCTYPE_CNT = 0,
};

enum {
    XML_COMMENT_ST_A = 0,
    XML_COMMENT_ST_B,
    XML_COMMENT_ST_C,
    XML_COMMENT_ST_D,
    XML_COMMENT_ST_E,
    XML_COMMENT_CNT
};

// Comment handling
static bool xml_comment_impl(uint32_t *restrict p_st, struct utf8 *utf8, struct text_metric metric, const char *restrict path, struct log *restrict log)
{
    uint32_t st = *p_st;
    for (;;) 
    {
        switch (st)
        {
        case XML_COMMENT_ST_A:
        case XML_COMMENT_ST_B:
            if (utf8->val == '-')
            {
                st++;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8->byte, utf8->len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
        case XML_COMMENT_ST_C:
            if (utf8->val == '-') st++;
            break;
        case XML_COMMENT_ST_D:
            if (utf8->val == '-') st++;
            else st--;
            break;
        case XML_COMMENT_ST_E:
            if (utf8->val == '>')
            {
                st = 0;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8->byte, utf8->len, XML_ERROR_CHAR_UNEXPECTED_CHAR);
            return 0;
        } 
        break;
    }
    *p_st = st;
    return 1;
}

enum {
    XML_DOC_ST_TAG_A = 0,
    XML_DOC_ST_TAG_B,
    XML_DOC_ST_MODE,
    XML_DOC_ST_EXCL,
    XML_DOC_ST_PI,
    XML_DOC_ST_NAME,
    XML_DOC_ST_WHITESPACE_A, // Optional whitespaces
    XML_DOC_ST_DECL_NAME,
    XML_DOC_ST_DECL_BEGIN,
    XML_DOC_ST_DECL_END,
    XML_DOC_ST_PI_BEGIN,
    XML_DOC_ST_PI_END,
    XML_DOC_ST_COMMENT_BEGIN,
    XML_DOC_ST_COMMENT_END = XML_DOC_ST_COMMENT_BEGIN + XML_COMMENT_CNT - 1,
    XML_DOC_ST_DOCTYPE_BEGIN,
    XML_DOC_ST_DOCTYPE_END = XML_DOC_ST_DOCTYPE_BEGIN + XML_DOCTYPE_CNT - 1,
    XML_DOC_ST_CDATA_BEGIN,
    XML_DOC_ST_CDATA_END = XML_DOC_ST_CDATA_BEGIN - 1
};

static bool xml_doc_impl(uint32_t *p_st, struct xml_context *context, struct utf8 *utf8, struct text_metric metric, const char *path, struct log *log)
{
    uint32_t st = *p_st;
    for (;;)
    {
        switch (st)
        {
        case XML_DOC_ST_TAG_A: // Determines whether header may appear or not
            if (utf8->val == '<') st = XML_DOC_ST_MODE, context->header = 1;
            else st = XML_DOC_ST_WHITESPACE_A;
        case XML_DOC_ST_TAG_B:
            if (utf8->val == '<') st++;
            else st = XML_DOC_ST_WHITESPACE_A;
            break;
        case XML_DOC_ST_MODE:
            switch (utf8->val)
            {
            case '!':
                st = XML_DOC_ST_EXCL;
                break;
            case '?':
                st = XML_DOC_ST_DECL_NAME;
                break;
            default:
                st = XML_DOC_ST_NAME;
                continue;
            }
            break;
        case XML_DOC_ST_EXCL:
            switch (utf8->val)
            {
            case '-':
                st = XML_DOC_ST_COMMENT_BEGIN;
                break;
            case '[':
                st = XML_DOC_ST_CDATA_BEGIN;
                break;
            default:
                st = XML_DOC_ST_DOCTYPE_BEGIN;
                continue;
            }            
            break;
        case XML_DOC_ST_WHITESPACE_A:
            if (!utf8_is_whitespace_len(utf8->val, utf8->len)) break;
            st++;
            continue;
        case XML_DOC_ST_DECL_NAME: 

        default:
            if (XML_DOC_ST_PI_BEGIN <= st && st <= XML_DOC_ST_PI_END)
            {
                st -= XML_DOC_ST_PI_BEGIN;
                //if (!xml_pi_impl(&st, context, ))
            }
            else if (XML_DOC_ST_COMMENT_BEGIN <= st && st <= XML_DOC_ST_COMMENT_END)
            {
                st -= XML_DOC_ST_COMMENT_BEGIN;
                if (!xml_comment_impl(&st, utf8, metric, path, log)) return 0;
                if (st) st += XML_DOC_ST_COMMENT_BEGIN;
                else st = XML_DOC_ST_WHITESPACE_A;
                break;
            }
            else if (XML_DOC_ST_DOCTYPE_BEGIN <= st && st <= XML_DOC_ST_DOCTYPE_END)
            {
                // Not implemented
            }
            log_message_error_xml(log, CODE_METRIC, metric, path, XML_ERROR_COMPILER);
            return 0;
                /*
                
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
        }*/
        
        }
        break;
    }
    *p_st = st;
    return 1;
}

struct xml_object *xml_compile(const char *path, xml_node_selector_callback xml_node_selector, xml_val_selector_callback xml_val_selector, void *context, struct log *log)
{
    FILE *f = NULL;
    if (path) for (;;)
    {
        f = fopen(path, "rb");
        if (!f) log_message_fopen(log, CODE_METRIC, MESSAGE_ERROR, path, errno);
        else break;
        return 0;
    }
    else f = stdin;

    struct { struct frame { struct xml_object *obj; size_t off, len, dsc_cap; } *frame; size_t cap; } stack = { 0 };
    
    struct xml_context xml_context = { 0 };
    struct xml_att_context xml_val_context = { 0 };
    struct xml_ctrl_context xml_ctrl_context = { 0 };
    
    size_t dep = 0;
    char buff[MAX(BLOCK_READ, countof(UTF8_BOM))] = { '\0' }, *text = NULL;
    struct text_metric metric = { 0 }; // Text metrics        
    struct xml_att xml_val = { 0 };
    struct xml_node xml_node = { 0 };
    
    size_t rd = fread(buff, 1, sizeof(buff), f), pos = 0;
    if (rd >= strlenof(UTF8_BOM) && !strncmp(buff, STRC(UTF8_BOM))) pos += strlenof(UTF8_BOM), metric.byte += strlenof(UTF8_BOM);
        
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
    
    struct utf8 utf8;

    bool halt = 0, skip = 0;
    uint32_t st = 0;

    for (; !halt && rd; rd = fread(buff, 1, sizeof buff, f), pos = 0) for (halt = 1; pos < rd; halt = 1)
    {
        // UTF-8 decoder coroutine
        if (!utf8_decode(buff[pos], &utf8.val, utf8.byte, &utf8.len, &utf8.context)) log_message_error_xml(log, CODE_METRIC, metric, path, XML_ERROR_INVALID_UTF);
        else
        {
            pos++, metric.byte++;
            if (utf8.context) continue;
            if (!utf8_is_valid(utf8.val, utf8.len)) log_message_error_xml(log, CODE_METRIC, metric, path, XML_ERROR_INVALID_UTF);
            else
            {
                switch (utf8.val)
                {
                case '\n':
                    if (!skip) // '\n' which appears after '\r' is always skipped
                    {
                        metric.row++, metric.col = 0, skip = 0;
                        break;
                    }
                    skip = 0;
                    continue;
                case '\r': // '\r' is replaced by '\n'
                    metric.row++, metric.col = 0, skip = 1, utf8 = (struct utf8) { .byte = { '\n' }, .len = 1, .val = '\n' };
                    break;
                default:
                    metric.col++, skip = 0;
                    if (!utf8_is_xml_char_len(utf8.val, utf8.len)) log_message_error_xml(log, CODE_METRIC, metric, path, XML_ERROR_INVALID_CHAR);
                    else halt = 0;
                }
            }
        }
        
        if (halt) break;

        for (;;)
        {
            /*switch (st)
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
            break;*/
        }
    }
    
    
    //if (dep) log_message_error_char_xml(log, CODE_METRIC, metric, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_EOF);
    //if (!root) log_message_error_xml(log, CODE_METRIC, metric, path, XML_ERROR_ROOT);
    if ((halt || dep) && stack.frame) xml_object_dispose(stack.frame[0].obj), stack.frame[0].obj = NULL;

    Fclose(f);
    struct xml_object *res = stack.frame ? stack.frame[0].obj : NULL;
    free(stack.frame);
    //free(name.buff);
    //free(attb.buff);
    //free(ctrl_context.buff);
    //free(temp.buff);
    return res;
}

/*
bool xml_node_selector(struct xml_node *node, char *str, size_t len, void *context)
{
    
    
    return 1;
}

bool xml_att_selector(struct xml_node *node, char *str, size_t len, void *context, size_t *p_ind)
{
    //ind = binary_search(temp.buff, stack.frame[dep].node->att, sizeof *stack.frame[dep].node->att, stack.frame[dep].node->att_cnt, str_strl_stable_cmp, &len);
    //att = stack.frame[dep].node->att + ind;
   
    return 1;
}
*/
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
    void *context;
    prologue_callback prologue;
    epilogue_callback epilogue;
    disposer_callback dispose;    
    struct {
        struct program_object *dsc;
        size_t dsc_cnt;
    };
};

// Program object destuctor makes use of stack-less recursion
void program_object_dispose(struct program_object *obj)
{
    if (!obj) return;
    for (struct program_object *prev = NULL;;)
    {
        if (obj->dsc_cnt)
        {
            struct program_object *temp = obj->dsc + obj->dsc_cnt - 1;
            obj->dsc = prev;
            prev = obj;
            obj = temp;
        }
        else
        {
            obj->dispose(obj->context);
            if (!prev) break;
            if (--prev->dsc_cnt) obj--;
            else
            {
                struct program_object *temp = prev->dsc;
                free(obj);
                obj = prev;
                prev = temp;
            }
        }
    }
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
        "Invalid control sequence",
        "Numeric value %" PRIu32 " is out of range"
    };
    size_t cnt = 0, len = *p_buff_cnt, col_disp = 0, byte_disp = 0, buff_cnt = *p_buff_cnt;
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
                tmp = snprintf(buff, buff_cnt, "%s", str[context->status]);
                break;
            case XML_ERROR_CHAR_UNEXPECTED_EOF:
            case XML_ERROR_CHAR_INVALID_SYMBOL:
                byte_disp = context->len, col_disp = 1; // Returning to the previous symbol
                tmp = snprintf(buff, buff_cnt, "%s \'%.*s\'", str[context->status], (int) context->len, context->str);
                break;
            case XML_ERROR_STR_UNEXPECTED_TAG:
            case XML_ERROR_STR_UNEXPECTED_ATTRIBUTE:
            case XML_ERROR_STR_DUPLICATED_ATTRIBUTE:
            case XML_ERROR_STR_ENDING:
            case XML_ERROR_STR_UNHANDLED_VALUE:
            case XML_ERROR_STR_CONTROL:
                tmp = snprintf(buff, buff_cnt, "%s \"%.*s\"", str[context->status], (int) context->len, context->str);
                break;
            case XML_ERROR_VAL_RANGE:
                tmp = snprintf(buff, buff_cnt, str[context->status], context->val);
                break;
            }
        case 1:
            tmp = snprintf(buff + len, buff_cnt - len, " (file: \"%s\"; line: %zu; character: %zu; byte: %" PRIu64 ")!\n",
                context->path,
                context->text_metric->line + 1,
                context->text_metric->col - col_disp + 1,
                context->text_metric->byte - byte_disp + 1
            );
        }
        if (tmp < 0) return 0;
        len = size_sub_sat(len, (size_t) tmp);
        cnt = size_add_sat(cnt, (size_t) tmp);
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
    
    struct { char *buff; size_t cap; } temp = { 0 }, ctrl = { 0 }, name = { 0 };
    struct { uint8_t *buff; size_t cap; } attb = { 0 };
    struct { struct frame { struct program_object *obj; size_t off, len; } *frame; size_t cap; } stack = { 0 };
        
    struct { struct strl name; struct strl subs; } ctrl_subs[] = {
        { STRI("amp"), STRI("&") },
        { STRI("apos"), STRI("\'") },
        { STRI("gt"), STRI(">") },
        { STRI("lt"), STRI("<") },
        { STRI("quot"), STRI("\"") }
    };

    uint8_t quot = 0;
    size_t len = 0, ind = 0, dep = 0;
    uint32_t ctrl_val = 0;
    char buff[BLOCK_READ];
    uint8_t utf8_byte[UTF8_COUNT];
    struct text_metric txt = { 0 }, str = { 0 }, ctr = { 0 }; // Text metrics        
    struct xml_att xml_att;
    struct xml_node xml_node;
    
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
        ST_ST0, ST_TAG_ROOT_A,
        ST_WHITESPACE_L,
        ST_EA0,       
        ST_EB0,        
        ST_ST1, ST_AH0, 
        ST_WHITESPACE_M, 
        ST_EQUALS_D, 
        ST_WHITESPACE_N, 
        ST_QUOTE_OPENING_D, 
        ST_QUOTE_CLOSING_D, 
        ST_AV0,       
        ST_WHITESPACE_O,
        ST_ANGLE_LEFT_B, 
        ST_TAG_BEGINNING_B, 
        ST_ST2, 
        ST_TAG_A, 
        ST_ST3, 
        ST_CT0, 
        ST_EB1,        
        ST_WHITESPACE_P, 
        ST_ANGLE_LEFT_C,
        ST_TAG_BEGINNING_C,
        ST_ST4,        
        ST_SPECIAL_A,
        ST_SPECIAL_B, 
        ST_SPECIAL_C, 
        ST_SPECIAL_D, 
        ST_SPECIAL_E, 
        ST_SPECIAL_F, 
        ST_SPECIAL_G, 
        ST_SPECIAL_H, 
        ST_SPECIAL_I,        
        ST_COMMENT_A,
        ST_COMMENT_B,
        ST_COMMENT_C,
        ST_COMMENT_D,
        ST_COMMENT_E,
        ST_COMMENT_F,
        ST_COMMENT_G,
        ST_COMMENT_H,
        ST_COMMENT_I,
        ST_COMMENT_J,
        ST_COMMENT_K,
        ST_COMMENT_L
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
        case ST_WHITESPACE_O: // <--
        case ST_WHITESPACE_P:
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
        case ST_ANGLE_LEFT_C:
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

        case ST_TAG_BEGINNING_A: case ST_TAG_BEGINNING_C:
            if (utf8_val == '!')
            {
                switch (st)
                {
                case ST_TAG_BEGINNING_A:
                    st = ST_COMMENT_A;
                    break;
                case ST_TAG_BEGINNING_C:
                    st = ST_COMMENT_I;
                }
            }
            else str = txt, str.col--, str.byte -= utf8_len, len = 0, st++, upd = 0;
            break;

        case ST_TAG_BEGINNING_B:
            switch (utf8_val)
            {
            case '/':
                str = txt, len = 0, st = ST_ST3;
                break;
            case '!':
                st = ST_COMMENT_E;
                break;
            default:
                str = txt, str.col--, str.byte -= utf8_len, len = 0, st++, upd = 0;
            }
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Tag or attribute name reading and storing
            //

        case ST_ST0: case ST_ST1: case ST_ST2: case ST_ST3:
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
                    if (!array_test(&obj->dsc, &obj->dsc_cnt, sizeof(*obj->dsc), 0, ARRAY_STRICT, ARG_SIZE(obj->dsc_cnt, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                    {

                        obj->dsc[obj->dsc_cnt] = (struct program_object) { .prologue = xml_node.prologue, .epilogue = xml_node.epilogue, .dispose = xml_node.dispose, .context = calloc(1, xml_node.sz) };
                        if (!obj->dsc[obj->dsc_cnt].context) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                        else
                        {
                            if (!array_test(&name.buff, &name.cap, sizeof(*name.buff), 0, 0, ARG_SIZE(stack.frame[dep - 1].off, stack.frame[dep - 1].len, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                            else
                            {
                                strcpy(name.buff, temp.buff);
                                stack.frame[dep] = (struct frame) { .obj = &obj->dsc[obj->dsc_cnt], .len = len, .off = stack.frame[dep - 1].off + stack.frame[dep - 1].len + 1 };
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
            //  Tag end handling
            // 

        case ST_EA0:
            if (utf8_val == '/') st++;
            else if (utf8_val == '>') dep++, st = ST_WHITESPACE_O;
            else str = txt, str.col--, str.byte -= utf8_len, len = 0, st += 2, upd = 0;
            break;

        case ST_EB0: case ST_EB1:
            if (utf8_val == '>')
            {
                switch (st)
                {
                case ST_EB0:
                    st = ST_WHITESPACE_O;
                    break;
                case ST_EB1:
                    st = ST_WHITESPACE_P;
                }
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Closing tag handling
            //

        case ST_CT0:
            if (len != stack.frame[dep].len || strncmp(temp.buff, name.buff + stack.frame[dep].off, len)) log_message_error_str_xml(log, CODE_METRIC, &txt, path, temp.buff, len, XML_ERROR_STR_ENDING);
            else
            {
                st = ST_EB0;
                upd = 0;
                break;
            }
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Selecting attribute
            //

        case ST_AH0:
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

        case ST_AV0:
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
            ctr = txt;
            if (utf8_val == '#') ctr.col++, ctr.byte++, st++;
            else ind = 0, st = ST_SPECIAL_H, upd = 0;
            break;

        case ST_SPECIAL_B:
            if (utf8_val == 'x') ctr.col++, ctr.byte++, st++;
            else st = ST_SPECIAL_E, upd = 0;
            break;

        case ST_SPECIAL_C:
            if ('0' <= utf8_val && utf8_val <= '9')
            {
                ctrl_val = utf8_val - '0', st++;
                break;
            }
            else if ('A' <= utf8_val && utf8_val <= 'F')
            {
                ctrl_val = utf8_val - 'A' + 10, st++;
                break;
            }
            else if ('a' <= utf8_val && utf8_val <= 'f')
            {
                ctrl_val = utf8_val - 'a' + 10, st++;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, &ctr, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
            halt = 1;
            break;

        case ST_SPECIAL_D:
            if ('0' <= utf8_val && utf8_val <= '9')
            {
                if (uint32_fused_mul_add(&ctrl_val, 16, utf8_val - '0')) log_message_error_val_xml(log, CODE_METRIC, &ctr, path, utf8_val, XML_ERROR_VAL_RANGE);
                else break;
            }
            else if ('A' <= utf8_val && utf8_val <= 'F')
            {
                if (uint32_fused_mul_add(&ctrl_val, 16, utf8_val - 'A' + 10)) log_message_error_val_xml(log, CODE_METRIC, &ctr, path, utf8_val, XML_ERROR_VAL_RANGE);
                else break;
            }
            else if ('a' <= utf8_val && utf8_val <= 'f')
            {
                if (uint32_fused_mul_add(&ctrl_val, 16, utf8_val - 'a' + 10)) log_message_error_val_xml(log, CODE_METRIC, &ctr, path, utf8_val, XML_ERROR_VAL_RANGE);
                else break;
            }
            else
            {
                st = ST_SPECIAL_H, upd = 0;
                break;
            }
            halt = 1;
            break;

        case ST_SPECIAL_E:
            if ('0' <= utf8_val && utf8_val <= '9')
            {
                ctrl_val = utf8_val - '0', st++;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, &ctr, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
            halt = 1;
            break;

        case ST_SPECIAL_F:
            if ('0' <= utf8_val && utf8_val <= '9')
            {
                if (uint32_fused_mul_add(&ctrl_val, 10, utf8_val - '0')) log_message_error_val_xml(log, CODE_METRIC, &ctr, path, utf8_val, XML_ERROR_VAL_RANGE);
                else break;
            }
            else
            {
                ind = 0, st++, upd = 0;
                break;
            }
            halt = 1;
            break;

        case ST_SPECIAL_G:
            if (utf8_val == ';')
            {
                if (ctrl_val >= UTF8_BOUND) log_message_error_val_xml(log, CODE_METRIC, &ctr, path, utf8_val,  XML_ERROR_VAL_RANGE);
                else
                {
                    uint8_t ctrl_byte[6], ctrl_len;
                    utf8_encode(ctrl_val, ctrl_byte, &ctrl_len);
                    if (!array_test(&temp.buff, &temp.cap, sizeof(*temp.buff), 0, 0, ARG_SIZE(len, ctrl_len, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                    else
                    {
                        strncpy(temp.buff + len, (char *) ctrl_byte, ctrl_len);
                        len += ctrl_len, st = ST_QUOTE_CLOSING_D;
                        break;
                    }
                }
            }
            else log_message_error_char_xml(log, CODE_METRIC, &ctr, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
            halt = 1;
            break;

        case ST_SPECIAL_H:
            if ((ind ? utf8_is_xml_name_char : utf8_is_xml_name_start_char)(utf8_val, utf8_len))
            {
                if (!array_test(&ctrl.buff, &ctrl.cap, sizeof(*ctrl.buff), 0, 0, ARG_SIZE(utf8_len))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                else
                {
                    strncpy(ctrl.buff, (char *) utf8_byte, utf8_len);
                    ind += utf8_len;
                    break;
                }
            }
            else if (ind)
            {
                st++, upd = 0;
                break;
            }
            else log_message_error_char_xml(log, CODE_METRIC, &ctr, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
            halt = 1;
            break;

        case ST_SPECIAL_I:
            if (utf8_val == ';')
            {
                size_t ctrl_ind = binary_search(ctrl.buff, ctrl_subs, sizeof(*ctrl_subs), countof(ctrl_subs), str_strl_stable_cmp_len, &ind);
                if (!(ctrl_ind + 1)) log_message_error_str_xml(log, CODE_METRIC, &ctr, path, ctrl.buff, ind, XML_ERROR_STR_CONTROL);
                else
                {
                    struct strl subs = ctrl_subs[ctrl_ind].subs;
                    if (!array_test(&temp.buff, &temp.cap, sizeof(*temp.buff), 0, 0, ARG_SIZE(len, subs.len + 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
                    else
                    {
                        strncpy(temp.buff + len, subs.str, subs.len);
                        len += subs.len;
                        st = ST_QUOTE_CLOSING_D;
                        break;
                    }
                }
            }
            else log_message_error_char_xml(log, CODE_METRIC, &ctr, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Comment handling
            //

        case ST_COMMENT_A: // Comments before the root tag
        case ST_COMMENT_E: // Comments inside the root tag
        case ST_COMMENT_I: // Comments after the root tag
            ind = 0;
            
        case ST_COMMENT_B:
        case ST_COMMENT_F: 
        case ST_COMMENT_J:
            if (utf8_val == '-')
            {
                st++;
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
            halt = 1;
            break;

        case ST_COMMENT_C: 
        case ST_COMMENT_G: 
        case ST_COMMENT_K:
            if (ind == 2) st++, upd = 0;
            else if (utf8_val == '-') ind++;
            else ind = 0;
            break;

        case ST_COMMENT_D: 
        case ST_COMMENT_H: 
        case ST_COMMENT_L:
            if (utf8_val == '>')
            {
                switch (st)
                {
                case ST_COMMENT_D:
                    st = ST_WHITESPACE_K;
                    break;
                case ST_COMMENT_H:
                    st = ST_WHITESPACE_O;
                    break;
                case ST_COMMENT_L:
                    st = ST_WHITESPACE_P;
                }
                break;
            }
            log_message_error_char_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
            halt = 1;
            break;

            ///////////////////////////////////////////////////////////////
            //
            //  Various stubs
            //

        case ST_ST4:
            log_message_error_char_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_len, XML_ERROR_CHAR_INVALID_SYMBOL);
            halt = 1;
            break;

        default:
            log_message_error_xml(log, CODE_METRIC, &txt, path, XML_ERROR_COMPILER);
            halt = 1;
        }
    }
    
    
    if (dep) log_message_error_char_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_len, XML_ERROR_CHAR_UNEXPECTED_EOF);
    if (!root) log_message_error_xml(log, CODE_METRIC, &txt, path, XML_ERROR_ROOT);
    if ((halt || dep) && stack.frame) program_object_dispose(stack.frame[0].obj), stack.frame[0].obj = NULL;

    if (f != stdin) fclose(f);
    struct program_object *res = stack.frame ? stack.frame[0].obj : NULL;
    free(stack.frame);
    free(name.buff);
    free(attb.buff);
    free(ctrl.buff);
    free(temp.buff);
    return res;
}

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

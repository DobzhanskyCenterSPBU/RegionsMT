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
    XML_ERROR_UNEXPECTED_EOF = 0,
    XML_ERROR_UTF,
    XML_ERROR_INVALID_SYMBOL,
    XML_ERROR_PROLOGUE,
    XML_ERROR_UNEXPECTED_TAG, 
    XML_ERROR_UNEXPECTED_ATTRIBUTE, 
    XML_ERROR_ENDING, 
    XML_ERROR_DUPLICATED_ATTRIBUTE,
    XML_ERROR_UNHANDLED_VALUE, 
    XML_ERROR_CONTROL, 
    XML_ERROR_COMPILER, 
    XML_ERROR_RANGE
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
        "Unexpected end of file",
        "Incorrect UTF-8 byte sequence",
        "Invalid symbol",
        "Invalid XML prologue",
        "Unexpected tag",
        "Unexpected attribute",
        "Unexpected closing tag",
        "Duplicated attribute",
        "Unable to handle value",
        "Invalid control sequence",
        "Compiler malfunction",
        "Numeric value %" PRIu32 " is out of range"
    };
    size_t cnt = 0, len = *p_buff_cnt, col_disp = 0, byte_disp = 0, buff_cnt = *p_buff_cnt;
    for (unsigned i = 0; i < 2; i++)
    {
        int tmp;
        switch (i)
        {
        case 0:
            switch (context->status)
            {
            case XML_ERROR_UNEXPECTED_EOF:
            case XML_ERROR_UTF:
            case XML_ERROR_PROLOGUE:
            case XML_ERROR_COMPILER:
                tmp = snprintf(buff, buff_cnt, "%s", str[context->status]);
                break;
            case XML_ERROR_INVALID_SYMBOL:
                byte_disp = context->len, col_disp = 1; // Returning to the previous symbol
                tmp = snprintf(buff, buff_cnt, "%s \'%.*s\'", str[context->status], (int) context->len, context->str);
                break;
            case XML_ERROR_UNEXPECTED_TAG:
            case XML_ERROR_ENDING:
            case XML_ERROR_UNHANDLED_VALUE:
            case XML_ERROR_CONTROL:
                tmp = snprintf(buff, buff_cnt, "%s \"%.*s\"", str[context->status], (int) context->len, context->str);
                break;
            case XML_ERROR_RANGE:
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

static bool log_message_error_xml(struct log *restrict log, struct code_metric code_metric, struct text_metric *restrict text_metric, const char *path, char *str, uint32_t val, size_t len, enum xml_status status)
{
    return log_message(log, code_metric, MESSAGE_TYPE_ERROR, message_xml, &(struct xml_context) { .text_metric = text_metric, .path = path, .str = str, .val = val, .len = len, .status = status });
}

///////////////////////////////////////////////////////////////////////////////
//
//  XML syntax analyzer
//

// This is ONLY required for the line marked with '(*)'
_Static_assert(BLOCK_READ > 2, "'BLOCK_READ' constant is assumed to be greater than 2!");

#define MAX_PRINT 16 

#define STR_D_XML "<?xml " 
#define STR_D_VER "version" 
#define STR_D_ENC "encoding"
#define STR_D_STA "standalone"
#define STR_D_UTF "UTF-8"
#define STR_D_VLV "1.0"
#define STR_D_VLS "no"
#define STR_D_PRE "?>"

struct program_object *program_object_from_xml(const char *path, xml_node_selector_callback xml_node_selector, xml_att_selector_callback xml_att_selector, void *context, struct log *log)
{
    FILE *f = NULL;
    if (path)
    {
        f = fopen(path, "r");
        if (!f)
        {
            log_message_fopen(log, CODE_METRIC, MESSAGE_TYPE_ERROR, path, errno); 
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

    bool halt = 0;
    uint8_t quot = 0;
    size_t len = 0, ind = 0, dep = 0;
    uint32_t ctrl_val = 0;
    char buff[BLOCK_READ];
    uint8_t utf8_byte[UTF8_COUNT];
    uint8_t utf8_len = 0, utf8_context = 0;
    uint32_t utf8_val = 0;
    struct text_metric txt = { 0 }, str = { 0 }, ctr = { 0 }; // Text metrics        
    struct xml_att xml_att;
    struct xml_node xml_node;
    
    size_t rd = fread(buff, 1, sizeof(buff), f), pos = 0;    
    if (rd >= 3 && !strncmp(buff, "\xef\xbb\xbf", 3)) pos += 3, txt.byte += 3; // (*) Reading UTF-8 BOM if it is present
    if (pos == rd) halt = 1;
    
    enum {
        OFF_HD,
        
        STP_HD0 = OFF_HD, 
        STP_W00, STP_HD1, STP_W01, STP_EQ0, STP_W02, STP_QO0,
        
        OFF_QC,
        
        STP_QC0 = OFF_QC, 
        STP_HH0, STP_HS0, STP_W03, STP_HD2, STP_W04, STP_EQ1, STP_W05, STP_QO1,
        STP_QC1, STP_HH1, STP_HS1, STP_W06, STP_HD3, STP_W07, STP_EQ2, STP_W08,
        STP_QO2, STP_QC2, STP_HH2, STP_HS2, STP_W09, STP_HD4, STP_HE0, STP_HE1,
        
        OFF_LA,

        STP_W10 = OFF_LA,
        STP_LT0,

        OFF_SL,
        
        STP_SL0 = OFF_SL,
        STP_ST0, STP_TG0,
        
        OFF_LB,

        STP_W11 = OFF_LB,
        STP_EA0,
        
        OFF_EB,
        
        STP_EB0 = OFF_EB,        
        STP_ST1, STP_AH0, STP_W12, STP_EQ3, STP_W13, STP_QO3, STP_QC3, STP_AV0,

        OFF_LC,

        STP_W14 = OFF_LC,
        STP_LT1, STP_SL1, STP_ST2, STP_TG1, STP_ST3, STP_CT0, STP_EB1,
        
        STP_W15 = OFF_LC + STP_EB1 - OFF_EB, 
        STP_LT2, STP_SL2, STP_ST4,
        
        OFF_SQ,
        
        STP_SQ0 = OFF_SQ,
        STP_SQ1 = OFF_SQ + STP_QC1 - OFF_QC,
        STP_SQ2 = OFF_SQ + STP_QC2 - OFF_QC,
        STP_SQ3 = OFF_SQ + STP_QC3 - OFF_QC,
        STP_SA0, STP_SA1, STP_SA2, STP_SA3, STP_SA4, STP_SA5, STP_SA6, STP_SA7,
        
        OFF_CM, STP_CM0 = OFF_CM,
        OFF_CA, STP_CA0 = OFF_CA,
        OFF_CB, STP_CB0 = OFF_CB,
        OFF_CC, STP_CC0 = OFF_CC,

        STP_CM1 = OFF_CM + STP_SL1 - OFF_SL,
        STP_CA1 = OFF_CA + STP_SL1 - OFF_SL,
        STP_CB1 = OFF_CB + STP_SL1 - OFF_SL,
        STP_CC1 = OFF_CC + STP_SL1 - OFF_SL,

        STP_CM2 = OFF_CM + STP_SL2 - OFF_SL,
        STP_CA2 = OFF_CA + STP_SL2 - OFF_SL,
        STP_CB2 = OFF_CB + STP_SL2 - OFF_SL,
        STP_CC2 = OFF_CC + STP_SL2 - OFF_SL
    };
    
    uint32_t stp = STP_HD0;

    for (; rd; rd = fread(buff, 1, sizeof buff, f), pos = 0)
    {
        bool upd = 1;
        do {
            if (upd) // UTF-8 decoder coroutine
            {
                if (pos >= rd) break;
                
                if (utf8_decode(buff[pos], &utf8_val, utf8_byte, &utf8_len, &utf8_context))
                {
                    pos++, txt.byte++;
                    if (utf8_context) continue;
                    if (utf8_is_invalid(utf8_val, utf8_len)) halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, NULL, 0, 0, XML_ERROR_UTF);
                    else if (utf8_val == '\n') txt.line++, txt.col = 0; // Updating text metrics
                    else txt.col++;                 
                }
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, NULL, 0, 0, XML_ERROR_UTF);
            }
            else upd = 1;

            if (halt) break;

            switch (stp)
            {
                ///////////////////////////////////////////////////////////////
                //
                //  XML prologue machine
                //
            
            case STP_HD0: case STP_HD1: case STP_HD2: case STP_HD3: 
            case STP_HD4:
                if (utf8_val == (uint32_t) (STR_D_XML STR_D_VER STR_D_ENC STR_D_STA)[ind])
                {
                    switch (++ind)
                    {
                    case strlenof(STR_D_XML):
                    case strlenof(STR_D_XML STR_D_VER):
                    case strlenof(STR_D_XML STR_D_VER STR_D_ENC):
                    case strlenof(STR_D_XML STR_D_VER STR_D_ENC STR_D_STA):
                        stp++;
                        break;                    
                    }                
                }
                else
                {
                    switch (ind)
                    {
                    case strlenof(STR_D_XML STR_D_VER):
                        ind += strlenof(STR_D_ENC), stp = STP_HD3, upd = 0;
                        break;

                    case strlenof(STR_D_XML STR_D_VER STR_D_ENC):
                        ind += strlenof(STR_D_STA), stp = STP_HD4, upd = 0;
                        break;

                    case strlenof(STR_D_XML STR_D_VER STR_D_ENC STR_D_STA):
                        ind = 0, stp++, upd = 0;
                        break;

                    default:
                        halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, NULL, 0, 0, XML_ERROR_PROLOGUE);
                    }                    
                }
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Reading whitespace
                //

            case STP_W00: case STP_W01: case STP_W02: case STP_W03:
            case STP_W04: case STP_W05: case STP_W06: case STP_W07:
            case STP_W08: case STP_W09: case STP_W10: case STP_W11:
            case STP_W12: case STP_W13: case STP_W14: case STP_W15:
                if (!utf8_is_whitespace(utf8_val, utf8_len)) stp++, upd = 0;
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Reading '=' sign
                //

            case STP_EQ0: case STP_EQ1: case STP_EQ2: case STP_EQ3:
                if (utf8_val == '=') stp++;
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_val, utf8_len, XML_ERROR_INVALID_SYMBOL);
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Reading opening quote
                //

            case STP_QO0: case STP_QO1: case STP_QO2: case STP_QO3:
                quot = 0;
                switch (utf8_val)
                {
                case '\'':

                    quot++;
                case '\"':
                    str = txt, len = 0, stp++;
                    break;

                default:
                    halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_val, utf8_len, XML_ERROR_INVALID_SYMBOL);
                }
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Reading closing quote
                //

            case STP_QC0: case STP_QC1: case STP_QC2: case STP_QC3:
                if (utf8_val == (uint32_t) "\"\'"[quot]) stp++;
                else switch (utf8_val)
                {
                case '&':
                    ctr = txt;
                    stp += OFF_SQ - OFF_QC; // Routing to "Control sequence handling"
                    break;

                case '<':
                    halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_val, utf8_len, XML_ERROR_INVALID_SYMBOL);
                    break;

                default:
                    if (!array_test(&temp.buff, &temp.cap, sizeof(*temp.buff), 0, 0, ARG_SIZE(len, utf8_len, 1))) halt = 1, log_message_crt(log, CODE_METRIC, MESSAGE_TYPE_ERROR, errno);
                    else strncpy(temp.buff + len, (char *) utf8_byte, utf8_len), len += utf8_len;
                }
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Reading prologue attributes
                //

            case STP_HH0:
                if (len == strlenof(STR_D_VLV) && !strncmp(temp.buff, STR_D_VLV, len)) stp++, upd = 0;
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, temp.buff, 0, len, XML_ERROR_UNHANDLED_VALUE);
                break;

            case STP_HH1:
                if (len == strlenof(STR_D_UTF) && !Strnicmp(temp.buff, STR_D_UTF, len)) stp++, upd = 0;
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, temp.buff, 0, len, XML_ERROR_UNHANDLED_VALUE);
                break;

            case STP_HH2:
                if (len == strlenof(STR_D_VLS) && !strncmp(temp.buff, STR_D_VLS, len)) stp++, upd = 0;
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, temp.buff, 0, len, XML_ERROR_UNHANDLED_VALUE);
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Prologue routing
                //

            case STP_HS0: case STP_HS1: case STP_HS2:
                if (utf8_is_whitespace(utf8_val, utf8_len)) stp++;
                else ind = 0, stp = STP_HE0, upd = 0;
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Handling the end of XML prologue
                //

            case STP_HE0: case STP_HE1:
                if (utf8_val == (uint32_t) (STR_D_PRE)[ind]) ind++, stp++;
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, NULL, 0, 0, XML_ERROR_PROLOGUE);
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Reading '<' sign
                //
                        
            case STP_LT0: case STP_LT1: case STP_LT2:
                if (utf8_val == '<') stp++;
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_val, utf8_len, XML_ERROR_INVALID_SYMBOL);
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Tag begin handling
                //

            case STP_SL0: case STP_SL2:
                if (utf8_val == '!') ind = 0, stp += OFF_CM - OFF_SL;
                else str = txt, str.col--, str.byte -= utf8_len, len = 0, stp++, upd = 0;
                break;

            case STP_SL1:
                switch (utf8_val)
                {
                case '/':
                    str = txt, len = 0, stp = STP_ST3;
                    break;

                case '!':
                    ind = 0, stp += OFF_CM - OFF_SL;
                    break;

                default:
                    str = txt, str.col--, str.byte -= utf8_len, len = 0, stp++, upd = 0;
                }
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Tag or attribute name reading
                //
                
            case STP_ST0: case STP_ST1: case STP_ST2: case STP_ST3:
                if ((len ? utf8_is_xml_name_char : utf8_is_xml_name_start_char)(utf8_val, utf8_len))
                {
                    if (!array_test(&temp.buff, &temp.cap, sizeof(*temp.buff), 0, 0, ARG_SIZE(utf8_len, 1))) halt = 1, log_message_crt(log, CODE_METRIC, MESSAGE_TYPE_ERROR, errno);
                    else strncpy(temp.buff, (char *) utf8_byte, utf8_len), len += utf8_len;
                }
                else if (!len) halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_val, utf8_len, XML_ERROR_INVALID_SYMBOL);
                else stp++, upd = 0;
                break;
                
                ///////////////////////////////////////////////////////////////
                //
                //  Tag name handling for the first time
                //

            case STP_TG0:
                if (xml_node_selector(&xml_node, temp.buff, len, context))
                {
                    if (!array_init(&stack.frame, &stack.cap, 1, sizeof(*stack.frame), 0, 0)) halt = 1, log_message_crt(log, CODE_METRIC, MESSAGE_TYPE_ERROR, errno);
                    else
                    {
                        stack.frame[0] = (struct frame) { .obj = malloc(sizeof(*stack.frame[0].obj)), .len = len };
                        if (!stack.frame[0].obj) halt = 1, log_message_crt(log, CODE_METRIC, MESSAGE_TYPE_ERROR, errno);
                        else
                        {
                            if (!array_init(&name.buff, &name.cap, len + 1, sizeof(*name.buff), 0, 0)) halt = 1, log_message_crt(log, CODE_METRIC, MESSAGE_TYPE_ERROR, errno);
                            else
                            {
                                strcpy(name.buff, temp.buff);

                                *stack.frame[0].obj = (struct program_object) { .prologue = xml_node.prologue, .epilogue = xml_node.epilogue, .dispose = xml_node.dispose, .context = calloc(1, xml_node.sz) };
                                if (!stack.frame[0].obj->context) halt = 1, log_message_crt(log, CODE_METRIC, MESSAGE_TYPE_ERROR, errno);
                                else stp++, upd = 0;
                            }
                        }
                    }
                }
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, temp.buff, 0, len, XML_ERROR_UNEXPECTED_TAG);
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Tag name handling
                //
                
            case STP_TG1:
                if (xml_node_selector(&xml_node, temp.buff, len, context))
                {
                    if (!array_test(&stack.frame, &stack.cap, sizeof(*stack.frame), 0, 0, ARG_SIZE(dep))) halt = 1, log_message_crt(log, CODE_METRIC, MESSAGE_TYPE_ERROR, errno);
                    else
                    {
                        struct program_object *obj = stack.frame[dep - 1].obj;
                        if (!array_test(&obj->dsc, &obj->dsc_cnt, sizeof(*obj->dsc), 0, ARRAY_STRICT, ARG_SIZE(obj->dsc_cnt, 1))) halt = 1, log_message_crt(log, CODE_METRIC, MESSAGE_TYPE_ERROR, errno);
                        {

                            obj->dsc[obj->dsc_cnt] = (struct program_object) { .prologue = xml_node.prologue, .epilogue = xml_node.epilogue, .dispose = xml_node.dispose, .context = calloc(1, xml_node.sz) };
                            if (!obj->dsc[obj->dsc_cnt].context) halt = 1, log_message_crt(log, CODE_METRIC, MESSAGE_TYPE_ERROR, errno);
                            else
                            {
                                if (!array_test(&name.buff, &name.cap, sizeof(*name.buff), 0, 0, ARG_SIZE(stack.frame[dep - 1].off, stack.frame[dep - 1].len, 1))) halt = 1, log_message_crt(log, CODE_METRIC, MESSAGE_TYPE_ERROR, errno);
                                else
                                {
                                    strcpy(name.buff, temp.buff);
                                    stack.frame[dep] = (struct frame) { .obj = &obj->dsc[obj->dsc_cnt], .len = len, .off = stack.frame[dep - 1].off + stack.frame[dep - 1].len + 1 };
                                    obj->dsc_cnt++;
                                    stp = OFF_LB, upd = 0;
                                }
                            }
                        }
                    }
                }
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, temp.buff, 0, len, XML_ERROR_UNEXPECTED_TAG);
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Tag end handling
                // 

            case STP_EA0:
                if (utf8_val == '/') stp++;
                else if (utf8_val == '>') dep++, stp = OFF_LC;
                else str = txt, str.col--, str.byte -= utf8_len, len = 0, stp += 2, upd = 0;
                break;

            case STP_EB0: case STP_EB1:
                if (utf8_val == '>') stp += OFF_LC - OFF_EB;
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_val, utf8_len, XML_ERROR_INVALID_SYMBOL);
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Closing tag handling
                //

            case STP_CT0:
                if (len == stack.frame[dep].len && !strncmp(temp.buff, name.buff + stack.frame[dep].off, len)) stp = STP_EB0, upd = 0;
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, temp.buff, 0, len, XML_ERROR_ENDING);
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Selecting attribute
                //

            case STP_AH0:
                if (xml_att_selector(&xml_att, temp.buff, len, context, &ind))
                {
                    if (ind >= attb.cap)
                    {
                        if (!array_test(&attb.buff, &attb.cap, sizeof(*attb.buff), 0, 0, ARG_SIZE(UINT8_CNT(ind + 1)))) halt = 1, log_message_crt(log, CODE_METRIC, MESSAGE_TYPE_ERROR, errno);
                        else
                        {
                            uint8_bit_set(attb.buff, ind);
                            stp++, upd = 0;
                        }
                    }
                    else if (!uint8_bit_test_set(attb.buff, ind)) stp++, upd = 0;
                    else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, temp.buff, 0, len, XML_ERROR_DUPLICATED_ATTRIBUTE);
                }
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, temp.buff, 0, len, XML_ERROR_UNEXPECTED_ATTRIBUTE);
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Executing attribute handler
                //

            case STP_AV0:
                temp.buff[len] = '\0'; // There is always room for the zero-terminator
                if (!xml_att.handler(temp.buff, len, (char *) stack.frame[dep].obj->context + xml_att.offset, xml_att.context)) halt = 1, log_message_error_xml(log, CODE_METRIC, &str, path, temp.buff, 0, len, XML_ERROR_UNHANDLED_VALUE);
                else upd = 0, stp = OFF_LB;
                break;
                
                ///////////////////////////////////////////////////////////////
                //
                //  Control sequence handling
                //

            case STP_SQ3:
                if (utf8_val == '#') ctr.col++, ctr.byte++, stp++;
                else ind = 0, stp = STP_SA6, upd = 0;
                break;
                                
            case STP_SA0:
                if (utf8_val == 'x') ctr.col++, ctr.byte++, stp++;
                else stp = STP_SA3, upd = 0;
                break;

            case STP_SA1:
                if ('0' <= utf8_val && utf8_val <= '9') ctrl_val = utf8_val - '0', stp++;
                else if ('A' <= utf8_val && utf8_val <= 'F') ctrl_val = utf8_val - 'A' + 10, stp++;
                else if ('a' <= utf8_val && utf8_val <= 'f') ctrl_val = utf8_val - 'a' + 10, stp++;
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &ctr, path, 0, utf8_val, 0, XML_ERROR_INVALID_SYMBOL);
                break;

            case STP_SA2:
                if ('0' <= utf8_val && utf8_val <= '9') 
                { 
                    if (uint32_fused_mul_add(&ctrl_val, 16, utf8_val - '0')) halt = 1, log_message_error_xml(log, CODE_METRIC, &ctr, path, 0, utf8_val, 0, XML_ERROR_RANGE); 
                }
                else if ('A' <= utf8_val && utf8_val <= 'F') 
                { 
                    if (uint32_fused_mul_add(&ctrl_val, 16, utf8_val - 'A' + 10)) halt = 1, log_message_error_xml(log, CODE_METRIC, &ctr, path, 0, utf8_val, 0, XML_ERROR_RANGE);
                }
                else if ('a' <= utf8_val && utf8_val <= 'f') 
                { 
                    if (uint32_fused_mul_add(&ctrl_val, 16, utf8_val - 'a' + 10)) halt = 1, log_message_error_xml(log, CODE_METRIC, &ctr, path, 0, utf8_val, 0, XML_ERROR_RANGE);
                }
                else stp += 3, upd = 0;
                break;

            case STP_SA3:
                if ('0' <= utf8_val && utf8_val <= '9') ctrl_val = utf8_val - '0', stp++;
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &ctr, path, 0, utf8_val, 0, XML_ERROR_INVALID_SYMBOL);
                break;

            case STP_SA4:
                if ('0' <= utf8_val && utf8_val <= '9') 
                { 
                    if (uint32_fused_mul_add(&ctrl_val, 10, utf8_val - '0')) halt = 1, log_message_error_xml(log, CODE_METRIC, &ctr, path, 0, utf8_val, 0, XML_ERROR_RANGE);
                }
                else stp++, upd = 0;
                break;

            case STP_SA5:
                if (utf8_val == ';')
                {
                    if (ctrl_val >= UTF8_BOUND) halt = 1, log_message_error_xml(log, CODE_METRIC, &ctr, path, 0, utf8_val, 0, XML_ERROR_RANGE);
                    else
                    {
                        uint8_t ctrl_byte[6], ctrl_len;
                        utf8_encode(ctrl_val, ctrl_byte, &ctrl_len);
                        if (!array_test(&temp.buff, &temp.cap, sizeof(*temp.buff), 0, 0, ARG_SIZE(len, ctrl_len, 1))) halt = 1, log_message_crt(log, CODE_METRIC, MESSAGE_TYPE_ERROR, errno);
                        else strncpy(temp.buff + len, (char *) ctrl_byte, ctrl_len), len += ctrl_len, stp = STP_QC3;
                    }
                }
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &ctr, path, 0, utf8_val, 0, XML_ERROR_INVALID_SYMBOL);
                break;

            case STP_SA6:
                if ((ind ? utf8_is_xml_name_char : utf8_is_xml_name_start_char)(utf8_val, utf8_len))
                {
                    if (!array_test(&ctrl.buff, &ctrl.cap, sizeof(*ctrl.buff), 0, 0, ARG_SIZE(utf8_len))) halt = 1, log_message_crt(log, CODE_METRIC, MESSAGE_TYPE_ERROR, errno);
                    else strncpy(ctrl.buff, (char *) utf8_byte, utf8_len), ind += utf8_len;
                }
                else if (!ind) halt = 1, log_message_error_xml(log, CODE_METRIC, &ctr, path, 0, utf8_val, 0, XML_ERROR_INVALID_SYMBOL);
                else stp++, upd = 0;
                break;

            case STP_SA7:
                if (utf8_val == ';')
                {
                    size_t ctrl_ind = binary_search(ctrl.buff, ctrl_subs, sizeof(*ctrl_subs), countof(ctrl_subs), str_strl_stable_cmp_len, &ind);
                    if (ctrl_ind + 1)
                    {
                        struct strl subs = ctrl_subs[ctrl_ind].subs;
                        if (!array_test(&temp.buff, &temp.cap, sizeof(*temp.buff), 0, 0, ARG_SIZE(len, subs.len + 1))) halt = 1, log_message_crt(log, CODE_METRIC, MESSAGE_TYPE_ERROR, errno);
                        else
                        {
                            strncpy(temp.buff + len, subs.str, subs.len);
                            len += subs.len;
                            stp = STP_QC3;
                        }
                    }
                    else halt = 1, log_message_error_xml(log, CODE_METRIC, &ctr, path, 0, utf8_val, 0, XML_ERROR_CONTROL);
                }
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &ctr, path, 0, utf8_val, 0, XML_ERROR_INVALID_SYMBOL);
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Comment handling
                //

            case STP_CM0: case STP_CM1: case STP_CM2:
            case STP_CA0: case STP_CA1: case STP_CA2:
                if (utf8_val == '-') stp++;
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_val, utf8_len, XML_ERROR_INVALID_SYMBOL);
                break;

            case STP_CB0: case STP_CB1: case STP_CB2:
                if (ind == 2) ind = 0, stp++, upd = 0;
                else if (utf8_val == '-') ind++;
                else ind = 0;
                break;

            case STP_CC0: case STP_CC1: case STP_CC2:
                if (utf8_val == '>') stp -= OFF_CM - OFF_LA + OFF_CC - OFF_CM;
                else halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_val, utf8_len, XML_ERROR_INVALID_SYMBOL);
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Various stubs
                //

            case STP_SQ0: case STP_SQ1: case STP_SQ2: // Special sequences should not appear in the prologue
                halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, NULL, 0, 0, XML_ERROR_PROLOGUE);
                break;

            case STP_ST4:
                halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_val, utf8_len, XML_ERROR_INVALID_SYMBOL);
                break;

            default:
                halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_val, utf8_len, XML_ERROR_COMPILER);
            }          
        } while (!halt);
    }
    
    if (dep) halt = 1, log_message_error_xml(log, CODE_METRIC, &txt, path, utf8_byte, utf8_val, utf8_len, XML_ERROR_UNEXPECTED_EOF);
    if (halt && stack.frame) program_object_dispose(stack.frame[0].obj), stack.frame[0].obj = NULL;

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

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
    ERR_EOF, ERR_UTF, ERR_SYM, ERR_PRL,
    ERR_TAG, ERR_ATT, ERR_END, ERR_DUP,
    ERR_HAN, ERR_CTR, ERR_CMP, ERR_RAN
};

struct message_error_xml {
    struct message base;
    enum xml_status status;
    uint64_t byte;
    size_t col, row;
};

#define MESSAGE_ERROR_XML(Name, Len, Ind, Status) \
    ((struct message_error_xml) { \
        .base = MESSAGE(message_error_xml, MESSAGE_TYPE_WARNING), \
        .status = (Status), \
        .name = (Name), \
        .len = (Len), \
        .ind = (Ind), \
    })

#if 0

static size_t message_error_xml(char *buff, size_t buff_cnt, void *Context)
{
    struct message_error_xml *restrict context = Context;
    const char *str[] = {
        __FUNCTION__,
        "ERROR (%s): %s!\n",
        "ERROR (%s): Cannot open specified XML document \"%s\". %s!\n",
        "ERRPR (%s): Numeric value %" PRIu32 " is out of range (file \"%s\", line %zu, character %zu, byte %zu)!\n",
        "ERROR (%s): %s (file \"%s\", line %zu, character %zu, byte %zu)!\n",
        "ERROR (%s): %s \"%." TOSTRING(MAX_PRINT) "s%s\" (file \"%s\", line %zu, character %zu, byte %zu)!\n",
        "Unexpected end of file",
        "Incorrect UTF-8 byte sequence",
        "Invalid symbol",
        "Invalid XML prologue",
        "Invalid tag",
        "Invalid attribute",
        "Unexpected close tag",
        "Duplicated attribute",
        "Unable to handle value",
        "Invalid control sequence",
        "Compiler malfunction" // Not intended to happen
    };
    int res = context->status < countof(str) ? snprintf(buff, buff_cnt, "%s!\n", str[context->status]) : 0;
    return MAX(0, res);
}

#endif

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

struct program_object *program_object_from_xml(struct xml_node *sch, const char *path, struct log *log)
{
    FILE *f = NULL;
    if (path)
    {
        f = fopen(path, "r");
        if (!f)
        {
            log_message(log, &MESSAGE_ERROR_CRT(errno).base);
            return 0;
        }
    }
    else f = stdin;

    enum
    {
        STR_FN = 0, STR_FR_EG, STR_FR_EI, STR_FR_RN, 
        STR_FR_EX, STR_FR_ET, STR_M_EOF, STR_M_UTF, 
        STR_M_SYM, STR_M_PRL, STR_M_TAG, STR_M_ATT, 
        STR_M_END, STR_M_DUP, STR_M_HAN, STR_M_CTR, 
        STR_M_CMP
    };
    
    enum xml_status errm;

    struct { struct strl name; char ch; } ctrseq[] = { { STRI("amp"), '&' }, { STRI("apos"), '\'' }, { STRI("gt"), '>' }, { STRI("lt"), '<' }, { STRI("quot"), '\"' } };
        
    // Parser errors
    char buff[BLOCK_READ];
    uint8_t utf8_byte[UTF8_COUNT];

    struct { char *buff; size_t cap; } temp = { 0 }, ctrl = { 0 };
    struct { uint8_t *buff; size_t cap; } attb = { 0 };
    struct { struct frame { struct program_object *obj; struct xml_node *node; struct frame *anc; } *frame; size_t cap; } stack = { 0 };
            
    bool halt = 0;
    uint8_t quot = 0;
    size_t len = 0, ind = 0, dep = 0;
    uint32_t ctrl_val = 0;
    uint8_t utf8_len = 0, utf8_context = 0;
    uint32_t utf8_val = 0;
    struct { size_t line; size_t col; size_t byte; } txt = { 0 }, str = { 0 }, ctr = { 0 }; // Text metrics        
    struct att *att = NULL;
    
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
        for (bool upd = 1; !halt;)
        {
            if (upd) // UTF-8 decoder coroutine
            {
                if (pos >= rd) break;
                
                if (utf8_decode(buff[pos], &utf8_val, utf8_byte, &utf8_len, &utf8_context))
                {
                    pos++, txt.byte++;
                    if (utf8_context) continue;
                    if (utf8_is_invalid(utf8_val, utf8_len))
                    {
                        errm = ERR_UTF, halt = 1;
                        break;
                    }

                    if (utf8_val == '\n') txt.line++, txt.col = 0; // Updating text metrics
                    else txt.col++;                 
                }
                else
                {
                    errm = ERR_UTF, halt = 1;
                    break;
                }
            }
            else upd = 1;

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
                        errm = ERR_PRL, halt = 1;
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
                else errm = ERR_SYM, halt = 1;
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
                    errm = ERR_SYM, halt = 1;
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
                    errm = ERR_SYM, halt = 1;
                    break;

                default:
                    if (!array_test(&temp.buff, &temp.cap, 1, 0, 0, ARG_SIZE(len, utf8_len, 1))) goto error;
                    strncpy(temp.buff + len, (char *) utf8_byte, utf8_len), len += utf8_len;
                }
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Reading prologue attributes
                //

            case STP_HH0:
                if (len == strlenof(STR_D_VLV) && !strncmp(temp.buff, STR_D_VLV, len)) stp++, upd = 0;
                else errm = ERR_HAN, halt = 1;
                break;

            case STP_HH1:
                if (len == strlenof(STR_D_UTF) && !Strnicmp(temp.buff, STR_D_UTF, len)) stp++, upd = 0;
                else errm = ERR_HAN, halt = 1;
                break;

            case STP_HH2:
                if (len == strlenof(STR_D_VLS) && !strncmp(temp.buff, STR_D_VLS, len)) stp++, upd = 0;
                else errm = ERR_HAN, halt = 1;
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
                else errm = ERR_PRL, halt = 1;
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Reading '<' sign
                //
                        
            case STP_LT0: case STP_LT1: case STP_LT2:
                if (utf8_val == '<') stp++;
                else errm = ERR_SYM, halt = 1;
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
                if (!len)
                {
                    if (utf8_is_xml_name_start_char(utf8_val, utf8_len))
                    {
                        if (!array_test(&temp.buff, &temp.cap, 1, 0, 0, ARG_SIZE(utf8_len, 1))) goto error;
                        strncpy(temp.buff, (char *) utf8_byte, utf8_len), len += utf8_len;
                    }
                    else errm = ERR_SYM, halt = 1;
                }
                else if (utf8_is_xml_name_char(utf8_val, utf8_len))
                {
                    if (!array_test(&temp.buff, &temp.cap, 1, 0, 0, ARG_SIZE(len, utf8_len, 1))) goto error;
                    strncpy(temp.buff + len, (char *) utf8_byte, utf8_len), len += utf8_len;
                }
                else stp++, upd = 0;
                break;
                
                ///////////////////////////////////////////////////////////////
                //
                //  Tag name handling for the first time
                //

            case STP_TG0:
                if (len == sch->name.len && !strncmp(temp.buff, sch->name.str, len))
                {
                    if (!array_test(&stack.frame, &stack.cap, sizeof(*stack.frame), 0, 0, ARG_SIZE(stack.cap, 1))) goto error;
                    
                    stack.frame[0] = (struct frame) { .obj = malloc(sizeof *stack.frame[0].obj), .node = sch }; ;
                    if (!stack.frame[0].obj) goto error;

                    *stack.frame[0].obj = (struct program_object) { .prologue = sch->prologue, .epilogue = sch->epilogue, .dispose = sch->dispose, .context = calloc(1, sch->sz) };
                    if (!stack.frame[0].obj->context) goto error;
                                     
                    size_t attb_cnt = BYTE_CNT(sch->att_cnt);
                    if (!array_test(&attb.buff, &attb.cap, 1, 0, 0, ARG_SIZE(attb_cnt))) goto error;
                    memset(attb.buff, 0, attb_cnt);

                    stp++, upd = 0;
                }
                else errm = ERR_TAG, halt = 1;
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Tag name handling
                //
                
            case STP_TG1:
                for (struct frame *anc = &stack.frame[dep - 1];;)
                {
                    ind = binary_search(temp.buff, anc->node->dsc, sizeof(*anc->node->dsc), anc->node->dsc_cnt, str_strl_cmp_len, &len);

                    if (ind + 1)
                    {
                        struct xml_node *nod = &anc->node->dsc[ind];

                        if (!array_test(&stack.frame, &stack.cap, sizeof(*stack.frame), 0, 0, ARG_SIZE(dep, 1))) goto error;

                        stack.frame[dep - 1].obj->dsc = realloc(stack.frame[dep - 1].obj->dsc, (stack.frame[dep - 1].obj->dsc_cnt + 1) * sizeof *stack.frame[dep - 1].obj->dsc);
                        if (!stack.frame[dep - 1].obj->dsc) goto error;

                        stack.frame[dep - 1].obj->dsc[stack.frame[dep - 1].obj->dsc_cnt] = (struct program_object) { .prologue = nod->prologue, .epilogue = nod->epilogue, .dispose = nod->dispose, .context = calloc(1, nod->sz) };
                        if (!stack.frame[dep - 1].obj->dsc[stack.frame[dep - 1].obj->dsc_cnt].context) goto error;

                        stack.frame[dep] = (struct frame) { .obj = &stack.frame[dep - 1].obj->dsc[stack.frame[dep - 1].obj->dsc_cnt], .node = nod, .anc = anc };
                        stack.frame[dep - 1].obj->dsc_cnt++;

                        size_t attb_cnt = BYTE_CNT(nod->att_cnt);
                        if (!array_test(&attb.buff, &attb.cap, 1, 0, 0, ARG_SIZE(attb_cnt))) goto error;
                        memset(attb.buff, 0, attb_cnt), stp = OFF_LB, upd = 0;
                    }
                    else
                    {
                        anc = anc->anc;
                        if (anc) continue;

                        halt = 1, errm = ERR_TAG;
                    }

                    break;
                }               

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
                else errm = ERR_SYM, halt = 1;
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Close tag handling
                //

            case STP_CT0:
                if (!--dep)
                {
                    if (len == sch->name.len && !strncmp(temp.buff, sch->name.str, len)) stp = STP_EB1, upd = 0;
                    else halt = 1, errm = ERR_END;
                }
                else if (len == stack.frame[dep].node->name.len && !strncmp(temp.buff, stack.frame[dep].node->name.str, len)) stp = STP_EB0, upd = 0;
                else halt = 1, errm = ERR_END;
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Selecting attribute
                //

            case STP_AH0:
                ind = binary_search(temp.buff, stack.frame[dep].node->att, sizeof *stack.frame[dep].node->att, stack.frame[dep].node->att_cnt, str_strl_cmp_len, &len);
                att = stack.frame[dep].node->att + ind;

                if (ind + 1)
                {
                    if (!bit_test(attb.buff, ind)) bit_set(attb.buff, ind), stp++, upd = 0;
                    else errm = ERR_DUP, halt = 1;
                }
                else errm = ERR_ATT, halt = 1;
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Executing attribute handler
                //

            case STP_AV0:
                temp.buff[len] = '\0'; // There is always room for the zero-terminator
                if (!att->handler(temp.buff, len, (char *) stack.frame[dep].obj->context + att->offset, att->context)) errm = ERR_HAN, halt = 1;
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
                else errm = ERR_SYM, halt = 1;
                break;

            case STP_SA2:
                if ('0' <= utf8_val && utf8_val <= '9') { if (uint32_fused_mul_add(&ctrl_val, 16, utf8_val - '0')) errm = ERR_RAN, halt = 1; }
                else if ('A' <= utf8_val && utf8_val <= 'F') { if (uint32_fused_mul_add(&ctrl_val, 16, utf8_val - 'A' + 10)) errm = ERR_RAN, halt = 1; }
                else if ('a' <= utf8_val && utf8_val <= 'f') { if (uint32_fused_mul_add(&ctrl_val, 16, utf8_val - 'a' + 10)) errm = ERR_RAN, halt = 1; }
                else stp += 3, upd = 0;
                break;

            case STP_SA3:
                if ('0' <= utf8_val && utf8_val <= '9') ctrl_val = utf8_val - '0', stp++;
                else errm = ERR_SYM, halt = 1;
                break;

            case STP_SA4:
                if ('0' <= utf8_val && utf8_val <= '9') { if (uint32_fused_mul_add(&ctrl_val, 10, utf8_val - '0')) errm = ERR_RAN, halt = 1; }
                else stp++, upd = 0;
                break;

            case STP_SA5:
                if (utf8_val == ';')
                {
                    if (ctrl_val >= UTF8_BOUND) errm = ERR_RAN, halt = 1;
                    else
                    {
                        uint8_t ctrl_byte[6], ctrl_len;
                        utf8_encode(ctrl_val, ctrl_byte, &ctrl_len);
                        if (!array_test(&temp.buff, &temp.cap, 1, 0, 0, ARG_SIZE(len, ctrl_len, 1))) goto error;
                        strncpy(temp.buff + len, (char *) ctrl_byte, ctrl_len), len += ctrl_len, stp = STP_QC3;
                    }
                }
                else errm = ERR_SYM, halt = 1;
                break;

            case STP_SA6:
                if (!ind)
                {
                    if (utf8_is_xml_name_start_char(utf8_val, utf8_len))
                    {
                        if (!array_test(&ctrl.buff, &ctrl.cap, 1, 0, 0, ARG_SIZE(utf8_len))) goto error;
                        strncpy(ctrl.buff, (char *) utf8_byte, utf8_len), ind += utf8_len;
                    }
                    else errm = ERR_SYM, halt = 1;
                }
                else if (utf8_is_xml_name_char(utf8_val, utf8_len))
                {
                    if (!array_test(&ctrl.buff, &ctrl.cap, 1, 0, 0, ARG_SIZE(ind, utf8_len))) goto error;
                    strncpy(ctrl.buff + ind, (char *) utf8_byte, utf8_len), ind += utf8_len;
                }
                else stp++, upd = 0;
                break;

            case STP_SA7:
                if (utf8_val == ';')
                {
                    size_t ctrind = binary_search(ctrl.buff, ctrseq, sizeof ctrseq[0], countof(ctrseq), str_strl_cmp_len, &ind);
                    if (ctrind + 1)
                    {
                        if (!array_test(&temp.buff, &temp.cap, 1, 0, 0, ARG_SIZE(len, 2))) goto error;
                        temp.buff[len++] = ctrseq[ctrind].ch, stp = STP_QC3;
                    }
                    else errm = ERR_CTR, halt = 1;
                }
                else errm = ERR_SYM, halt = 1;
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Comment handling
                //

            case STP_CM0: case STP_CM1: case STP_CM2:
            case STP_CA0: case STP_CA1: case STP_CA2:
                if (utf8_val == '-') stp++;
                else errm = ERR_SYM, halt = 1;
                break;

            case STP_CB0: case STP_CB1: case STP_CB2:
                if (ind == 2) ind = 0, stp++, upd = 0;
                else if (utf8_val == '-') ind++;
                else ind = 0;
                break;

            case STP_CC0: case STP_CC1: case STP_CC2:
                if (utf8_val == '>') stp -= OFF_CM - OFF_LA + OFF_CC - OFF_CM;
                else errm = ERR_SYM, halt = 1;
                break;

                ///////////////////////////////////////////////////////////////
                //
                //  Various stubs
                //

            case STP_SQ0: case STP_SQ1: case STP_SQ2:
                errm = ERR_PRL, halt = 1;
                break;

            case STP_ST4:
                errm = ERR_SYM, halt = 1;
                break;

            default:
                errm = ERR_CMP, halt = 1;
            }          
        }

        if (halt) break;
    }
    
    if (halt || dep) // Compiler errors
    {
        switch (errm)
        {
        case ERR_SYM:
            txt.byte -= utf8_len, txt.col--;

        case ERR_EOF: case ERR_UTF: case ERR_PRL: case ERR_CMP:
            //logMsg(inf, strings[STR_FR_EX], strings[STR_FN], strings[errm], input, txt.line + 1, txt.col + 1, txt.byte + 1);
            break;
        
        case ERR_TAG: case ERR_ATT: case ERR_END: case ERR_DUP:
        case ERR_HAN:
            //if (!dynamicArrayTest((void **) &temp.buff, &temp.cap, 1, len + 1)) goto error;
            //temp.buff[len] = '\0';

            //logMsg(inf, strings[STR_FR_ET], strings[STR_FN], strings[errm], temp.buff, len > MAX_PRINT ? "..." : "", input, str.line + 1, str.col + 1, str.byte + 1);
            break;

        case ERR_CTR:
            //if (!dynamicArrayTest((void **) &ctrl.buff, &ctrl.cap, 1, ind + 1)) goto error;
            //ctrl.buff[ind] = '\0';

            //logMsg(inf, strings[STR_FR_ET], strings[STR_FN], strings[errm], ctrl.buff, ind > MAX_PRINT ? "..." : "", input, ctr.line + 1, ctr.col + 1, ctr.byte + 1);
            break;

        case ERR_RAN:
            //logMsg(inf, strings[STR_FR_RN], strings[STR_FN], ctrl_val, input, ctr.line + 1, ctr.col + 1, ctr.byte + 1);
            break;
        }

        for (;;) // System errors
        {
            break;

        error:
            //strerror_s(error, sizeof error, errno);
            //logMsg(inf, strings[STR_FR_EG], strings[STR_FN], error);
            break;
        }

        if (stack.frame) program_object_dispose(stack.frame[0].obj), stack.frame[0].obj = NULL;
    }
    
    if (f != stdin) fclose(f);    
    struct program_object *res = stack.frame ? stack.frame[0].obj : NULL;
    free(stack.frame);
    free(attb.buff);
    free(ctrl.buff);
    free(temp.buff);
    return res;
}

bool xml_node_selector()
{
    return 1;
}

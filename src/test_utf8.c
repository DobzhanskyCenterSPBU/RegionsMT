#include "test.h"
#include "test_utf8.h"
#include "utf8.h"

#include <string.h>
#include <stdlib.h>
#include <wchar.h>

DECLARE_PATH

#define UTF8I(s) ((uint8_t *) s), strlenof((s))
#define UTF16I(...) (uint16_t []) { __VA_ARGS__ }, countof(((uint16_t []) { __VA_ARGS__ }))

struct utf8_test { uint32_t val; uint8_t *utf8; uint8_t utf8_len; uint16_t *utf16; uint8_t utf16_len; };

bool utf8_test_len(struct utf8_test *in, union message_test *p_error)
{
    uint8_t len = utf8_len(in->val);
    if (len != in->utf8_len) p_error-> = MESSAGE_ERROR_TEST(i);
    else return 1;
    return 0;
}


bool test_utf8(struct queue *queue_message, union message_error_internal_test *)
{
    struct { uint32_t val; uint8_t *utf8; uint8_t utf8_len; uint16_t *utf16; uint8_t utf16_len; } in[] =
    {
        { 0x0061, UTF8I("a"), UTF16I(0x0061) },
        { 0x00BC, UTF8I("\xC2\xBC"), UTF16I(0x00BC) },
        { 0x0391, UTF8I("\xCE\x91"), UTF16I(0x0391) },
        { 0x0430, UTF8I("\xD0\xB0"), UTF16I(0x0430) },
        { 0x05D0, UTF8I("\xD7\x90"), UTF16I(0x05D0) },
        { 0x1D2C, UTF8I("\xE1\xB4\xAC"), UTF16I(0x1D2C) },
        { 0x2016, UTF8I("\xE2\x80\x96"), UTF16I(0x2016) },
        { 0x2102, UTF8I("\xE2\x84\x82"), UTF16I(0x2102) },
        { 0x2155, UTF8I("\xE2\x85\x95"), UTF16I(0x2155) },
        { 0x222D, UTF8I("\xE2\x88\xAD"), UTF16I(0x222D) },
        { 0x2460, UTF8I("\xE2\x91\xA0"), UTF16I(0x2460) },
        { 0x2591, UTF8I("\xE2\x96\x91"), UTF16I(0x2591) },
        { 0xF000, UTF8I("\xEF\x80\x80"), UTF16I(0xF000) },
        { 0xFF21, UTF8I("\xEF\xBC\xA1"), UTF16I(0xFF21) },
        { 0x14400, UTF8I("\xF0\x94\x90\x80"), UTF16I(0xD811, 0xDC00) },
        { 0x1880F, UTF8I("\xF0\x98\xA0\x8F"), UTF16I(0xD822, 0xDC0F) },
        { 0x1E000, UTF8I("\xF0\x9E\x80\x80"), UTF16I(0xD838, 0xDC00) },
        { 0x10ffff, UTF8I("\xF4\x8F\xBF\xBF"), UTF16I(0xDBFF, 0xDFFF) },
        { 0x200000, UTF8I("\xF8\x88\x80\x80\x80") },
        { 0x200001, UTF8I("\xF8\x88\x80\x80\x81") },
        { 0x241041, UTF8I("\xF8\x89\x81\x81\x81") },
        { 0x0, UTF8I("\0"), UTF16I(0) },
        { 0x7F, UTF8I("\x7F"), UTF16I(0x7F)},
        { 0x80, UTF8I("\xC2\x80"), UTF16I(0x80)},
        { 0x7FF, UTF8I("\xDF\xBF"), UTF16I(0x7FF)},
        { 0x800, UTF8I("\xE0\xA0\x80"), UTF16I(0x800) },
        { 0xFFFF, UTF8I("\xEF\xBF\xBF"), UTF16I(0xFFFF) },
        { 0x10000, UTF8I("\xF0\x90\x80\x80"), UTF16I(0xD800, 0xDC00) },
        { 0x1FFFFF, UTF8I("\xF7\xBF\xBF\xBF") },
        { 0x200000, UTF8I("\xF8\x88\x80\x80\x80") },
        { 0x3FFFFFF, UTF8I("\xFB\xBF\xBF\xBF\xBF") },
        { 0x4000000, UTF8I("\xFC\x84\x80\x80\x80\x80") },
        { 0x7FFFFFFF, UTF8I("\xFD\xBF\xBF\xBF\xBF\xBF") }
    };

    bool succ = 1;

    for (size_t i = 0; i < countof(in); i++)
    {
        uint8_t byte[UTF8_COUNT];
        
        
        utf8_encode(in[i].val, byte, &len);
        if (strncmp((char *) in[i].utf8, (char *) byte, len) || len != in[i].utf8_len) succ &= queue_enqueue(queue_message, 1, &MESSAGE_ERROR_TEST(i), 1);
        else
        {
            uint8_t context = 0;
            uint32_t val = 0;
            len = 0;
            for (size_t j = 0; j < in[i].utf8_len; j++)
                if (!utf8_decode(in[i].utf8[j], &val, byte, &len, &context)) return 0;
            if (strncmp((char *) in[i].utf8, (char *) byte, len) || in[i].val != val || len != in[i].utf8_len) return 0;

            if (in[i].val < UTF8_BOUND)
            {
                uint16_t word[UTF16_COUNT];
                utf16_encode(in[i].val, word, &len);
                if (len != in[i].utf16_len) return 0;
                for (size_t j = 0; j < len; j++)
                    if (in[i].utf16[j] != word[j]) return 0;

                context = 0;
                val = 0;
                len = 0;
                for (size_t j = 0; j < in[i].utf16_len; j++)
                    if (!utf16_decode(in[i].utf16[j], &val, word, &len, &context)) return 0;

                if (len != in[i].utf16_len || in[i].val != val) return 0;
                for (size_t j = 0; j < len; j++)
                    if (in[i].utf16[j] != word[j]) return 0;
            }
        }
    }

    if (succ);
    //message-> = MESSAGE_INFO_TEST();
    return succ;
}
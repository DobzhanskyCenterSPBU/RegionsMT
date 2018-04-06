#include "test_utf8.h"
#include "utf8.h"
#include "memory.h"

#include <string.h>
#include <stdlib.h>

DECLARE_PATH

#define UTF8I(...) ARG(uint8_t, __VA_ARGS__)
#define UTF16I(...) ARG(uint16_t, __VA_ARGS__)

#define MESSAGE_ERROR_UTF8_TEST(Obituary) \
    ((struct message_error_utf8_test) { \
        .base = MESSAGE(message_error_utf8_test, MESSAGE_TYPE_ERROR), \
        .obituary = (Obituary), \
    })

static size_t message_error_utf8_test(char *buff, size_t buff_cnt, void *Context)
{
    struct message_error_utf8_test *restrict context = Context;
    const char *str[] = { 
        "Incorrect length of the UTF-8 byte sequence",
        "Incorrect UTF-8 byte sequence",
        "Incorrect Unicode value for the UTF-8 byte sequence",
        "Incorrect length of the UTF-16 word sequence",
        "Incorrect UTF-16 word sequence",
        "Incorrect Unicode value for the UTF-16 word sequence",
        "Internal error"
    };
    int res = context->obituary < countof(str) ? snprintf(buff, buff_cnt, "%s!\n", str[context->obituary]) : 0;
    return MAX(0, res);
}

bool utf8_test(void *dst, size_t *p_context, struct message *p_message)
{
    (void) p_message;

    struct utf8_test data[] = {
        { 0x0061, UTF8I('a'), UTF16I(0x0061) },
        { 0x00BC, UTF8I('\xC2', '\xBC'), UTF16I(0x00BC) },
        { 0x0391, UTF8I('\xCE', '\x91'), UTF16I(0x0391) },
        { 0x0430, UTF8I('\xD0', '\xB0'), UTF16I(0x0430) },
        { 0x05D0, UTF8I('\xD7', '\x90'), UTF16I(0x05D0) },
        { 0x1D2C, UTF8I('\xE1', '\xB4', '\xAC'), UTF16I(0x1D2C) },
        { 0x2016, UTF8I('\xE2', '\x80', '\x96'), UTF16I(0x2016) },
        { 0x2102, UTF8I('\xE2', '\x84', '\x82'), UTF16I(0x2102) },
        { 0x2155, UTF8I('\xE2', '\x85', '\x95'), UTF16I(0x2155) },
        { 0x222D, UTF8I('\xE2', '\x88', '\xAD'), UTF16I(0x222D) },
        { 0x2460, UTF8I('\xE2', '\x91', '\xA0'), UTF16I(0x2460) },
        { 0x2591, UTF8I('\xE2', '\x96', '\x91'), UTF16I(0x2591) },
        { 0xf000, UTF8I('\xEF', '\x80', '\x80'), UTF16I(0xF000) },
        { 0xFF21, UTF8I('\xEF', '\xBC', '\xA1'), UTF16I(0xFF21) },
        { 0x14400, UTF8I('\xF0', '\x94', '\x90', '\x80'), UTF16I(0xD811, 0xDC00) },
        { 0x1880F, UTF8I('\xF0', '\x98', '\xA0', '\x8F'), UTF16I(0xD822, 0xDC0F) },
        { 0x1E000, UTF8I('\xF0', '\x9E', '\x80', '\x80'), UTF16I(0xD838, 0xDC00) },
        { 0x10ffff, UTF8I('\xF4', '\x8F', '\xBF', '\xBF'), UTF16I(0xDBFF, 0xDFFF) },
        { 0x200000, UTF8I('\xF8', '\x88', '\x80', '\x80', '\x80') },
        { 0x200001, UTF8I('\xF8', '\x88', '\x80', '\x80', '\x81') },
        { 0x241041, UTF8I('\xF8', '\x89', '\x81', '\x81', '\x81') },
        { 0x0, UTF8I('\0'), UTF16I(0) },
        { 0x7F, UTF8I('\x7F'), UTF16I(0x7F)},
        { 0x80, UTF8I('\xC2', '\x80'), UTF16I(0x80)},
        { 0x7FF, UTF8I('\xDF', '\xBF'), UTF16I(0x7FF)},
        { 0x800, UTF8I('\xE0', '\xA0', '\x80'), UTF16I(0x800) },
        { 0xFFFF, UTF8I('\xEF', '\xBF', '\xBF'), UTF16I(0xFFFF) },
        { 0x10000, UTF8I('\xF0', '\x90', '\x80', '\x80'), UTF16I(0xD800, 0xDC00) },
        { 0x1FFFFF, UTF8I('\xF7', '\xBF', '\xBF', '\xBF') },
        { 0x200000, UTF8I('\xF8', '\x88', '\x80', '\x80', '\x80') },
        { 0x3FFFFFF, UTF8I('\xFB', '\xBF', '\xBF', '\xBF', '\xBF') },
        { 0x4000000, UTF8I('\xFC', '\x84', '\x80', '\x80', '\x80', '\x80') },
        { 0x7FFFFFFF, UTF8I('\xFD', '\xBF', '\xBF', '\xBF', '\xBF', '\xBF') }
    };
    
    if (*p_context < countof(data)) memcpy(dst, data + *p_context, sizeof(*data)), ++*p_context;
    else *p_context = 0;
    return 1;
}

bool utf8_test_len(void *In, struct message *p_message)
{
    struct utf8_test *restrict in = In;
    uint8_t len = utf8_len(in->val);
    if (len != in->utf8_len) *p_message = MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF8_LEN).base;
    else return 1;
    return 0;
}

bool utf8_test_encode(void *In, struct message *p_message)
{
    struct utf8_test *restrict in = In;
    uint8_t byte[UTF8_COUNT], len;
    utf8_encode(in->val, byte, &len);
    if (strncmp((char *) in->utf8, (char *) byte, len) || len != in->utf8_len) *p_message = MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF8_ENCODE).base;
    else return 1;
    return 0;
}

bool utf8_test_decode(void *In, struct message *p_message)
{
    struct utf8_test *restrict in = In;
    uint8_t byte[UTF8_COUNT], context = 0, len, ind = 0;
    uint32_t val;
    for (; ind < in->utf8_len; ind++)
        if (!utf8_decode(in->utf8[ind], &val, byte, &len, &context)) break;
    if (ind < in->utf8_len) *p_message = MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF8_INTERNAL).base;
    else
        if (strncmp((char *) in->utf8, (char *) byte, len) || in->val != val || len != in->utf8_len) *p_message = MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF8_DECODE).base;
        else return 1;
    return 0;
}

bool utf16_test_encode(void *In, struct message *p_message)
{
    struct utf8_test *restrict in = In;
    if (in->val < UTF8_BOUND)
    {
        uint16_t word[UTF16_COUNT];
        uint8_t len;
        utf16_encode(in->val, word, &len);
        if (len != in->utf16_len) *p_message = MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF16_LEN).base;
        else
        {
            uint8_t ind = 0;
            for (ind = 0; ind < len; ind++)
                if (in->utf16[ind] != word[ind]) break;
            if (ind < len) *p_message = MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF16_ENCODE).base;
            else return 1;
        }
    }
    else return 1;
    return 0;
}

bool utf16_test_decode(void *In, struct message *p_message)
{
    struct utf8_test *restrict in = In;
    if (in->val < UTF8_BOUND)
    {
        uint16_t word[UTF16_COUNT];
        uint8_t context = 0, len, ind = 0;
        uint32_t val;
        for (size_t ind = 0; ind < in->utf16_len; ind++)
            if (!utf16_decode(in->utf16[ind], &val, word, &len, &context)) break;
        if (ind < in->utf16_len) *p_message = MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF16_INTERNAL).base;
        else
        {
            if (len != in->utf16_len || in->val != val) *p_message = MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF16_DECODE).base;
            else
            {
                uint8_t ind = 0;
                for (ind = 0; ind < len; ind++)
                    if (in->utf16[ind] != word[ind]) break;
                if (ind < len) *p_message = MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF16_DECODE).base;
                else return 1;
            }
        }
    }
    else return 1;
    return 0;   
}
#include "test_utf8.h"
#include "memory.h"

#include <string.h>
#include <stdlib.h>

DECLARE_PATH

#define UTF8I(...) INIT(uint8_t, __VA_ARGS__)
#define UTF16I(...) INIT(uint16_t, __VA_ARGS__)

enum utf8_test_obituary {
    UTF8_TEST_OBITUARY_UTF8_LEN = 0,
    UTF8_TEST_OBITUARY_UTF8_ENCODE,
    UTF8_TEST_OBITUARY_UTF8_DECODE,
    UTF8_TEST_OBITUARY_UTF8_INTERNAL,
    UTF8_TEST_OBITUARY_UTF16_LEN,
    UTF8_TEST_OBITUARY_UTF16_ENCODE,
    UTF8_TEST_OBITUARY_UTF16_DECODE,
    UTF8_TEST_OBITUARY_UTF16_INTERNAL
};

struct message_error_utf8_test {
    struct message base;
    enum utf8_test_obituary status;
};

#define MESSAGE_ERROR_UTF8_TEST(Obituary) \
    ((struct message_error_utf8_test) { \
        .base = MESSAGE(message_error_utf8_test, MESSAGE_TYPE_ERROR), \
        .status = (Obituary), \
    })

static size_t message_error_utf8_test(char *buff, size_t buff_cnt, void *Context)
{
    struct message_error_utf8_test *restrict context = Context;
    const char *str[] = { 
        "Incorrect length of the UTF-8 byte sequence",
        "Incorrect UTF-8 byte sequence",
        "Incorrect Unicode value of the UTF-8 byte sequence",
        "Internal error",
        "Incorrect length of the UTF-16 word sequence",
        "Incorrect UTF-16 word sequence",
        "Incorrect Unicode value of the UTF-16 word sequence",
        "Internal error"
    };
    int res = context->status < countof(str) ? snprintf(buff, buff_cnt, "%s!\n", str[context->status]) : 0;
    return MAX(0, res);
}

bool test_utf8_generator(void *dst, size_t *p_context, struct log *log)
{
    (void) log;
    struct test_utf8 data[] = {
        { 0x0, UTF8I('\0'), UTF16I(0) },
        { 0x3F, UTF8I('?'), UTF16I(0x3F) },
        { 0x40, UTF8I('@'), UTF16I(0x40) },
        { 0x7F, UTF8I('\x7F'), UTF16I(0x7F)},
        { 0x80, UTF8I('\xC2', '\x80'), UTF16I(0x80)},
        { 0x3FF, UTF8I('\xCF', '\xBF'), UTF16I(0x3FF) },
        { 0x400, UTF8I('\xD0', '\x80'), UTF16I(0x400) },
        { 0x7FF, UTF8I('\xDF', '\xBF'), UTF16I(0x7FF)},
        { 0x800, UTF8I('\xE0', '\xA0', '\x80'), UTF16I(0x800) },
        { 0x3FFF, UTF8I('\xE3', '\xBF', '\xBF'), UTF16I(0x3FFF) },
        { 0x4000, UTF8I('\xE4', '\x80', '\x80'), UTF16I(0x4000) },
        { 0xFFFF, UTF8I('\xEF', '\xBF', '\xBF'), UTF16I(0xFFFF) },
        { 0x10000, UTF8I('\xF0', '\x90', '\x80', '\x80'), UTF16I(0xD800, 0xDC00) },
        { 0x10ffff, UTF8I('\xF4', '\x8F', '\xBF', '\xBF'), UTF16I(0xDBFF, 0xDFFF) }, // Last valid UTF-16 code
        { 0x110000, UTF8I('\xF4', '\x90', '\x80', '\x80') },
        { 0x1FFFFF, UTF8I('\xF7', '\xBF', '\xBF', '\xBF') },
        { 0x200000, UTF8I('\xF8', '\x88', '\x80', '\x80', '\x80') },
        { 0x3FFFFFF, UTF8I('\xFB', '\xBF', '\xBF', '\xBF', '\xBF') },
        { 0x4000000, UTF8I('\xFC', '\x84', '\x80', '\x80', '\x80', '\x80') },
        { 0x7FFFFFFF, UTF8I('\xFD', '\xBF', '\xBF', '\xBF', '\xBF', '\xBF') }
    };    
    size_t ind = *p_context;
    if (ind < countof(data)) memcpy(dst, data + ind, sizeof(*data)), ++*p_context;
    else *p_context = 0;
    return 1;
}

bool test_utf8_len(void *In, struct log *log)
{
    struct test_utf8 *restrict in = In;
    uint8_t len = utf8_len(in->val);
    if (len != in->utf8_len) log_message(log, &MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF8_LEN).base);
    else return 1;
    return 0;
}

bool test_utf8_encode(void *In, struct log *log)
{
    struct test_utf8 *restrict in = In;
    uint8_t byte[UTF8_COUNT], len;
    utf8_encode(in->val, byte, &len);
    if (strncmp((char *) in->utf8, (char *) byte, len) || len != in->utf8_len) log_message(log, &MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF8_ENCODE).base);
    else return 1;
    return 0;
}

bool test_utf8_decode(void *In, struct log *log)
{
    struct test_utf8 *restrict in = In;
    uint8_t byte[UTF8_COUNT], context = 0, len, ind = 0;
    uint32_t val;
    for (; ind < in->utf8_len; ind++)
        if (!utf8_decode(in->utf8[ind], &val, byte, &len, &context)) break;
    if (ind < in->utf8_len) log_message(log, &MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF8_INTERNAL).base);
    else
        if (strncmp((char *) in->utf8, (char *) byte, len) || in->val != val || len != in->utf8_len) log_message(log, &MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF8_DECODE).base);
        else return 1;
    return 0;
}

bool test_utf16_encode(void *In, struct log *log)
{
    struct test_utf8 *restrict in = In;
    if (in->val < UTF8_BOUND)
    {
        uint16_t word[UTF16_COUNT];
        uint8_t len;
        utf16_encode(in->val, word, &len);
        if (len != in->utf16_len) log_message(log, &MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF16_LEN).base);
        else
        {
            uint8_t ind = 0;
            for (ind = 0; ind < len; ind++)
                if (in->utf16[ind] != word[ind]) break;
            if (ind < len) log_message(log, &MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF16_ENCODE).base);
            else return 1;
        }
    }
    else return 1;
    return 0;
}

bool test_utf16_decode(void *In, struct log *log)
{
    struct test_utf8 *restrict in = In;
    if (in->val < UTF8_BOUND)
    {
        uint16_t word[UTF16_COUNT];
        uint8_t context = 0, len, ind = 0;
        uint32_t val;
        for (; ind < in->utf16_len; ind++)
            if (!utf16_decode(in->utf16[ind], &val, word, &len, &context)) break;
        if (ind < in->utf16_len) log_message(log, &MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF16_INTERNAL).base);
        else
        {
            if (len != in->utf16_len || in->val != val) log_message(log, &MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF16_DECODE).base);
            else
            {
                uint8_t ind = 0;
                for (ind = 0; ind < len; ind++)
                    if (in->utf16[ind] != word[ind]) break;
                if (ind < len) log_message(log, &MESSAGE_ERROR_UTF8_TEST(UTF8_TEST_OBITUARY_UTF16_DECODE).base);
                else return 1;
            }
        }
    }
    else return 1;
    return 0;   
}
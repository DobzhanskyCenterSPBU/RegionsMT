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
    UTF8_TEST_OBITUARY_UTF16_INTERNAL,
};

static bool log_message_error_utf8_test(struct log *restrict log, struct code_metric code_metric, enum utf8_test_obituary obituary)
{
    static const char *str[] = {
        "Incorrect length of the UTF-8 byte sequence",
        "Incorrect UTF-8 byte sequence",
        "Incorrect Unicode value of the UTF-8 byte sequence",
        "Internal error",
        "Incorrect length of the UTF-16 word sequence",
        "Incorrect UTF-16 word sequence",
        "Incorrect Unicode value of the UTF-16 word sequence",
        "Internal error"
    };
    return log_message_generic(log, code_metric, MESSAGE_ERROR, "%s!\n", str[obituary]);
}

bool test_utf8_generator(void *dst, size_t *p_context, struct log *log)
{
    (void) log;
    const struct test_utf8 data[] = {
        { 0x0, UTF8I('\0'), UTF16I(0) },
        { 0x3F, UTF8I('?'), UTF16I(0x3F) },
        { 0x40, UTF8I('@'), UTF16I(0x40) },
        { 0x7F, UTF8I(0x7F), UTF16I(0x7F)},
        { 0x80, UTF8I(0xC2, 0x80), UTF16I(0x80)},
        { 0x3FF, UTF8I(0xCF, 0xBF), UTF16I(0x3FF) },
        { 0x400, UTF8I(0xD0, 0x80), UTF16I(0x400) },
        { 0x7FF, UTF8I(0xDF, 0xBF), UTF16I(0x7FF)},
        { 0x800, UTF8I(0xE0, 0xA0, 0x80), UTF16I(0x800) },
        { 0x3FFF, UTF8I(0xE3, 0xBF, 0xBF), UTF16I(0x3FFF) },
        { 0x4000, UTF8I(0xE4, 0x80, 0x80), UTF16I(0x4000) },
        { 0xFFFF, UTF8I(0xEF, 0xBF, 0xBF), UTF16I(0xFFFF) },
        { 0x10000, UTF8I(0xF0, 0x90, 0x80, 0x80), UTF16I(0xD800, 0xDC00) },
        { 0x10ffff, UTF8I(0xF4, 0x8F, 0xBF, 0xBF), UTF16I(0xDBFF, 0xDFFF) }, // Last valid UTF-16 code
        { 0x110000, UTF8I(0xF4, 0x90, 0x80, 0x80) },
        { 0x1FFFFF, UTF8I(0xF7, 0xBF, 0xBF, 0xBF) },
        { 0x200000, UTF8I(0xF8, 0x88, 0x80, 0x80, 0x80) },
        { 0x3FFFFFF, UTF8I(0xFB, 0xBF, 0xBF, 0xBF, 0xBF) },
        { 0x4000000, UTF8I(0xFC, 0x84, 0x80, 0x80, 0x80, 0x80) },
        { 0x7FFFFFFF, UTF8I(0xFD, 0xBF, 0xBF, 0xBF, 0xBF, 0xBF) }
    };    
    memcpy(dst, data + *p_context, sizeof(*data));
    if (++*p_context >= countof(data)) *p_context = 0;
    return 1;
}

bool test_utf8_len(void *In, struct log *log)
{
    struct test_utf8 *restrict in = In;
    uint8_t len = utf8_len(in->val);
    if (len != in->utf8_len) log_message_error_utf8_test(log, CODE_METRIC, UTF8_TEST_OBITUARY_UTF8_LEN);
    else return 1;
    return 0;
}

bool test_utf8_encode(void *In, struct log *log)
{
    struct test_utf8 *restrict in = In;
    uint8_t byte[UTF8_COUNT], len;
    utf8_encode(in->val, byte, &len);
    if (strncmp((char *) in->utf8, (char *) byte, len) || len != in->utf8_len) log_message_error_utf8_test(log, CODE_METRIC, UTF8_TEST_OBITUARY_UTF8_ENCODE);
    else return 1;
    return 0;
}

bool test_utf8_decode(void *In, struct log *log)
{
    struct test_utf8 *restrict in = In;
    uint8_t byte[UTF8_COUNT], context = 0, len = 0, ind = 0;
    uint32_t val = 0;
    for (; ind < in->utf8_len; ind++)
        if (!utf8_decode(in->utf8[ind], &val, byte, &len, &context)) break;
    if (ind < in->utf8_len) log_message_error_utf8_test(log, CODE_METRIC, UTF8_TEST_OBITUARY_UTF8_INTERNAL);
    else
        if (strncmp((char *) in->utf8, (char *) byte, len) || in->val != val || len != in->utf8_len) log_message_error_utf8_test(log, CODE_METRIC, UTF8_TEST_OBITUARY_UTF8_DECODE);
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
        if (len != in->utf16_len) log_message_error_utf8_test(log, CODE_METRIC, UTF8_TEST_OBITUARY_UTF16_LEN);
        else
        {
            uint8_t ind = 0;
            for (ind = 0; ind < len; ind++)
                if (in->utf16[ind] != word[ind]) break;
            if (ind < len) log_message_error_utf8_test(log, CODE_METRIC, UTF8_TEST_OBITUARY_UTF16_ENCODE);
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
        uint8_t context = 0, len = 0, ind = 0;
        uint32_t val = 0;
        for (; ind < in->utf16_len; ind++)
            if (!utf16_decode(in->utf16[ind], &val, word, &len, &context)) break;
        if (ind < in->utf16_len) log_message_error_utf8_test(log, CODE_METRIC, UTF8_TEST_OBITUARY_UTF16_INTERNAL);
        else
        {
            if (len != in->utf16_len || in->val != val) log_message_error_utf8_test(log, CODE_METRIC, UTF8_TEST_OBITUARY_UTF16_DECODE);
            else
            {
                ind = 0;
                for (ind = 0; ind < len; ind++)
                    if (in->utf16[ind] != word[ind]) break;
                if (ind < len) log_message_error_utf8_test(log, CODE_METRIC, UTF8_TEST_OBITUARY_UTF16_DECODE);
                else return 1;
            }
        }
    }
    else return 1;
    return 0;   
}

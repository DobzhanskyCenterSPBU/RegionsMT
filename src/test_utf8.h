#pragma once

#include "memory.h"
#include "log.h"

struct utf8_test { 
    uint32_t val; 
    uint8_t *utf8; 
    uint8_t utf8_len; 
    uint16_t *utf16; 
    uint8_t utf16_len; 
};

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
    enum utf8_test_obituary obituary;
};

bool utf8_test(void *, size_t *, struct message *);
bool utf8_test_len(void *, struct message *);
bool utf8_test_encode(void *, struct message *);
bool utf8_test_decode(void *, struct message *);
bool utf16_test_encode(void *, struct message *);
bool utf16_test_decode(void *, struct message *);

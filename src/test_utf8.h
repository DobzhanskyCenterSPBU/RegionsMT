#pragma once

#include "log.h"
#include "utf8.h"

struct test_utf8 { 
    uint32_t val; 
    uint8_t utf8[UTF8_COUNT]; 
    uint8_t utf8_len; 
    uint16_t utf16[UTF16_COUNT]; 
    uint8_t utf16_len; 
};

bool test_utf8_generator(void *, size_t *, struct log *);
bool test_utf8_len(void *, struct log *);
bool test_utf8_encode(void *, struct log *);
bool test_utf8_decode(void *, struct log *);
bool test_utf16_encode(void *, struct log *);
bool test_utf16_decode(void *, struct log *);

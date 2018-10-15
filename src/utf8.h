#pragma once

#include "common.h"

#define UTF8_COUNT 6
#define UTF8_BOUND 0x110000

uint8_t utf8_len(uint32_t);

bool utf8_is_overlong(uint32_t, uint8_t);
bool utf8_is_valid(uint32_t, uint8_t);

// Before proceeding with these functions, 'utf8_is_valid' should be ensured to return 'true'.
// In many cases length is known explicitly, so it is provided a possibility to pass it directly.
bool utf8_is_whitespace(uint32_t);
bool utf8_is_whitespace_len(uint32_t, uint8_t);
bool utf8_is_xml_char(uint32_t);
bool utf8_is_xml_char_len(uint32_t, uint8_t);
bool utf8_is_xml_name_start_char_len(uint32_t, uint8_t);
bool utf8_is_xml_name_char_len(uint32_t, uint8_t);

void utf8_encode(uint32_t, uint8_t *restrict, uint8_t *restrict);
bool utf8_decode(uint8_t, uint32_t *restrict, uint8_t *restrict, uint8_t *restrict, uint8_t *restrict);
bool utf8_decode_once(uint8_t *restrict, size_t, uint32_t *restrict, uint8_t *restrict);

#define UTF16_COUNT 2

void utf16_encode(uint32_t, uint16_t *restrict, uint8_t *restrict);
bool utf16_decode(uint16_t, uint32_t *restrict, uint16_t *restrict, uint8_t *restrict, uint8_t *restrict);

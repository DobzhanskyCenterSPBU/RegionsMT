#pragma once

#include "Common.h"
#include "x86_64/Tools.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define UTF8_COUNT 6
#define UTF8_BOUND 0x110000

inline bool isoverlong(uint32_t utf8val, uint8_t utf8len)
{
    switch (utf8len)
    {
    case 0: 
    case 1:
        break;

    case 2:
        return uint32BitScanReverse(utf8val) <= 6;

    default:
        return uint32BitScanReverse(utf8val) <= 5 * ((uint32_t) utf8len - 1);
    }

    return 0;
}

inline bool isinvalid(uint32_t utf8val, uint8_t utf8len)
{
    return utf8val >= UTF8_BOUND || isoverlong(utf8val, utf8len);
}

inline bool iswhitespace(uint32_t utf8val, uint8_t utf8len)
{
    if (utf8len == 1) return utf8val == 0x20 || utf8val == 0xa || utf8val == 0x9 || utf8val == 0xd;
    return 0;
}

inline bool isxmlnamestartchar(uint32_t utf8val, uint8_t utf8len)
{
    switch (utf8len)
    {
    case 1:
        return utf8val == ':' || ('A' <= utf8val && utf8val <= 'Z') || utf8val == '_' || ('a' <= utf8val && utf8val <= 'z');

    case 2:
        return
            (0xc0 <= utf8val && utf8val <= 0xd6) ||
            (0xd8 <= utf8val && utf8val <= 0xf6) ||
            (0xf8 <= utf8val && utf8val <= 0x2ff) ||
            (0x370 <= utf8val && utf8val <= 0x37d) ||
            0x37f <= utf8val;

    case 3:
        return
            utf8val <= 0x1fff ||
            (0x200c <= utf8val && utf8val <= 0x200d) ||
            (0x2070 <= utf8val && utf8val <= 0x218f) ||
            (0x2c00 <= utf8val && utf8val <= 0x2fef) ||
            (0x3001 <= utf8val && utf8val <= 0xd7ff) ||
            (0xf900 <= utf8val && utf8val <= 0xfdcf) ||
            (0xfdf0 <= utf8val && utf8val <= 0xfffd);

    case 4:
        return utf8val <= 0xeffff;
    }

    return 0;
}

inline bool isxmlnamechar(uint32_t utf8val, uint8_t utf8len)
{
    if (isxmlnamestartchar(utf8val, utf8len)) return 1;

    switch (utf8len)
    {
    case 1:
        return utf8val == '-' || utf8val == '.' || ('0' <= utf8val && utf8val <= '9');

    case 2:
        return utf8val == 0xb7 || (0x300 <= utf8val && utf8val <= 0x36f);

    case 3:
        return (0x203f <= utf8val && utf8val <= 0x2040);
    }

    return 0;
}

inline void utf8encode(uint32_t utf8val, char *restrict utf8byte, uint8_t *restrict len)
{
    uint8_t mode = (uint8_t) uint32BitScanReverse(utf8val);

    if (mode <= 6) utf8byte[0] = (char) utf8val, *len = 1;
    else
    {
        *len = (mode + 4) / 5;
        for (uint8_t i = *len; i > 1; utf8byte[--i] = 128 | (utf8val & 63), utf8val >>= 6);
        utf8byte[0] = (char) ((((1u << (*len + 1)) - 1) << (8 - *len)) | utf8val);
    }
}

// The 'coroutine' decodes multi-byte sequence supplied by separate characters 'ch'. 
// It should be called until '*context' becomes zero. Returns '0' on error. 
// Warning: '*val', '*len', '*context' should be initialized with zeros before the initial call
// and should not be modified between sequential calls!
inline bool utf8decode(char ch, uint32_t *restrict val, char *restrict utf8byte, uint8_t *restrict len, uint8_t *restrict context)
{
    uint8_t mode = uint8BitScanReverse(~(uint8_t) ch);

    switch (mode)
    {
    case UINT8_MAX: // Invalid UTF-8 characters: '\xff'
    case 0: //         and '\xfe'
        break;

    case 6: // UTF-8 continuation byte
        if (*context)
        {
            *val <<= 6;
            *val |= ch & 63;
            utf8byte[*len - (*context)--] = ch;

            return 1;
        }
        
        break;

    case 7:
        if (!*context) // Single-byte character
        {
            *len = 1;
            *context = 0;
            *val = utf8byte[0] = ch;

            return 1;
        }
        
        break;

    default: // UTF-8 start byte (cases 1, 2, 3, 4, and 5)
        if (!*context)
        {
            *context = (*len = (7 - mode)) - 1;
            *val = ch & ((1u << mode) - 1);
            utf8byte[0] = ch;

            return 1;
        }       
    }

    return 0;
}

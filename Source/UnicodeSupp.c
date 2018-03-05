#include "UnicodeSupp.h"

extern inline bool isoverlong(uint32_t, uint8_t);
extern inline bool isinvalid(uint32_t, uint8_t);
extern inline bool iswhitespace(uint32_t, uint8_t);
extern inline bool isxmlnamestartchar(uint32_t, uint8_t);
extern inline bool isxmlnamechar(uint32_t, uint8_t);

extern inline void utf8encode(uint32_t, char *restrict, uint8_t *restrict );
extern inline bool utf8decode(char, uint32_t *restrict, char *restrict, uint8_t *restrict, uint8_t *restrict);

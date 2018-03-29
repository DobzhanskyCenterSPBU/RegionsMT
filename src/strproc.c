#include "np.h"
#include "ll.h"
#include "strproc.h"

#include <string.h>
#include <stdlib.h>

int char_cmp(const void *a, const void *b, void *context)
{
    (void) context;
    return *(const char *) a - *(const char *) b;
}

int str_strl_cmp_len(const void *Str, const void *Entry, void *p_Len)
{
    const struct strl *entry = Entry;
    size_t len = *(size_t *) p_Len;
    int res = strncmp((const char *) Str, entry->str, len);
    return res ? res : len < entry->len ? INT_MIN : 0;
}

int str_strl_cmp(const void *Str, const void *Entry, void *context)
{
    (void) context;
    const struct strl *entry = Entry;
    return strcmp((const char *) Str, entry->str);
}

bool p_str_handler(const char *str, size_t len, void *Ptr, void *context)
{
    (void) context;
    (void) len;
    const char **ptr = Ptr;
    *ptr = str;
    return 1;
}

bool str_handler(const char *str, size_t len, void *Ptr, void *context)
{
    (void) context;
    char **ptr = Ptr;
    char *tmp = malloc(len + 1);
    if (!tmp) return 0;
    memcpy(tmp, str, len + 1);
    *ptr = tmp;
    return 1;
}

bool bool_handler(const char *str, size_t len, void *Ptr, void *Context)
{
    (void) len;
    struct handler_context *context = Context;
    char *test;
    uint32_t res = (uint32_t) strtoul(str, &test, 10);
    if (*test)
    {
        if (!Stricmp(str, "false")) res = 0;
        else if (!Stricmp(str, "true")) res = 1;
        else res = UINT32_MAX;
    }

    if (res > 1) return 0;
    if (context && res) bit_set((uint8_t *) Ptr + context->offset, context->bit_pos);
    return 1;
}

uint64_t str_to_uint64(const char *str, char **ptr)
{
    return (uint64_t) strtoull(str, ptr, 10);
}

uint32_t str_to_uint32(const char *str, char **ptr)
{
    unsigned long res = strtoul(str, ptr, 10);
    return (uint32_t) MIN(res, UINT32_MAX); // In the case of 'unsigned long' is 64 bit integer
}

uint16_t str_to_uint16(const char *str, char **ptr)
{
    unsigned long res = strtoul(str, ptr, 10);
    return (uint16_t) MIN(res, UINT16_MAX);
}

#define DECLARE_INTEGER_HANDLER(TYPE, PREFIX, CONV) \
    bool PREFIX ## _handler(const char *str, size_t len, void *Ptr, void *Context) \
    { \
        (void) len; \
        TYPE *ptr = Ptr; \
        struct handler_context *context = Context; \
        char *test; \
        *ptr = CONV(str, &test); \
        if (*test) return 0; \
        if (context) bit_set((uint8_t *) Ptr + context->offset, context->bit_pos); \
        return 1; \
    }

DECLARE_INTEGER_HANDLER(uint64_t, uint64, str_to_uint64)
DECLARE_INTEGER_HANDLER(uint32_t, uint32, str_to_uint32)
DECLARE_INTEGER_HANDLER(uint16_t, uint16, str_to_uint16)
DECLARE_INTEGER_HANDLER(double, flt64, strtod)

#if defined _M_X64 || defined __x86_64__
DECLARE_INTEGER_HANDLER(size_t, size, str_to_uint64)
#elif defined defined _M_IX86 || defined __i386__
DECLARE_INTEGER_HANDLER(size_t, size, str_to_uint32)
#endif

bool empty_handler(const char *str, size_t len, void *Ptr, void *Context)
{
    (void) str;
    (void) len;
    struct handler_context *context = Context;
    if (context) bit_set((uint8_t *) Ptr + context->offset, context->bit_pos);
    return 1;
}

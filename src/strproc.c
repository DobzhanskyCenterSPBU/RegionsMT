#include "np.h"
#include "ll.h"
#include "strproc.h"

#include <string.h>
#include <stdlib.h>

int char_cmp(const char *a, const char *b, void *context)
{
    (void) context;
    return *a - *b;
}

int str_strl_cmp_len(const char *str, const struct strl *entry, size_t *p_len)
{
    int res = strncmp(str, entry->str, *p_len);
    return res > 0 ? 1 : res < 0 ? -1 : *p_len < entry->len ? -1 : 0;
}

int str_strl_cmp(const char *str, const struct strl *entry, void *context)
{
    (void) context;
    int res = strcmp(str, entry->str);
    return res > 0 ? 1 : res < 0 ? -1 : 0;
}

bool p_str_handler(const char *str, size_t len, const char **ptr, void *context)
{
    (void) context;
    (void) len;
    *ptr = str;
    return 1;
}

bool str_handler(const char *str, size_t len, char **ptr, void *context)
{
    (void) context;
    *ptr = malloc(len + 1);
    if (!*ptr) return 0;
    memcpy(*ptr, str, len + 1);
    return 1;
}

bool bool_handler(const char *str, size_t len, void *ptr, struct handler_context *context)
{
    (void) len;

    char *test;
    uint32_t res = (uint32_t) strtoul(str, &test, 10);

    if (*test)
    {
        if (!Stricmp(str, "false")) res = 0;
        else if (!Stricmp(str, "true")) res = 1;
        else res = UINT32_MAX;
    }

    if (res > 1) return 0;
    if (context && res) bit_set((uint8_t *) ptr + context->offset, context->bit_pos);
    return 1;
}

#define DECLARE_INTEGER_HANDLER(TYPE, PREFIX) \
    bool PREFIX ## _handler(const char *str, size_t len, TYPE *ptr, struct handler_context *context) \
    { \
        (void) len; \
        char *test; \
        *ptr = (TYPE) strtoull(str, &test, 10); \
        if (*test) return 0; \
        if (context) bit_set((uint8_t *) ptr + context->offset, context->bit_pos); \
        return 1; \
    }

DECLARE_INTEGER_HANDLER(uint64_t, uint64)
DECLARE_INTEGER_HANDLER(uint32_t, uint32)
DECLARE_INTEGER_HANDLER(uint16_t, uint16)
DECLARE_INTEGER_HANDLER(size_t, size)

bool flt64_handler(const char *str, size_t len, double *ptr, struct handler_context *context)
{
    (void) len;
    char *test;
    *ptr = strtod(str, &test);
    if (*test) return 0;
    if (context) bit_set((uint8_t *) ptr + context->offset, context->bit_pos);
    return 1;
}

bool empty_handler(const char *str, size_t len, void *ptr, struct handler_context *context)
{
    (void) str;
    (void) len;
    if (context) bit_set((uint8_t *) ptr + context->offset, context->bit_pos);
    return 1;
}

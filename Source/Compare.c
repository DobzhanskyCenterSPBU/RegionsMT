#include "Common.h"
#include "Compare.h"
#include "x86_64/Tools.h"

#include <string.h>
#include <stdlib.h>

int8_t charComp(const char *a, const char *b, void *context)
{
    (void) context;
    return (int8_t) *a - (int8_t) *b;
}

int8_t strCompDict(const char *probe, const strl *entry, void *context)
{
    (void) context;
    int res = strncmp(probe, entry->str, entry->len);
    return res > 0 ? 1 : res < 0 ? -1 : 0;
}

int8_t strCompDictLen(const char *probe, const strl *entry, size_t *len)
{
    bool gt = *len > entry->len, lt = *len < entry->len;
    
    int res = strncmp(probe, entry->str, lt ? *len : entry->len);
    return res > 0 ? 1 : res < 0 ? -1 : gt ? 1 : lt ? -1 : 0;
}
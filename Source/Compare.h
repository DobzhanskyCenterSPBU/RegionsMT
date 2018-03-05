#pragma once

#include "x86_64/Compare.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { 
    char *str; 
    size_t len; 
} strl;

int8_t charComp(const char *, const char *, void *);
int8_t strCompDict(const char *, const strl *, void *);
int8_t strCompDictLen(const char *, const strl *, size_t *);

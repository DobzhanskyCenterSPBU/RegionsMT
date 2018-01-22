#pragma once

///////////////////////////////////////////////////////////////////////////////
//
//  Functions for a string processing
//

#include "common.h"

struct strl {
    char *str;
    size_t len;
};

// Functions to be used as 'stable_cmp_callback' (see sort.h)
int char_cmp(const char *, const char *, void *);
int str_strl_cmp_len(const char *, const struct strl *, size_t *);
int str_strl_cmp(const char *, const struct strl *, void *);

typedef bool (*read_callback)(const char *, size_t, void *, void *); // Functional type for read callbacks
typedef bool (*write_callback)(char **, size_t *, size_t, void *, void *); // Functional type for write callbacks
typedef union { read_callback read; write_callback write; } rw_callback;

struct handler_context {
    ptrdiff_t offset;
    size_t bit_pos;
};

// Functions to be used as 'read_callback'
bool p_str_handler(const char *, size_t, const char **, void *);
bool str_handler(const char *, size_t, char **, void *);
bool bool_handler(const char *, size_t, void *, struct handler_context *);
bool uint64_handler(const char *, size_t, uint64_t *, struct handler_context *);
bool uint32_handler(const char *, size_t, uint32_t *, struct handler_context *);
bool uint16_handler(const char *, size_t, uint16_t *, struct handler_context *);
bool size_handler(const char *, size_t, size_t *, struct handler_context *);
bool flt64_handler(const char *, size_t, double *, struct handler_context *);
bool empty_handler(const char *, size_t, void *, struct handler_context *);

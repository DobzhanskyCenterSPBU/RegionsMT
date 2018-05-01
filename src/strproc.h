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

// Functions to be used as 'stable_cmp_callback' and 'cmp_callback' (see sort.h)
int char_cmp(const void *, const void *, void *);
int str_strl_stable_cmp_len(const void *, const void *, void *);
int str_strl_stable_cmp(const void *, const void *, void *);
int str_off_stable_cmp(const void *, const void *, void *);
bool str_off_cmp(const void *, const void *, void *);

typedef bool (*read_callback)(const char *, size_t, void *, void *); // Functional type for read callbacks
typedef bool (*write_callback)(char **, size_t *, size_t, void *, void *); // Functional type for write callbacks
typedef union { read_callback read; write_callback write; } rw_callback;

struct handler_context {
    ptrdiff_t offset;
    size_t bit_pos;
};

// Functions to be used as 'read_callback'
// If the second argument is 'SIZE_MAX', it is considered undefined
bool empty_handler(const char *, size_t, void *, void *);
bool p_str_handler(const char *, size_t, void *, void *);
bool str_handler(const char *, size_t, void *, void *);
bool bool_handler(const char *, size_t, void *, void *);
bool uint64_handler(const char *, size_t, void *, void *);
bool uint32_handler(const char *, size_t, void *, void *);
bool uint16_handler(const char *, size_t, void *, void *);
bool uint8_handler(const char *, size_t, void *, void *);
bool size_handler(const char *, size_t, void *, void *);
bool flt64_handler(const char *, size_t, void *, void *);

struct str_tbl_handler_context {
    char *str;
    size_t str_cap, str_cnt;
};

// Here the second argument should be a definite number 
bool str_tbl_handler(const char *, size_t, void *, void *);

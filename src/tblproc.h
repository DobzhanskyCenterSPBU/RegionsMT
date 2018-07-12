#pragma once

#include "log.h"
#include "strproc.h"

#include <stdio.h>

struct tbl_col {
    rw_callback handler;
    void *ptr, *context;
};

typedef bool (*tbl_selector_callback)(struct tbl_col *, size_t, size_t, void *, void *);
typedef bool (*tbl_eol_callback)(size_t, void *, void *);

size_t row_count(FILE *, int64_t, size_t);
uint64_t row_align(FILE *, int64_t);

bool tbl_read(const char *, int64_t, tbl_selector_callback, tbl_eol_callback, void *, void *, size_t *, size_t *, size_t *, char, struct log *);

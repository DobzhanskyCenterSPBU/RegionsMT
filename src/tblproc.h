#pragma once

#include "strproc.h"

#include <stdio.h>

typedef bool (*tbl_row_finalizer)(size_t, void *, void *);
typedef size_t (*tbl_selector)(char *, size_t, size_t *, void *);

struct tbl_col_sch {
    rw_callback handler;
    ptrdiff_t offset;
    void *context;
};

struct tbl_sch {
    size_t sz;
    tbl_row_finalizer finalizer;
    struct {
        struct tbl_col_sch *col_sch;
        size_t col_sch_cnt;
    };
};

size_t row_count(FILE *, int64_t, size_t);
uint64_t row_align(FILE *, int64_t);

bool row_read(FILE *, struct tbl_sch *, void *, size_t *, size_t *, size_t *, char, struct log *, size_t, size_t);

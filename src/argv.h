#pragma once

///////////////////////////////////////////////////////////////////////////////
//
//  Command-line parser which is nearly equal to 'getopt_long' GNU function
//

#include "common.h"
#include "log.h"
#include "strproc.h"

struct par {
    void *ptr, *context; // Third argument of the handler
    read_callback handler;
    bool option;
};

typedef bool (*par_selector_callback)(struct par *, char *, size_t, void *, void *, bool);
bool argv_parse(par_selector_callback, void *, void *, char **, size_t, char ***, size_t *, struct log *);

struct tag {
    struct strl name;
    size_t id;
};

struct par_sch {
    ptrdiff_t off;
    void *context; // Third argument of the handler
    read_callback handler;
    bool option;
};

struct argv_par_sch {
    struct {
        struct tag *ltag; // Long tag name handling; array should be sorted by 'name' according to the 'strncmp'
        size_t ltag_cnt;
    };
    struct {
        struct tag *stag; // Short tag name handling; 'name' is UTF-8 byte sequence; array should be sorted by 'ch'
        size_t stag_cnt;
    };
    struct {
        struct par_sch *par;
        size_t par_cnt;
    };
};

bool argv_par_selector(struct par *, char *, size_t len, void *, void *, bool);

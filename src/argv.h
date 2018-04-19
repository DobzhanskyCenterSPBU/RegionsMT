#pragma once

///////////////////////////////////////////////////////////////////////////////
//
//  Command-line parser which is nearly equal to 'getopt_long' GNU function
//

#include "common.h"
#include "log.h"
#include "strproc.h"

struct par {
    ptrdiff_t offset; // Offset of the field
    void *context; // Third argument of the handler
    read_callback handler;
    bool option;
};

typedef bool (*par_selector_callback)(struct par *, char *, size_t, void *);
bool argv_parse(par_selector_callback, par_selector_callback, void *, void *, char **, size_t, char ***, size_t *, struct log *);

struct tag {
    struct strl name;
    size_t id;
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
        struct par *par;
        size_t par_cnt;
    };
};

bool argv_par_selector_long(struct par *, char *, size_t len, void *);
bool argv_par_selector_shrt(struct par *, char *, size_t len, void *);

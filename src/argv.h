#pragma once

///////////////////////////////////////////////////////////////////////////////
//
//  Command-line parser which is nearly equal to 'getopt_long' GNU function
//

#include "common.h"
#include "log.h"
#include "strproc.h"

enum par_mode {
    PAR_MODE_ANY = 0,
    PAR_MODE_VALUE_ONLY = 1,
    PAR_MODE_OPTION_ONLY = 2,
};

struct tag {
    struct strl name;
    size_t id;
};

struct argv_sch {
    struct {
        struct tag *ltag; // Long tag name handling; array should be sorted by 'name' according to the 'strncmp'
        size_t ltag_cnt;
    };
    struct {
        struct tag *stag; // Short tag name handling; 'name' is UTF-8 byte sequence; array should be sorted by 'ch'
        size_t stag_cnt;
    };
    struct {
        struct par {
            ptrdiff_t offset; // Offset of the field
            void *context; // Third argument of the handler
            read_callback handler;
            enum par_mode mode;
        } *par;
        size_t par_cnt;
    };
};

bool argv_parse(struct argv_sch *, void *, char **, size_t, char ***, size_t *, struct log *);

#pragma once

#include "np.h"
#include "common.h"

#include <stdarg.h>

struct log {
    FILE *file;
    char *buff;
    size_t buff_cnt, buff_cap, buff_lim;
    uint64_t file_sz; // File size is 64-bit always!
};

enum message_type { 
    MESSAGE_TYPE_DEFAULT = 0,
    MESSAGE_TYPE_ERROR,
    MESSAGE_TYPE_WARNING,
    MESSAGE_TYPE_NOTE,
    MESSAGE_TYPE_INFO,
    MESSAGE_TYPE_RESERVED
};

typedef bool (*message_callback)(char *, size_t *, void *);
typedef bool (*message_callback_var)(char *, size_t *, void *, const char *, va_list);

struct code_metric {
    const char *path, *func;
    size_t line;
};

struct time_diff {
    uint64_t start, stop;
    const char *prefix;
};

bool message_time_diff(char *, size_t *, void *);
bool message_crt(char *, size_t *, void *);
bool message_var_generic(char *, size_t *, void *, const char *, va_list);
bool message_var_crt(char *, size_t *, void *, const char *, va_list);

#define DECLARE_PATH \
    static const char Path[] = __FILE__;    

#define CODE_METRIC \
    (struct code_metric) { \
        .path = Path, \
        .func = __func__, \
        .line = __LINE__, \
    }

bool log_init(struct log *restrict, char *restrict, size_t);
void log_close(struct log *restrict);
bool log_flush(struct log *restrict);
bool log_message(struct log *restrict, struct code_metric, enum message_type, message_callback, void *);
bool log_message_var(struct log *restrict, struct code_metric, enum message_type, message_callback_var, void *, const char *restrict, ...);

// Some specialized messages
bool log_message_generic(struct log *restrict, struct code_metric, enum message_type, const char *restrict, ...);
bool log_message_time_diff(struct log *restrict, struct code_metric, enum message_type, uint64_t, uint64_t, const char *restrict);
bool log_message_crt(struct log *restrict, struct code_metric, enum message_type, Errno_t);
bool log_message_fopen(struct log *restrict, struct code_metric, enum message_type, const char *restrict, Errno_t);
bool log_message_fseek(struct log *restrict, struct code_metric, enum message_type, int64_t, const char *restrict);

#pragma once

#include "np.h"
#include "common.h"

#include <stdarg.h>

#define ANSI_COLOR_BLACK "\x1b[30m"
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_BRIGHT_BLACK "\x1b[90m"
#define ANSI_COLOR_BRIGHT_RED "\x1b[91m"
#define ANSI_COLOR_BRIGHT_GREEN "\x1b[92m"
#define ANSI_COLOR_BRIGHT_YELLOW "\x1b[93m"
#define ANSI_COLOR_BRIGHT_BLUE "\x1b[94m"
#define ANSI_COLOR_BRIGHT_MAGENTA "\x1b[96m"
#define ANSI_COLOR_BRIGHT_CYAN "\x1b[96m"
#define ANSI_COLOR_BRIGHT_WHITE "\x1b[97m"
#define ANSI_COLOR_RESET "\x1b[0m"

#define COL(S) S ANSI_COLOR_RESET
#define COL_TTL0(S) COL(ANSI_COLOR_CYAN S)
#define COL_TTL1(S) COL(ANSI_COLOR_RED S)
#define COL_TTL2(S) COL(ANSI_COLOR_YELLOW S)
#define COL_TTL3(S) COL(ANSI_COLOR_BLUE S)
#define COL_TTL4(S) COL(ANSI_COLOR_GREEN S)
#define COL_TTL5(S) COL(ANSI_COLOR_BLACK S)
#define COL_TIME(S) COL(ANSI_COLOR_BRIGHT_BLACK S)
#define COL_SRC(S) COL(ANSI_COLOR_BRIGHT_BLACK S)
#define COL_FILE(S) COL(ANSI_COLOR_BRIGHT_YELLOW S)
#define COL_NUM(S) COL(ANSI_COLOR_BRIGHT_WHITE S)

struct log {
    FILE *file;
    char *buff;
    size_t cnt, cap, lim;
    uint64_t tot; // File size is 64-bit always!
};

enum message_type { 
    MESSAGE_DEFAULT = 0,
    MESSAGE_ERROR,
    MESSAGE_WARNING,
    MESSAGE_NOTE,
    MESSAGE_INFO,
    MESSAGE_RESERVED
};

typedef bool (*message_callback)(char *, size_t *, void *);
typedef bool (*message_callback_var)(char *, size_t *, void *, va_list);

struct code_metric {
    const char *path, *func;
    size_t line;
};

struct time_diff {
    uint64_t start, stop;
};

bool message_crt(char *, size_t *, void *);
bool message_var(char *, size_t *, void *, va_list);

struct message_thunk {
    message_callback handler;
    void *context;
};

bool message_var_two_stage(char *, size_t *, void *Thunk, va_list);

#define DECLARE_PATH \
    static const char Path[] = __FILE__;    

#define CODE_METRIC \
    (struct code_metric) { \
        .path = Path, \
        .func = __func__, \
        .line = __LINE__, \
    }

#define INTP(X) ((int) MIN((X), INT_MAX)) // Useful for the printf-like functions

bool log_init(struct log *restrict, char *restrict, size_t, bool, struct log *restrict);
void log_close(struct log *restrict);
bool log_multiple_init(struct log *restrict, size_t, char *restrict, size_t, struct log *restrict);
void log_multiple_close(struct log *restrict, size_t);
bool log_flush(struct log *restrict);
bool log_message(struct log *restrict, struct code_metric, enum message_type, message_callback, void *);
bool log_message_var(struct log *restrict, struct code_metric, enum message_type, message_callback_var, void *, ...);

// Some specialized messages
bool log_message_generic(struct log *restrict, struct code_metric, enum message_type, ...);
bool log_message_time_diff(struct log *restrict, struct code_metric, enum message_type, uint64_t, uint64_t, ...);
bool log_message_crt(struct log *restrict, struct code_metric, enum message_type, Errno_t);
bool log_message_fopen(struct log *restrict, struct code_metric, enum message_type, const char *restrict, Errno_t);
bool log_message_fseek(struct log *restrict, struct code_metric, enum message_type, int64_t, const char *restrict);

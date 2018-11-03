#pragma once

#include "np.h"
#include "common.h"

#include <stdarg.h>

#define ANSI "\x1b"
#define CSI "["
#define SGR(C) C "m"

#define RESET 0
#define FG_BLACK 30
#define FG_RED 31
#define FG_GREEN 32
#define FG_YELLOW 33
#define FG_BLUE 34
#define FG_MAGENTA 35
#define FG_CYAN 36
#define FG_WHITE 37
#define FG_BR_BLACK 90
#define FG_BR_RED 91
#define FG_BR_GREEN 92
#define FG_BR_YELLOW 93
#define FG_BR_BLUE 94
#define FG_BR_MAGENTA 95
#define FG_BR_CYAN 96
#define FG_BR_WHITE 97

#define TXT_RESET ANSI CSI SGR(TOSTRING(RESET))
#define TXT_FG(COL, TXT) ANSI CSI SGR(TOSTRING(FG_ ## COL)) TXT TXT_RESET
#define STY_TTL0(S) TXT_FG(GREEN, S)
#define STY_TTL1(S) TXT_FG(RED, S)
#define STY_TTL2(S) TXT_FG(YELLOW, S)
#define STY_TTL3(S) TXT_FG(BLUE, S)
#define STY_TTL4(S) TXT_FG(CYAN, S)
#define STY_TTL5(S) TXT_FG(BLACK, S)
#define STY_TTL_TS(S) TXT_FG(BR_BLACK, S)
#define STY_TTL_SRC(S) TXT_FG(BR_BLACK, S)
#define STY_PATH(S) TXT_FG(BR_CYAN, S)
#define STY_NUM(S) TXT_FG(BR_WHITE, S)
#define STY_STR(S) TXT_FG(BR_WHITE, S)
#define STY_CHR(S) TXT_FG(BR_WHITE, S)

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

#define CODE_METRIC \
    (struct code_metric) { .path = (__FILE__), .func = (__func__), .line = (__LINE__) }

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

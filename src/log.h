#pragma once

#include "np.h"
#include "common.h"
#include "utf8.h"

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

#define INIT_ENV_COL(COL) \
    { ANSI CSI SGR(TOSTRING(COL)), ANSI CSI SGR(TOSTRING(RESET)) }

struct env {
    char *begin, *end;
};

#define ENV_GUARD(S, D) ((S) ? (S) : D)
#define ENV_GENERIC_BEGIN(E, DB) (ENV_GUARD((E).begin, (DB)))
#define ENV_GENERIC_END(E, DE) (ENV_GUARD((E).end, (DE)))
#define ENV_GENERIC(E, DB, DE, ...) ENV_GENERIC_BEGIN(E, DB), __VA_ARGS__, ENV_GENERIC_END(E, DE)
#define ENV_BEGIN(E) ENV_GENERIC_BEGIN(E, "")
#define ENV_END(E) ENV_GENERIC_END(E, "")
#define ENV(E, ...) ENV_GENERIC(E, "", "", __VA_ARGS__)
#define QUO(SQUO, DQUO, S, ...) ENV_GENERIC(((S) ? (SQUO) : (DQUO)), __VA_ARGS__, ((S) ? UTF8_LSQUO : UTF8_LDQUO), ((S) ? UTF8_RSQUO : UTF8_RDQUO))
#define SQUO(SQUO, ...) ENV_GENERIC(SQUO, UTF8_LSQUO, UTF8_RSQUO, __VA_ARGS__)
#define DQUO(DQUO, ...) ENV_GENERIC(DQUO, UTF8_LDQUO, UTF8_RDQUO, __VA_ARGS__)

enum message_type {
    MESSAGE_DEFAULT = 0,
    MESSAGE_ERROR,
    MESSAGE_WARNING,
    MESSAGE_NOTE,
    MESSAGE_INFO,
    MESSAGE_RESERVED,
    MESSAGE_CNT
};

struct style {
    struct env ts, ttl[MESSAGE_CNT], src, dquo, squo, num, path, str;
};

struct log {
    FILE *file;
    char *buff;
    struct style style;
    size_t cnt, cap, lim;
    uint64_t tot; // File size is 64-bit always!
};

typedef bool (*message_callback)(char *, size_t *, void *, struct style);
typedef bool (*message_callback_var)(char *, size_t *, void *, struct style, va_list);

struct code_metric {
    const char *path, *func;
    size_t line;
};

struct time_diff {
    uint64_t start, stop;
};

bool message_crt(char *, size_t *, void *, struct style);
bool message_var(char *, size_t *, void *, struct style, va_list);

struct message_thunk {
    message_callback handler;
    void *context;
};

bool message_var_two_stage(char *, size_t *, void *Thunk, struct style, va_list);

#define CODE_METRIC \
    (struct code_metric) { .path = (__FILE__), .func = (__func__), .line = (__LINE__) }

#define INTP(X) ((int) MIN((X), INT_MAX)) // Useful for the printf-like functions

bool log_init(struct log *restrict, char *restrict, size_t, bool, struct style, struct log *restrict);
void log_close(struct log *restrict);
bool log_multiple_init(struct log *restrict, size_t, char *restrict, size_t, struct style, struct log *restrict);
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

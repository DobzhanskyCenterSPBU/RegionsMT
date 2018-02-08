#pragma once

#include "np.h"
#include "common.h"

#include <stdarg.h>

struct log {
    FILE *file;
    char *buff;
    size_t buff_cap;
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

typedef size_t (*message_callback)(char *, size_t, void *);
typedef size_t (*message_callback_var)(char *, size_t, void *, char *, va_list);

struct message {
    const char *path, *func;
    size_t line;
    union {
        message_callback handler;
        message_callback_var handler_var;
    };
    enum message_type type;
};

struct message_info_time_diff {
    struct message base;
    uint64_t start, stop;
    char *descr;
};

size_t message_info_time_diff(char *, size_t, void *);

struct message_error_crt {
    struct message base;
    Errno_t code;
};

size_t message_error_crt(char *, size_t, void *);
size_t message_var_generic(char *, size_t, void *, char *, va_list);

#define DECLARE_PATH \
    static const char Path[] = __FILE__;    

#define MESSAGE(Handler, Type) \
    (struct message) { \
        .path = Path, \
        .func = __func__, \
        .line = __LINE__, \
        .handler = (Handler), \
        .type = (Type) \
    }

#define MESSAGE_VAR(Handler_var, Type) \
    (struct message) { \
        .path = Path, \
        .func = __func__, \
        .line = __LINE__, \
        .handler_var = (Handler_var), \
        .type = (Type) \
    }

#define MESSAGE_INFO_TIME_DIFF(Start, Stop, Descr) \
    ((struct message_info_time_diff) { \
        .base = MESSAGE(message_info_time_diff, MESSAGE_TYPE_INFO), \
        .start = (Start), \
        .stop = (Stop), \
        .descr = (Descr) \
    })

#define MESSAGE_ERROR_CRT(Code) \
    ((struct message_error_crt) { \
        .base = MESSAGE(message_error_crt, MESSAGE_TYPE_ERROR), \
        .code = (Code) \
    })

#define MESSAGE_VAR_GENERIC(Type) \
    (MESSAGE_VAR(message_var_generic, (Type)))

bool log_init(struct log *restrict, char *restrict, size_t);
void log_close(struct log *restrict);
bool log_message(struct log *restrict, struct message *restrict);
bool log_message_var(struct log *restrict, struct message *restrict, char *, ...);

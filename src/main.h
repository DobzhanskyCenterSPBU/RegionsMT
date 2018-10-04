#pragma once

#include "common.h"
#include "ll.h"
#include "log.h"

enum {
    MAIN_ARGS_BIT_POS_THREAD_CNT = 0,
    MAIN_ARGS_BIT_POS_HELP,
    MAIN_ARGS_BIT_POS_TEST,
    MAIN_ARGS_BIT_POS_CAT,
    MAIN_ARGS_BIT_POS_LDE,
    MAIN_ARGS_BIT_CNT
};

struct main_args {
    char *log_path;
    size_t thread_cnt;
    uint8_t bits[UINT8_CNT(MAIN_ARGS_BIT_CNT)];
};

struct main_args main_args_default(void);
struct main_args main_args_override(struct main_args, struct main_args);

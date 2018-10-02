#pragma once

#include "main.h"
#include "log.h"

struct module_root_in {
    struct main_args *main_args;
    struct log *main_log;
};

struct module_root_context {
    struct main_args base;
};

struct module_root_out {
    struct log *thread_log;
    struct thread_pool *thread_pool;
    //tasksInfo tasksInfo;
    //outInfo outInfo;
    //pnumInfo pnumInfo;
    uint64_t initime;
};

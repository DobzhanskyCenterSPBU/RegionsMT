#pragma once

#include "main.h"
#include "log.h"

enum {
    FRAMEWORKOUT_BIT_POS_FOPEN = 0,
    FRAMEWORKOUT_BIT_CNT,
};

struct module_root_out {
    struct log log;
    struct thread_pool *thread_pool;
    //tasksInfo tasksInfo;
    //outInfo outInfo;
    //pnumInfo pnumInfo;
    uint64_t initime;
};

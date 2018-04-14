#pragma once

#include "log.h"

typedef bool(*test_generator_callback)(void *, size_t *, struct log *);
typedef void(*test_disposer_callback)(void *);
typedef bool(*test_callback)(void *, struct log *);

struct test_group {
    test_disposer_callback test_disposer;
    size_t test_sz;
    struct {
        test_generator_callback *test_generator;
        size_t test_generator_cnt;
    };
    struct {
        test_callback *test;
        size_t test_cnt;
    };
};

bool test(struct log *);
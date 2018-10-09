#pragma once

#include "log.h"

struct test_np_a {
    char *str;
    size_t cnt;
};

#define TEST_NP_EXP 24

bool test_np_generator_a(void *, size_t *, struct log *);
void test_np_disposer_a(void *);
bool test_np_a(void *, struct log *);

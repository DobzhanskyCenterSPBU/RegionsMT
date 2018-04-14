#pragma once

#include "log.h"

struct test_sort_a {
    void *arr;
    size_t cnt, sz;
};

bool test_sort_generator_a_1(void *, size_t *, struct log *);
bool test_sort_generator_a_2(void *, size_t *, struct log *);
void test_sort_disposer(void *);
bool test_sort_a(void *, struct log *);
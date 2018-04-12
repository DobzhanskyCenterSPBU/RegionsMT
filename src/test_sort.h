#pragma once

#include "log.h"

struct test_sort {
    void *arr;
    size_t cnt, sz;
};

bool test_sort_generator_a(void *, size_t *, struct log *);
bool test_sort_generator_b(void *, size_t *, struct log *);
void test_sort_disposer(void *);
bool test_sort_a(void *, struct log *);
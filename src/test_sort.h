#pragma once

#include "log.h"

struct test_sort_a {
    void *arr;
    size_t cnt, sz;
};

struct test_sort_b {
    void *arr;
    size_t cnt, sz, ucnt;
};

bool test_sort_generator_a_1(void *, size_t *, struct log *);
bool test_sort_generator_a_2(void *, size_t *, struct log *);
void test_sort_disposer_a(void *);
bool test_sort_a(void *, struct log *);

bool test_sort_generator_b_1(void *, size_t *, struct log *);
void test_sort_disposer_b(void *);
bool test_sort_b_1(void *, struct log *);
bool test_sort_b_2(void *, struct log *);
#pragma once

#include "log.h"

struct test_sort_a {
    void *arr;
    size_t cnt, sz;
};

struct test_sort_b {
    double *arr;
    size_t cnt, ucnt;
};

struct test_sort_c {
    size_t *arr;
    size_t cnt;
};

// Maximal array size is '1 << TEST_SORT_EXP'
#define TEST_SORT_EXP 24

bool test_sort_generator_a_1(void *, size_t *, struct log *);
bool test_sort_generator_a_2(void *, size_t *, struct log *);
void test_sort_disposer_a(void *);
bool test_sort_a(void *, struct log *);

bool test_sort_generator_b_1(void *, size_t *, struct log *);
void test_sort_disposer_b(void *);
bool test_sort_b_1(void *, struct log *);
bool test_sort_b_2(void *, struct log *);

bool test_sort_generator_c_1(void *, size_t *, struct log *);
void test_sort_disposer_c(void *);
bool test_sort_c_1(void *, struct log *);
bool test_sort_c_2(void *, struct log *);

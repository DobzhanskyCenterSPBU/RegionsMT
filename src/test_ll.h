#pragma once

#include "log.h"

struct test_ll {
    double a, b; 
    int res_dsc, res_dsc_abs, res_dsc_nan;
};

bool test_ll_generator_a(void *, size_t *, struct log *);
bool test_ll_a_1(void *, struct log *);
bool test_ll_a_2(void *, struct log *);
bool test_ll_a_3(void *, struct log *);
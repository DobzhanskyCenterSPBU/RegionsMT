#pragma once

#include "common.h"

#include <gsl/gsl_rng.h>

enum categorical_flags {
    TEST_TYPE_CODOMINANT = 1,
    TEST_TYPE_RECESSIVE = 2,
    TEST_TYPE_DOMINANT = 4,
    TEST_TYPE_ALLELIC =8
};

double maver_adj(uint8_t *, size_t *, size_t, size_t, size_t, size_t *, size_t, double, gsl_rng *, enum categorical_test_type);

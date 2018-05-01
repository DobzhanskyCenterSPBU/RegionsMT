#pragma once

#include "common.h"

#include <gsl/gsl_rng.h>

enum categorical_test_type {
    TEST_TYPE_CODOMINANT = 0,
    TEST_TYPE_RECESSIVE,
    TEST_TYPE_DOMINANT,
    TEST_TYPE_ALLELIC
};

double maver_adj(uint8_t *, size_t *, size_t, size_t, size_t, size_t *, size_t, double, gsl_rng *, enum categorical_test_type);

#pragma once

#include "common.h"

#include <gsl/gsl_rng.h>

double maver_adj(uint8_t *, uint8_t *, size_t, size_t, size_t *, size_t, double, gsl_rng *);

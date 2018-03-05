#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined __x86_64 || defined _M_X64

// WARNING! 'float64CompDscStable' should not be used on arrays that may contain NaN's
int8_t float64CompDscAbsStable(const double *, const double *, void *);
int8_t float64CompDscStable(const double *, const double *, void *);
int8_t float64CompDscNaNStable(const double *, const double *, void *);
bool float64CompDsc(const double *, const double *, void *);
bool float64CompDscNaN(const double *, const double *, void *);

#endif
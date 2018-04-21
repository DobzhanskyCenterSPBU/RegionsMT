#pragma once

#include "common.h"

double log_fact(size_t);
double log_choose(size_t, size_t);
double pdf_hypergeom(size_t, size_t, size_t, size_t);
double gamma_inc_P(double, double);
double gamma_inc_Q(double, double);

double cdf_gamma_Q(double, double, double);
double cdf_chisq_Q(double, double);
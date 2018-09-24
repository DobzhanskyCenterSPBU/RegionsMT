#include "ll.h"
#include "gslsupp.h"

#include <math.h>
#include <gsl/gsl_sf.h>

double log_fact(size_t n)
{
    gsl_sf_result res;
    if (n < UINT_MAX) 
    {
        if (gsl_sf_lnfact_e((unsigned) n, &res) == GSL_SUCCESS) return res.val;
    }
    else
    {
        if (gsl_sf_lngamma_e((double) n + 1., &res) == GSL_SUCCESS) return res.val;
    }
    return nan(__func__);
}

double log_choose(size_t n, size_t m)
{
    if (m > n) return nan(__func__);
    if (m == n || !m) return 0.;
    return m > (n >> 1) ? // Addition here may be not associative
        log_fact(n) - log_fact(n - m) - log_fact(m) : 
        log_fact(n) - log_fact(m) - log_fact(n - m);
}

double pdf_hypergeom(size_t k, size_t n1, size_t n2, size_t t)
{
    size_t car, n12 = size_add(&car, n1, n2);
    if (car) return nan(__func__);
    if (t > n12) t = n12;
    if (k > n1 || k > t || (t > n2 && k < t - n2)) return 0.;
    return exp(log_choose(n1, k) + log_choose(n2, t - k) - log_choose(n12, t));
}

double gamma_inc_P(double a, double x)
{
    gsl_sf_result res;
    if (gsl_sf_gamma_inc_P_e(a, x, &res) == GSL_SUCCESS) return res.val;
    return nan(__func__);
}

double gamma_inc_Q(double a, double x)
{
    gsl_sf_result res;
    if (gsl_sf_gamma_inc_Q_e(a, x, &res) == GSL_SUCCESS) return res.val;
    return nan(__func__);
}

double cdf_gamma_Q(double x, double a, double b)
{
    if (x <= 0.) return 1.;
    double y = x / b;
    return y < a ? 1. - gamma_inc_P(a, y) : gamma_inc_Q(a, y);
}

double cdf_chisq_Q(double x, double df)
{
    return cdf_gamma_Q(x, df / 2., 2.);
}
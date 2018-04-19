#include "np.h"
#include "common.h"
#include "ll.h"
#include "categorical.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_sf.h>
#include <gsl/gsl_randist.h>

#define REMAP(remap, cd) \
    switch ((cd)) \
    { \
    case 3: /* = 1 | 2 */ \
        (remap)[0] = 0; \
        (remap)[1] = 1; \
        (cd) = 2; \
        break; \
        \
    case 5: /* = 1 | 4 */ \
        (remap)[0] = 0; \
        (remap)[2] = 1; \
        (cd) = 2; \
        break; \
        \
    case 6: /* = 2 | 4 */ \
        (remap)[1] = 0; \
        (remap)[2] = 1; \
        (cd) = 2; \
        break; \
        \
    case 7: /* = 1 | 2 | 4 */ \
        (remap)[0] = 0; \
        (remap)[1] = 1; \
        (remap)[2] = 2; \
        (cd) = 3; \
        break; \
        \
    default: \
        continue; \
    }

enum maver_adj_status {
    MOVING_AVERAGE_STATUS_ERROR = 0,
    MOVING_AVERAGE_STATUS_SUCCESS,
    MOVING_AVERAGE_STATUS_SATURATION
};

enum maver_adj_status maver_adj(uint8_t *gen, uint8_t *phen, size_t snp_cnt, size_t phen_cnt, size_t rpl_max, size_t k, double rel_err, double *res)
{
    //SEXP res, dim = getAttrib(genotype, R_DimSymbol);
    //const unsigned phen_cnt = INTEGER(dim)[0], snp_cnt = INTEGER(dim)[1];
    //const double relError = 1. + 1.e-7;

    //PROTECT(res = allocVector(REALSXP, 1));

    size_t table[9], sumy[3], remapy[3];
    double outer[9];

    size_t *permut = NULL;
    size_t *row_count = NULL, *row_tablesum = NULL, *row_filter = NULL, *row_sums = NULL;
    uint8_t *row_unique = NULL, *row_remap = NULL;
        
    // Memory management
    permut = malloc((size_t) phen_cnt * sizeof *permut);
    row_count = calloc((size_t) snp_cnt, sizeof *row_count); // Filled with zeros!
    row_tablesum = malloc((size_t) snp_cnt * sizeof *row_tablesum);
    row_filter = malloc((size_t) phen_cnt * (size_t) snp_cnt * sizeof *row_filter);
    row_sums = malloc(3 * (size_t) snp_cnt * sizeof *row_sums);
    row_unique = calloc((size_t) snp_cnt, sizeof *row_unique); // Filled with zeros!
    row_remap = malloc(3 * (size_t) snp_cnt * sizeof *row_remap);

    if (!(permut && row_count && row_tablesum && row_filter && row_sums && row_unique && row_remap)) goto error;
    // Memory allocation failed

    ///////////////////////////////////////////////////////////////////////////
    //
    //  Initialization
    //

    double density = 0.;
    size_t density_count = 0;

    for (unsigned j = 0; j < snp_cnt; j++)
    {
        const size_t phen_off = phen_cnt * j;

        // Filtering genotypes
        size_t sel_cnt = 0;
        for (size_t i = 0; i < phen_cnt; i++) if (gen[i + phen_off] < 3) row_filter[sel_cnt++ + phen_off] = i;

        if (!sel_cnt) continue; // There is nothing to do if the count is zero
        row_count[j] = sel_cnt;

        // Working with rows
        uint8_t cdx = 0;
        uint8_t *remapx = row_remap + 3 * j;
        for (size_t i = 0; i < sel_cnt && cdx != 7; cdx |= 1 << gen[row_filter[i++ + phen_off] + phen_off]);
        REMAP(remapx, cdx); // Remapping table row indeces
        row_unique[j] = cdx;

        // Working with columns
        uint8_t cdy = 0;
        for (size_t i = 0; i < sel_cnt && cdy != 7; cdy |= 1 << phen[row_filter[i++ + phen_off]]);
        REMAP(remapy, cdy); // Remapping table column indeces

        // Building contingency table
        memset(table, 0, cdx * cdy * sizeof(*table));

        for (size_t i = 0; i < sel_cnt; i++)
        {
            size_t ind = row_filter[i + phen_off];
            table[remapx[gen[ind + phen_off]] + cdx * remapy[phen[ind]]]++;
        }

        // Computing sums
        unsigned *const sumx = row_sums + 3 * j;

        memset(sumx, 0, cdx * sizeof(*sumx));
        memset(sumy, 0, cdy * sizeof(*sumy));
        row_tablesum[j] = 0;

        for (uint8_t t = 0; t < cdy; row_tablesum[j] += sumy[t], t++) for (uint8_t s = 0; s < cdx; s++)
        {
            unsigned el = table[s + cdx * t];
            sumx[s] += el;
            sumy[t] += el;
        }

        // Computing outer product
        bool exact = 0;

        for (uint8_t t = 0; t < cdy; t++) for (uint8_t s = 0; s < cdx; s++)
        {
            uint8_t ind = s + cdx * t;
            double tmp = outer[ind] = (double) sumx[s] * (double) sumy[t] / (double) row_tablesum[j];
            if (tmp >= 5. || cdx > 2 || cdy > 2) continue;
            exact = 1;
            break;
        }

        if (exact) // Exact Fisher test
        {
            const size_t lo = size_sub_sat(sumx[0], sumy[1]);
            const size_t hi = MIN(sumx[0], sumy[0]);

            const double hyp_comp = gsl_ran_hypergeometric_pdf(table[0], sumy[0], sumy[1], sumx[0]);
            double hyp_sum = 0., hyp_less = 0.;

            for (size_t ind = lo; ind <= hi; ind++)
            {
                double hyp = gsl_ran_hypergeometric_pdf(ind, sumy[0], sumy[1], sumx[0]);
                hyp_sum += hyp;
                if (hyp <= hyp_comp * rel_err) hyp_less += hyp;
            }

            density += log10(hyp_sum) - log10(hyp_less);
        }
        else // Chi-square test
        {
            double stat = 0.;
            for (uint8_t ind = 0; ind < cdx * cdy; ind++)
            {
                double diff = outer[ind] - (double) table[ind];
                stat += diff * diff / outer[ind];
            }

            density -= log10(gsl_cdf_chisq_Q(stat, (cdx - 1) * (cdy - 1)));
        }
        density_count++;
    }

    density /= density_count;

    ///////////////////////////////////////////////////////////////////////////
    //
    //  Simulations
    //

    size_t rpl = 0, qc = 0;

    for (; rpl < rpl_max && (!k || qc <= k); rpl++) // Entering 'fixed' mode if 'k' is 'NA'
    {
        // Generating random permutation
        for (unsigned i = 0; i < phen_cnt; permut[i] = i, i++); // Starting with identity permutation

        for (unsigned i = 0; i < phen_cnt; i++) // Performing 'Knuth shuffle'
        {
            unsigned k = i + (unsigned) floor(unif_rand() * (phen_cnt - i)), tmp = permut[k];
            permut[k] = permut[i];
            permut[i] = tmp;
        }

        // Density computation
        double density_ = 0.;
        size_t density_count_ = 0;

        for (unsigned j = 0; j < snp_cnt; j++) if (row_count[j] && row_unique[j])
        {
            const size_t phen_cntj = (size_t) phen_cnt * (size_t) j;
            const unsigned selcount = row_count[j];
            const uint8_t cdx = row_unique[j];
            uint8_t *const remapx = row_remap + 3 * j;

            // Working with columns
            uint8_t cdy = 0;
            for (unsigned i = 0; i < selcount && cdy != 7; cdy |= 1 << phen[permut[row_filter[i++ + phen_cntj]]]);
            REMAP(remapy, cdy); // Remapping table column indeces

            // Building contingency table
            memset(table, 0, cdx * cdy * sizeof table[0]);

            for (unsigned i = 0; i < selcount; i++)
            {
                unsigned ind = row_filter[i + phen_cntj];
                table[remapx[gen[ind + phen_cntj]] + cdx * remapy[phen[permut[ind]]]]++;
            }

            // Computing sums
            unsigned *const sumx = row_sums + 3 * j; // Row sums are already computed

            memset(sumy, 0, cdy * sizeof sumy[0]);
            for (uint8_t t = 0; t < cdy; t++) for (uint8_t s = 0; s < cdx; sumy[t] += table[s + cdx * t], s++);

            // Computing outer product
            double outer_min = DBL_MAX;

            for (uint8_t t = 0; t < cdy; t++) for (uint8_t s = 0; s < cdx; s++)
            {
                uint8_t ind = s + cdx * t;
                outer[ind] = (double) sumx[s] * (double) sumy[t] / (double) row_tablesum[j];
                if (outer[ind] < outer_min) outer_min = outer[ind];
            }

            // Computing p-values
            if (outer_min <= 4. && cdx == 2 && cdy == 2)
            {
                const unsigned lo = max(0, (signed) sumx[0] - (signed) sumy[1]);
                const unsigned hi = min(sumx[0], sumy[0]);

                const double hyp_comp = gsl_ran_hypergeometric_pdf(table[0], sumy[0], sumy[1], sumx[0]);
                double hyp_sum = 0., hyp_less = 0.;

                for (unsigned ind = lo; ind <= hi; ind++)
                {
                    double hyp = Rf_dhyper(ind, sumy[0], sumy[1], sumx[0], 0);
                    hyp_sum += hyp;
                    if (hyp <= hyp_comp * rel_err) hyp_less += hyp;
                }

                density_ += log(hyp_sum) - log(hyp_less);
            }
            else
            {
                double stat = 0.;

                for (uint8_t ind = 0; ind < cdx * cdy; ind++)
                {
                    double diff = outer[ind] - (double) table[ind];
                    stat += diff * diff / outer[ind];
                }

                density_ -= Rf_pchisq(stat, (cdx - 1) * (cdy - 1), 0, 1); // Upper tail and logarithm are enabled
            }

            density_count_++;
        }

        density_ /= density_count_;
        if (density_ > density) qc++;
    }

   

    *res = (double) qc / (double) rpl;
    if  (k && rpl == rpl_max) res *= -1;

    // Error handling
    for (;;)
    {
    error1:
        //Rf_warning("Function '%s' exited with an error: memory allocation failed!\n", __FUNCTION__);
        break;
    }

epilogue:
    // Memory should be cleared after all...
    free(permut);
    free(row_count);
    free(row_tablesum);
    free(row_filter);
    free(row_sums);

    free(row_unique);
    free(row_remap);

    return res;
}

p_test()
{
    gsl_sf_result res;
    gsl_sf_gammainv_e(1, &res);
    
    double res = gsl_ran_hypergeometric_pdf(1, 7, 19, 13);
    //printf("%.10f", res);
}

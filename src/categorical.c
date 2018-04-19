#include "common.h"
#include "categorical.h"

#include <float.h>
#include <stdlib.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_sf_gamma.h>

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

bool moving_average_adjusted(uint8_t *gen, uint8_t *phen, size_t snp_cnt, size_t phen_cnt, size_t rmax, size_t k)
{
    //SEXP res, dim = getAttrib(genotype, R_DimSymbol);
    //const unsigned phen_cnt = INTEGER(dim)[0], snp_cnt = INTEGER(dim)[1];
    const double relError = 1. + 1.e-7;

    //PROTECT(res = allocVector(REALSXP, 1));

    unsigned table[9], sumy[3], remapy[3];
    double outer[9];

    unsigned *permut = NULL;
    unsigned *row_count = NULL, *row_tablesum = NULL, *row_filter = NULL, *row_sums = NULL;
    unsigned char *row_unique = NULL, *row_remap = NULL;
        
    // Memory management
    permut = malloc((size_t) phen_cnt * sizeof *permut);
    row_count = calloc((size_t) snp_cnt, sizeof *row_count); // Filled with zeros!
    row_tablesum = malloc((size_t) snp_cnt * sizeof *row_tablesum);
    row_filter = malloc((size_t) phen_cnt * (size_t) snp_cnt * sizeof *row_filter);
    row_sums = malloc(3 * (size_t) snp_cnt * sizeof *row_sums);
    row_unique = calloc((size_t) snp_cnt, sizeof *row_unique); // Filled with zeros!
    row_remap = malloc(3 * (size_t) snp_cnt * sizeof *row_remap);

    if (!(permut && row_count && row_tablesum && row_filter && row_sums && row_unique && row_remap)) goto error1; 
    // Memory allocation failed

    ///////////////////////////////////////////////////////////////////////////
    //
    //  Initialization
    //

    double density = 0.;
    size_t density_count = 0;

    for (unsigned j = 0; j < snp_cnt; j++)
    {
        const size_t phen_cntj = (size_t) phen_cnt * (size_t) j;

        // Filtering genotypes
        unsigned selcount = 0;
        for (unsigned i = 0; i < phen_cnt; i++) if (INTEGER(genotype)[i + phen_cntj] != NA_INTEGER && INTEGER(genotype)[i + phen_cntj] != 3) row_filter[selcount++ + phen_cntj] = i;

        if (!selcount) continue; // There is nothing to do if the count is zero
        row_count[j] = selcount;

        // Working with rows
        unsigned char cdx = 0;
        unsigned char *const remapx = row_remap + 3 * j;
        for (unsigned i = 0; i < selcount && cdx != 7; cdx |= 1 << INTEGER(genotype)[row_filter[i++ + phen_cntj] + phen_cntj]);
        REMAP(remapx, cdx); // Remapping table row indeces
        row_unique[j] = cdx;

        // Working with columns
        unsigned char cdy = 0;
        for (unsigned i = 0; i < selcount && cdy != 7; cdy |= 1 << INTEGER(phenotype)[row_filter[i++ + phen_cntj]]);
        REMAP(remapy, cdy); // Remapping table column indeces

                            // Building contingency table
        memset(table, 0, cdx * cdy * sizeof table[0]);

        for (unsigned i = 0; i < selcount; i++)
        {
            unsigned ind = row_filter[i + phen_cntj];
            table[remapx[INTEGER(genotype)[ind + phen_cntj]] + cdx * remapy[INTEGER(phenotype)[ind]]]++;
        }

        // Computing sums
        unsigned *const sumx = row_sums + 3 * j;

        memset(sumx, 0, cdx * sizeof sumx[0]);
        memset(sumy, 0, cdy * sizeof sumy[0]);
        row_tablesum[j] = 0;

        for (unsigned char t = 0; t < cdy; row_tablesum[j] += sumy[t], t++) for (unsigned char s = 0; s < cdx; s++)
        {
            unsigned el = table[s + cdx * t];
            sumx[s] += el;
            sumy[t] += el;
        }

        // Computing outer product
        double outer_min = DBL_MAX;

        for (unsigned char t = 0; t < cdy; t++) for (unsigned char s = 0; s < cdx; s++)
        {
            unsigned char ind = s + cdx * t;
            outer[ind] = (double) sumx[s] * (double) sumy[t] / (double) row_tablesum[j];
            if (outer[ind] < outer_min) outer_min = outer[ind];
        }

        // Computing p-values
        if (outer_min <= 4. && cdx == 2 && cdy == 2)
        {
            const unsigned lo = max(0, (signed) sumx[0] - (signed) sumy[1]);
            const unsigned hi = min(sumx[0], sumy[0]);

            const double hyp_comp = Rf_dhyper(table[0], sumy[0], sumy[1], sumx[0], 0);
            double hyp_sum = 0., hyp_less = 0.;

            for (unsigned ind = lo; ind <= hi; ind++)
            {
                double hyp = Rf_dhyper(ind, sumy[0], sumy[1], sumx[0], 0);
                hyp_sum += hyp;
                if (hyp <= hyp_comp * relError) hyp_less += hyp;
            }

            density += log(hyp_sum) - log(hyp_less);
        }
        else
        {
            double stat = 0.;

            for (unsigned char ind = 0; ind < cdx * cdy; ind++)
            {
                double diff = outer[ind] - (double) table[ind];
                stat += diff * diff / outer[ind];
            }

            density -= Rf_pchisq(stat, (cdx - 1) * (cdy - 1), 0, 1); // Upper tail and logarithm are enabled
        }

        density_count++;
    }

    density /= density_count;

    ///////////////////////////////////////////////////////////////////////////
    //
    //  Simulations
    //

    const size_t rbnd = (size_t) REAL(rmax)[0], qcbnd = INTEGER(k)[0];
    const _Bool fixed = qcbnd == NA_INTEGER; // Entering 'fixed' mode if 'k' is 'NA'

    size_t rpl = 0, qc = 0;

    for (; rpl < rbnd && (fixed || qc <= qcbnd); rpl++)
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
            const unsigned char cdx = row_unique[j];
            unsigned char *const remapx = row_remap + 3 * j;

            // Working with columns
            unsigned char cdy = 0;
            for (unsigned i = 0; i < selcount && cdy != 7; cdy |= 1 << INTEGER(phenotype)[permut[row_filter[i++ + phen_cntj]]]);
            REMAP(remapy, cdy); // Remapping table column indeces

                                // Building contingency table
            memset(table, 0, cdx * cdy * sizeof table[0]);

            for (unsigned i = 0; i < selcount; i++)
            {
                unsigned ind = row_filter[i + phen_cntj];
                table[remapx[INTEGER(genotype)[ind + phen_cntj]] + cdx * remapy[INTEGER(phenotype)[permut[ind]]]]++;
            }

            // Computing sums
            unsigned *const sumx = row_sums + 3 * j; // Row sums are already computed

            memset(sumy, 0, cdy * sizeof sumy[0]);
            for (unsigned char t = 0; t < cdy; t++) for (unsigned char s = 0; s < cdx; sumy[t] += table[s + cdx * t], s++);

            // Computing outer product
            double outer_min = DBL_MAX;

            for (unsigned char t = 0; t < cdy; t++) for (unsigned char s = 0; s < cdx; s++)
            {
                unsigned char ind = s + cdx * t;
                outer[ind] = (double) sumx[s] * (double) sumy[t] / (double) row_tablesum[j];
                if (outer[ind] < outer_min) outer_min = outer[ind];
            }

            // Computing p-values
            if (outer_min <= 4. && cdx == 2 && cdy == 2)
            {
                const unsigned lo = max(0, (signed) sumx[0] - (signed) sumy[1]);
                const unsigned hi = min(sumx[0], sumy[0]);

                const double hyp_comp = Rf_dhyper(table[0], sumy[0], sumy[1], sumx[0], 0);
                double hyp_sum = 0., hyp_less = 0.;

                for (unsigned ind = lo; ind <= hi; ind++)
                {
                    double hyp = Rf_dhyper(ind, sumy[0], sumy[1], sumx[0], 0);
                    hyp_sum += hyp;
                    if (hyp <= hyp_comp * relError) hyp_less += hyp;
                }

                density_ += log(hyp_sum) - log(hyp_less);
            }
            else
            {
                double stat = 0.;

                for (unsigned char ind = 0; ind < cdx * cdy; ind++)
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

    REAL(res)[0] = (double) qc / (double) rpl;
    if (!fixed && rpl == rbnd) REAL(res)[0] *= -1;

epilogue:
    // Memory should be cleared after all...
    free(permut);
    free(row_count);
    free(row_tablesum);
    free(row_filter);
    free(row_sums);

    free(row_unique);
    free(row_remap);

    UNPROTECT(1);
    return res;

    // Error handling
    for (;;)
    {
    error0:
        Rf_warning("Function '%s' exited with an error: dimensions of genotype and phenotype are discordant!\n", __FUNCTION__);
        break;

    error1:
        Rf_warning("Function '%s' exited with an error: memory allocation failed!\n", __FUNCTION__);
        break;
    }

    REAL(res)[0] = NA_REAL;
    goto epilogue;
}

p_test()
{
    gsl_sf_result res;
    gsl_sf_gammainv_e(1, &res);
    
    double res = gsl_ran_hypergeometric_pdf(1, 7, 19, 13);
    //printf("%.10f", res);
}

#include "np.h"
#include "common.h"
#include "gslsupp.h"
#include "ll.h"
#include "memory.h"
#include "categorical.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static double stat_exact(size_t *table, size_t *mar_x, size_t *mar_y, double rel_err)
{
    size_t lo = size_sub_sat(mar_x[0], mar_y[1]), hi = MIN(mar_x[0], mar_y[0]);
    double hyp_comp = pdf_hypergeom(table[0], mar_y[0], mar_y[1], mar_x[0]), hyp_sum = 0., hyp_less = 0.;
    for (size_t i = lo; i <= hi; i++)
    {
        double hyp = pdf_hypergeom(i, mar_y[0], mar_y[1], mar_x[0]);
        hyp_sum += hyp;
        if (hyp <= hyp_comp * rel_err) hyp_less += hyp;
    }
    return log10(hyp_sum) - log10(hyp_less);
}

static double stat_chisq(size_t *table, double *outer, size_t mar_x_cnt, size_t mar_y_cnt)
{
    double stat = 0.;
    size_t pr = mar_x_cnt * mar_y_cnt;
    for (size_t i = 0; i < pr; i++)
    {
        double out = outer[i], diff = out - (double) table[i];
        stat += diff * diff / out;
    }
    return -log10(cdf_chisq_Q(stat, (double) (pr - mar_x_cnt - mar_y_cnt + 1)));
}

static bool outer_prod(size_t *table, double *outer, size_t *mar_x, size_t *mar_y, size_t mar_xy, size_t mar_x_cnt, size_t mar_y_cnt)
{
    if (mar_x_cnt > 2 || mar_y_cnt > 2)
    {
        for (size_t i = 0; i < mar_y_cnt; i++) for (size_t j = 0; j < mar_x_cnt; j++)
            outer[j + mar_x_cnt * i] = (double) mar_x[j] * (double) mar_y[i] / (double) mar_xy;
        return 1;
    }
    for (size_t i = 0; i < mar_y_cnt; i++) for (size_t j = 0; j < mar_x_cnt; j++)
    {
        double tmp = (double) mar_x[j] * (double) mar_y[i] / (double) mar_xy;
        if (tmp >= 5.) outer[j + mar_x_cnt * i] = tmp;
        else return 0;
    }
    return 1;
}

static uint8_t bits_check(uint8_t bits)
{
    if (bits == (1 | 2) || bits == (1 | 4) || bits == (2 | 4)) return 2;
    if (bits == (1 | 2 | 4)) return 3;
    return 0;
}

static void table_shuffle(size_t *table, uint8_t bits_x, uint8_t bits_y)
{
    switch (bits_x)
    {
    case (1 | 2):
        switch (bits_y)
        {
        case (1 | 2):
            table[2] = table[3], table[3] = table[4];
            break;
        case (1 | 4):
            table[2] = table[6], table[3] = table[7];
            break;
        case (2 | 4):
            table[0] = table[3], table[1] = table[4], table[2] = table[6], table[3] = table[7];
            break;
        case (1 | 2 | 4):
            table[2] = table[3], table[3] = table[4], table[4] = table[6], table[5] = table[7];
            break;
        }
        break;
    case (1 | 4):
        switch (bits_y)
        {
        case (1 | 2):
            table[1] = table[2], table[2] = table[3], table[3] = table[5];
            break;
        case (1 | 4):
            table[1] = table[2], table[2] = table[6], table[3] = table[8];
            break;
        case (2 | 4):
            table[0] = table[3], table[1] = table[5], table[2] = table[6], table[3] = table[8];
            break;
        case (1 | 2 | 4):
            table[1] = table[2], table[2] = table[3], table[3] = table[5], table[4] = table[6], table[5] = table[8];
            break;
        }
        break;
    case (2 | 4):
        switch (bits_y)
        {
        case (1 | 2):
            table[0] = table[1], table[1] = table[2], table[2] = table[4], table[3] = table[5];
            break;
        case (1 | 4):
            table[0] = table[1], table[1] = table[2], table[2] = table[7], table[3] = table[8];
            break;
        case (2 | 4):
            table[0] = table[4], table[1] = table[5], table[2] = table[7], table[3] = table[8];
            break;
        case (1 | 2 | 4):
            table[0] = table[1], table[1] = table[2], table[2] = table[4], table[3] = table[5], table[4] = table[7], table[5] = table[8];
            break;
        }
        break;
    case (1 | 2 | 4):
        switch (bits_y)
        {
        case (2 | 4):
            table[0] = table[3], table[1] = table[4], table[2] = table[5];
        case (1 | 4):
            table[3] = table[6], table[4] = table[7], table[5] = table[8];
            break;
        }
        break;
    }
}

// rel_err = 1. + 1.e-7;
double maver_adj(uint8_t *gen, uint8_t *phen, size_t snp_cnt, size_t phen_cnt, size_t *p_rpl, size_t k, double rel_err, gsl_rng *rng)
{
    double res = nan(__func__);
    uint8_t *phen_perm = malloc(phen_cnt * sizeof(*phen_perm));
    size_t *filter = NULL;
    struct {
        size_t mar_x[3], mar_xy, sel_cnt;
        uint8_t bits_x, mar_x_cnt;
    } *snp_data = NULL;
    
    if ((phen_cnt && !phen_perm) ||
        !array_init(&filter, NULL, snp_cnt * phen_cnt, sizeof(*filter), 0, ARRAY_STRICT) ||
        !array_init(&snp_data, NULL, snp_cnt, sizeof(*snp_data), 0, ARRAY_CLEAR | ARRAY_STRICT)) goto error;        
      
    //  Initialization
    size_t table[9] = { 0 }, mar_y[3] = { 0 };
    double outer[9];

    double density = 0.;
    size_t density_cnt = 0;

    for (size_t j = 0, off = 0; j < snp_cnt; j++, off += phen_cnt)
    {
        // Initializing genotype filter
        size_t sel_cnt = 0;
        for (size_t i = 0; i < phen_cnt; i++) if (gen[i + off] < 3) filter[sel_cnt++ + off] = i;

        if (!sel_cnt) continue; // There is nothing to do if the count is zero
        snp_data[j].sel_cnt = sel_cnt;

        // Counting unique genotypes
        uint8_t bits_x = 0;
        for (size_t i = 0; i < sel_cnt && bits_x != 7; bits_x |= 1 << gen[filter[i++ + off] + off]);
        uint8_t mar_x_cnt = bits_check(bits_x);
        if (!mar_x_cnt) continue;
        snp_data[j].bits_x = bits_x;
        snp_data[j].mar_x_cnt = mar_x_cnt;
        
        // Counting unique phenotypes
        uint8_t bits_y = 0;
        for (size_t i = 0; i < sel_cnt && bits_y != 7; bits_y |= 1 << phen[filter[i++ + off]]);
        uint8_t mar_y_cnt = bits_check(bits_y);
        if (!mar_y_cnt) continue;

        // Building contingency table
        for (size_t i = 0; i < sel_cnt; i++)
        {
            size_t ind = filter[i + off];
            table[gen[ind + off] + 3 * phen[ind]]++;
        }
        table_shuffle(table, bits_x, bits_y);

        // Computing sums
        size_t *mar_x = snp_data[j].mar_x, mar_xy = 0;
        for (uint8_t t = 0; t < mar_y_cnt; mar_xy += mar_y[t], t++) for (uint8_t s = 0; s < mar_x_cnt; s++)
        {
            size_t el = table[s + mar_x_cnt * t];
            mar_x[s] += el;
            mar_y[t] += el;
        }
        snp_data[j].mar_xy = mar_xy;

        density += outer_prod(table, outer, mar_x, mar_y, mar_xy, mar_x_cnt, mar_y_cnt) ?
            stat_chisq(table, outer, mar_x_cnt, mar_y_cnt) :
            stat_exact(table, mar_x, mar_y, rel_err);
        density_cnt++;
    }

    if (!density_cnt) goto error;
    density /= density_cnt;

    // Simulations
    size_t rpl = 0, qc = 0, rpl_max = *p_rpl;
    for (; rpl < rpl_max && (!k || qc <= k); rpl++) // Entering 'fixed' mode if 'k' is 0
    {
        // Generating random permutation
        memcpy(phen_perm, phen, phen_cnt * sizeof(*phen_perm));
        for (size_t i = 0; i < phen_cnt; i++) // Performing 'Knuth shuffle'
        {
            size_t j = i + (size_t) floor(gsl_rng_uniform(rng) * (phen_cnt - i));
            uint8_t swp = phen_perm[i];
            phen_perm[i] = phen_perm[j];
            phen_perm[j] = swp;
        }

        // Density computation
        double density_perm = 0.;
        size_t density_perm_cnt = 0;

        for (size_t j = 0, off = 0; j < snp_cnt; j++, off += phen_cnt) if (snp_data[j].sel_cnt && snp_data[j].mar_x_cnt)
        {
            size_t sel_cnt = snp_data[j].sel_cnt, mar_x_cnt = snp_data[j].mar_x_cnt;
            
            // Counting unique phenotypes
            uint8_t bits_y = 0;
            for (size_t i = 0; i < sel_cnt && bits_y != 7; bits_y |= 1 << phen_perm[filter[i++ + off]]);
            uint8_t mar_y_cnt = bits_check(bits_y);
            if (!mar_y_cnt) continue;
            
            // Building contingency table
            memset(table, 0, sizeof(table));
            for (size_t i = 0; i < sel_cnt; i++)
            {
                size_t ind = filter[i + off];
                table[gen[ind + off] + 3 * phen_perm[ind]]++;
            }
            table_shuffle(table, snp_data[j].bits_x, bits_y);

            // Computing sums
            size_t *mar_x = snp_data[j].mar_x; // Row sums have been already computed
            memset(mar_y, 0, mar_y_cnt * sizeof(*mar_y));
            for (uint8_t t = 0; t < mar_y_cnt; t++) for (uint8_t s = 0; s < mar_x_cnt; mar_y[t] += table[s + mar_x_cnt * t], s++);
                        
            density_perm += outer_prod(table, outer, mar_x, mar_y, snp_data[j].mar_xy, mar_x_cnt, mar_y_cnt) ?
                stat_chisq(table, outer, mar_x_cnt, mar_y_cnt) :
                stat_exact(table, mar_x, mar_y, rel_err);
            density_perm_cnt++;
        }
        if (!density_perm_cnt) continue;
        if (density_perm > density * (double) density_perm_cnt) qc++;
    }   

    res = (double) qc / (double) rpl;
    *p_rpl = rpl;

error:
    free(snp_data);
    free(filter);
    free(phen_perm);
    return res;
}

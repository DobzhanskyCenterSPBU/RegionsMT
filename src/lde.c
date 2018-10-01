#include "ll.h"
#include "lde.h"

double maf_impl(uint8_t *gen, size_t phen_cnt)
{
    size_t t[3] = { 0 };
    for (size_t i = 0; i < phen_cnt; i++) if (gen[i] < 3) t[gen[i]]++;
    double p = (double) t[0] + (double) t[0] + (double) t[1], q = (double) t[1] + (double) t[2] + (double) t[2];
    return MIN(p, q) / (p + q);
}

void maf_all(uint8_t *gen, size_t snp_cnt, size_t phen_cnt, double *maf)
{
    for (size_t i = 0; i < phen_cnt; i++) maf[i] = maf_impl(gen + phen_cnt * i, phen_cnt);
}

double lde_impl(uint8_t *gen_pos, uint8_t *gen_neg, size_t phen_cnt)
{
    size_t t[9] = { 0 };
    for (size_t i = 0; i < phen_cnt; i++) if (gen_pos[i] < 3 && gen_neg[i] < 3) t[gen_pos[i] + 3 * gen_neg[i]]++;
    double ts = (double) t[0];
    for (size_t i = 1; i < 9; ts += (double) t[i++]);
    double fr[] = { 
        ((double) t[0] + .5 * ((double) t[1] + (double) t[3]) + .25 * (double) t[4]) / ts,
        ((double) t[2] + .5 * ((double) t[1] + (double) t[5]) + .25 * (double) t[4]) / ts,
        ((double) t[6] + .5 * ((double) t[3] + (double) t[7]) + .25 * (double) t[4]) / ts,
        ((double) t[8] + .5 * ((double) t[5] + (double) t[7]) + .25 * (double) t[4]) / ts
    };
    double mar_pos[] = { fr[0] + fr[1], fr[2] + fr[3] }, mar_neg[] = { fr[0] + fr[2], fr[1] + fr[3] }, cov = (double) fr[0] - mar_pos[0] * mar_neg[0];
    if (cov == 0.) return 0.;
    double pr0, pr1;
    if (cov > 0.) pr0 = mar_pos[0] * mar_pos[1], pr1 = mar_neg[0] * mar_neg[1];
    else pr0 = mar_pos[0] * mar_neg[1], pr1 = mar_neg[0] * mar_pos[1];
    return cov / MIN(pr0, pr1);
}


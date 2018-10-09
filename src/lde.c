#include "ll.h"
#include "lde.h"

double maf_impl(uint8_t *gen, size_t phen_cnt)
{
    size_t t[3] = { 0 };
    for (size_t i = 0; i < phen_cnt; i++) if (gen[i] < 3) t[gen[i]]++;
    size_t p = t[0] + t[0] + t[1], q = t[1] + t[2] + t[2];
    return (double) MIN(p, q) / (double) (p + q);
}

void maf_all(uint8_t *gen, size_t phen_cnt, double *maf)
{
    for (size_t i = 0; i < phen_cnt; i++) maf[i] = maf_impl(gen + phen_cnt * i, phen_cnt);
}

double lde_impl(uint8_t *gen_pos, uint8_t *gen_neg, size_t phen_cnt)
{
    size_t t[9] = { 0 };
    for (size_t i = 0; i < phen_cnt; i++) if (gen_pos[i] < 3 && gen_neg[i] < 3) t[gen_pos[i] + 3 * gen_neg[i]]++;
    size_t ts = t[0];
    for (size_t i = 1; i < 9; ts += t[i++]);
    size_t fr[] = { 
        4 * t[0] + 2 * (t[1] + t[3]) + t[4], 
        4 * t[2] + 2 * (t[1] + t[5]) + t[4],
        4 * t[6] + 2 * (t[3] + t[7]) + t[4],
        4 * t[8] + 2 * (t[5] + t[7]) + t[4]
    };
    size_t mar_pos[] = { fr[0] + fr[1], fr[2] + fr[3] }, mar_neg[] = { fr[0] + fr[2], fr[1] + fr[3] };
    size_t bor, cov = size_sub(&bor, 4 * fr[0] * ts, mar_pos[0] * mar_neg[0]); // Actual covariance = 'cov / (16 * ts * ts)'
    if (!cov) return 0.;
    size_t pr0, pr1;
    if (!bor) pr0 = mar_pos[0] * mar_pos[1], pr1 = mar_neg[0] * mar_neg[1];
    else pr0 = mar_pos[0] * mar_neg[1], pr1 = mar_neg[0] * mar_pos[1];
    return (double) cov / (double) MIN(pr0, pr1);
}


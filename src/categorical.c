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

#define DECLARE_BITS_INIT(TYPE, PREFIX) \
    static size_t PREFIX ## _bits_init(uint8_t *bits, size_t cnt, size_t ucnt, size_t *filter, TYPE *data) \
    { \
        size_t res = 0; \
        for (size_t i = 0; i < cnt; i++) \
        { \
            if (uint8_bit_test_set(bits, data[filter[i]])) continue; \
            if (++res == ucnt) break; \
        } \
        return res; \
    }

DECLARE_BITS_INIT(uint8_t, gen)
DECLARE_BITS_INIT(size_t, phen)

#define GEN_CNT 3
#define ALT_CNT 4

static size_t gen_pop_cnt_codominant(uint8_t *bits, size_t pop_cnt)
{
    (void) bits;
    return pop_cnt;
}

static size_t gen_pop_cnt_recessive(uint8_t *bits, size_t pop_cnt)
{
    return pop_cnt == 2 ? bits[0] == (1 | 4) || bits[0] == (2 | 4) ? 2 : 1 : MIN(pop_cnt, 2);
}

static size_t gen_pop_cnt_dominant(uint8_t *bits, size_t pop_cnt)
{
    return pop_cnt == 2 ? bits[0] == (1 | 2) || bits[0] == (1 | 4) ? 2 : 1 : MIN(pop_cnt, 2);
}

static size_t gen_pop_cnt_allelic(uint8_t *bits, size_t pop_cnt)
{
    (void) bits;
    return MIN(pop_cnt, 2);
}

static void gen_shuffle_codominant(size_t *dst, size_t *src, uint8_t *bits, size_t pop_cnt)
{
    (void) pop_cnt;
    switch (bits[0])
    {
    case (1 | 2 | 4):
        dst[2] = src[2];
    case (1 | 2):
        dst[0] = src[0];
        dst[1] = src[1];
        break;
    case (1 | 4):
        dst[0] = src[0];
        dst[1] = src[2];
        break;
    case (2 | 4):
        dst[0] = src[1];
        dst[1] = src[2];
        break;
    }
}

static void gen_shuffle_recessive(size_t *dst, size_t *src, uint8_t *bits, size_t pop_cnt)
{
    (void) pop_cnt;
    switch (bits[0])
    {
    case (1 | 2 | 4):
        dst[0] = src[0] + src[1];
        dst[1] = src[2];
        break;
    case (2 | 4):
        dst[0] = src[1];
        dst[1] = src[2];
    case (1 | 4):
        dst[0] = src[0];
        dst[1] = src[2];
        break;
    }
}

static void gen_shuffle_dominant(size_t *dst, size_t *src, uint8_t *bits, size_t pop_cnt)
{
    (void) pop_cnt;
    switch (bits[0])
    {
    case (1 | 2 | 4):
        dst[0] = src[0];
        dst[1] = src[1] + src[2];
        break;
    case (1 | 4):
        dst[0] = src[0];
        dst[1] = src[2];
        break;
    case (1 | 2):
        dst[0] = src[0];
        dst[1] = src[1];
        break;
    }
}

static void gen_shuffle_allelic(size_t *dst, size_t *src, uint8_t *bits, size_t pop_cnt)
{
    (void) pop_cnt;
    switch (bits[0])
    {
    case (1 | 2 | 4):
        dst[0] = src[0] + src[1];
        dst[1] = src[1] + src[2];
        break;
    case (2 | 4):
        dst[0] = src[1];
        dst[1] = src[1] + src[2];
    case (1 | 4):
        dst[0] = src[0];
        dst[1] = src[1];
        break;
    case (1 | 2):
        dst[0] = src[0] + src[1];
        dst[1] = src[1];
        break;
    }
}

typedef size_t (*gen_pop_cnt_alt_callback)(uint8_t *, size_t);
typedef void (*gen_shuffle_alt_callback)(size_t *, size_t *, uint8_t *, size_t);

gen_pop_cnt_alt_callback get_gen_pop_cnt_alt(size_t ind)
{
    return (gen_pop_cnt_alt_callback[]) { gen_pop_cnt_codominant, gen_pop_cnt_recessive, gen_pop_cnt_dominant, gen_pop_cnt_allelic }[ind];
}

gen_shuffle_alt_callback get_gen_shuffle_alt(size_t ind)
{
    return (gen_shuffle_alt_callback[]) { gen_shuffle_codominant, gen_shuffle_recessive, gen_shuffle_dominant, gen_shuffle_allelic }[ind];
}

void table_shuffle(size_t *dst, size_t *src, uint8_t *gen_bits, size_t gen_pop_cnt, uint8_t *phen_bits, size_t phen_pop_cnt, gen_shuffle_alt_callback gen_shuffle_alt)
{
    size_t off = 0;
    for (size_t i = 0, j = 0; i < phen_pop_cnt; i++, j += GEN_CNT)
    {
        if (!uint8_bit_test(phen_bits, i)) continue;
        gen_shuffle_alt(dst + off, src + j, gen_bits, gen_pop_cnt);
        off += gen_pop_cnt;
    }
}

struct categorical_supp {
    uint8_t *phen_bits;
    size_t *filter, *table, *phen_mar;
    double *outer;
    double nlpv[ALT_CNT], qas[ALT_CNT];
    size_t rpl[ALT_CNT];
};

struct categorical_snp_data {
    size_t gen_mar[GEN_CNT * ALT_CNT], gen_phen_mar[ALT_CNT], cnt, gen_pop_cnt_alt[ALT_CNT], flags_pop_cnt;
    uint8_t gen_bits[UINT8_CNT(GEN_CNT)];
};

struct maver_adj_supp {
    uint8_t *phen_bits;
    size_t *filter, *table, *phen_mar, *phen_perm;
    double *outer;
    struct categorical_snp_data *snp_data;
    double nlpv[ALT_CNT];
    size_t rpl[ALT_CNT];
};

_Static_assert((2 * GEN_CNT * sizeof(size_t)) / (2 * GEN_CNT) == sizeof(size_t), "Multiplication overflow!");
_Static_assert((GEN_CNT * sizeof(double)) / GEN_CNT == sizeof(double), "Multiplication overflow!");

bool categorical_init(struct categorical_supp *supp, size_t phen_cnt, size_t phen_ucnt)
{
    if (phen_ucnt > phen_cnt) return 0; // Wrong parameter    
    supp->phen_mar = malloc(phen_ucnt * sizeof(*supp->phen_mar));
    supp->phen_bits = malloc(UINT8_CNT(phen_ucnt) * sizeof(*supp->phen_bits));
    supp->filter = malloc(phen_cnt * sizeof(*supp->filter));

    if ((phen_ucnt && !supp->phen_mar && !supp->phen_bits) ||
        (phen_cnt && !supp->filter) ||
        !array_init(&supp->outer, NULL, phen_ucnt, GEN_CNT * sizeof(*supp->outer), 0, ARRAY_STRICT) ||
        !array_init(&supp->table, NULL, phen_ucnt, 2 * GEN_CNT * sizeof(*supp->table), 0, ARRAY_STRICT)) return 0;

    return 1;
}

void categorical_close(struct categorical_supp *supp)
{
    free(supp->phen_mar);
    free(supp->phen_bits);
    free(supp->filter);
    free(supp->outer);
    free(supp->table);
}

bool maver_adj_init(struct maver_adj_supp *supp, size_t snp_cnt, size_t phen_cnt, size_t phen_ucnt)
{
    if (phen_ucnt > phen_cnt) return 0; // Wrong parameter    
    supp->phen_perm = malloc(phen_cnt * sizeof(*supp->phen_perm));
    supp->phen_mar = malloc(phen_ucnt * sizeof(*supp->phen_mar));
    supp->phen_bits = malloc(UINT8_CNT(phen_ucnt) * sizeof(*supp->phen_bits));

    if ((phen_ucnt && !supp->phen_mar && !supp->phen_bits) ||
        (phen_cnt && !supp->phen_perm) ||
        !array_init(&supp->snp_data, NULL, snp_cnt, sizeof(*supp->snp_data), 0, ARRAY_CLEAR | ARRAY_STRICT) ||
        !array_init(&supp->filter, NULL, snp_cnt * phen_cnt, sizeof(*supp->filter), 0, ARRAY_STRICT) || // Result of 'snp_cnt * phen_cnt' is assumed not to be wrapped due to the validness of the 'gen' array
        !array_init(&supp->outer, NULL, phen_ucnt, GEN_CNT * sizeof(*supp->outer), 0, ARRAY_STRICT) ||
        !array_init(&supp->table, NULL, phen_ucnt, 2 * GEN_CNT * sizeof(*supp->table), 0, ARRAY_STRICT)) return 0;

    return 1;
}

void maver_adj_close(struct maver_adj_supp *supp)
{
    free(supp->phen_perm);
    free(supp->phen_mar);
    free(supp->phen_bits);
    free(supp->snp_data);
    free(supp->filter);
    free(supp->outer);
    free(supp->table);
}

static size_t filter_init(size_t *filter, uint8_t *gen, size_t phen_cnt)
{
    size_t cnt = 0;
    for (size_t i = 0; i < phen_cnt; i++) if (gen[i] < GEN_CNT) filter[cnt++] = i;
    return cnt;
}

static size_t gen_pop_cnt_alt_init(size_t *gen_pop_cnt_alt, uint8_t *gen_bits, size_t gen_pop_cnt, enum categorical_flags flags)
{
    size_t res = 0;
    for (size_t i = 0; i < ALT_CNT; i++, flags >>= 1) if (flags & 1)
    {
        size_t tmp = get_gen_pop_cnt_alt(i)(gen_bits, gen_pop_cnt);
        if (tmp < 2) continue;
        gen_pop_cnt_alt[i] = tmp;
        res++;
    }
    return res;
}

static void contingency_table_init(size_t *table, uint8_t *gen, size_t *phen, size_t cnt, size_t *filter)
{
    for (size_t i = 0; i < cnt; i++)
    {
        size_t ind = filter[i];
        table[gen[ind] + GEN_CNT * phen[ind]]++;
    }
}

static void gen_phen_mar_init(size_t *table, size_t *gen_mar, size_t *phen_mar, size_t *p_gen_phen_mar, size_t gen_pop_cnt, size_t phen_pop_cnt)
{
    size_t gen_phen_mar = 0;
    for (size_t t = 0; t < phen_pop_cnt; gen_phen_mar += phen_mar[t], t++) for (size_t s = 0; s < gen_pop_cnt; s++)
    {
        size_t el = table[s + gen_pop_cnt * t];
        gen_mar[s] += el;
        phen_mar[t] += el;
    }
    *p_gen_phen_mar = gen_phen_mar;
}

static void phen_mar_init(size_t *table, size_t *phen_mar, size_t gen_pop_cnt, size_t phen_pop_cnt)
{
    for (size_t t = 0; t < phen_pop_cnt; t++) for (size_t s = 0; s < gen_pop_cnt; phen_mar[t] += table[s + gen_pop_cnt * t], s++);
}

static void outer_prod_chisq(size_t *table, double *outer, size_t *gen_mar, size_t *phen_mar, size_t gen_phen_mar, size_t gen_pop_cnt, size_t phen_pop_cnt)
{
    for (size_t i = 0; i < phen_pop_cnt; i++) for (size_t j = 0; j < gen_pop_cnt; j++)
        outer[j + gen_pop_cnt * i] = (double) gen_mar[j] * (double) phen_mar[i] / (double) gen_phen_mar;
}

static bool outer_prod_combined(size_t *table, double *outer, size_t *gen_mar, size_t *phen_mar, size_t gen_phen_mar, size_t gen_pop_cnt, size_t phen_pop_cnt)
{
    if (gen_pop_cnt > 2 || phen_pop_cnt > 2)
    {
        outer_prod_chisq(table, outer, gen_mar, phen_mar, gen_phen_mar, gen_pop_cnt, phen_pop_cnt);
        return 1;
    }
    for (size_t i = 0; i < phen_pop_cnt; i++) for (size_t j = 0; j < gen_pop_cnt; j++)
    {
        double tmp = (double) gen_mar[j] * (double) phen_mar[i] / (double) gen_phen_mar;
        if (tmp >= 5.) outer[j + gen_pop_cnt * i] = tmp;
        else return 0;
    }
    return 1;
}

static double stat_exact(size_t *table, size_t *gen_mar, size_t *phen_mar, double rel_err)
{
    size_t lo = size_sub_sat(gen_mar[0], phen_mar[1]), hi = MIN(gen_mar[0], phen_mar[0]);
    double hyp_comp = pdf_hypergeom(table[0], phen_mar[0], phen_mar[1], gen_mar[0]), hyp_sum = 0., hyp_less = 0.;
    for (size_t i = lo; i <= hi; i++)
    {
        double hyp = pdf_hypergeom(i, phen_mar[0], phen_mar[1], gen_mar[0]);
        hyp_sum += hyp;
        if (hyp <= hyp_comp * rel_err) hyp_less += hyp;
    }
    return log10(hyp_sum) - log10(hyp_less);
}

static double stat_chisq(size_t *table, double *outer, size_t gen_pop_cnt, size_t phen_pop_cnt)
{
    double stat = 0.;
    size_t pr = gen_pop_cnt * phen_pop_cnt;
    for (size_t i = 0; i < pr; i++)
    {
        double out = outer[i], diff = out - (double) table[i];
        stat += diff * diff / out;
    }
    return -log10(cdf_chisq_Q(stat, (double) (pr - gen_pop_cnt - phen_pop_cnt + 1)));
}

static void perm_init(size_t *perm, size_t cnt, gsl_rng *rng)
{
    for (size_t i = 0; i < cnt; i++) // Performing 'Knuth shuffle'
    {
        size_t j = i + (size_t) floor(gsl_rng_uniform(rng) * (cnt - i));
        size_t swp = perm[i];
        perm[i] = perm[j];
        perm[j] = swp;
    }
}

void categorical_impl(struct categorical_supp *supp, uint8_t *gen, size_t *phen, size_t snp_cnt, size_t phen_cnt, size_t phen_ucnt, double rel_err, enum categorical_flags flags)
{    
    const size_t table_disp = GEN_CNT * phen_ucnt;
       
    // Initializing genotype filter
    size_t cnt = filter_init(supp->filter, gen, phen_cnt);
    if (!cnt) return;
    
    // Counting unique genotypes
    uint8_t gen_bits[UINT8_CNT(GEN_CNT)] = { 0 };
    size_t gen_pop_cnt = gen_bits_init(gen_bits, cnt, GEN_CNT, supp->filter, gen);
    size_t gen_pop_cnt_alt[ALT_CNT] = { 0 };
    if (!gen_pop_cnt_alt_init(gen_pop_cnt_alt, gen_bits, gen_pop_cnt, flags)) return;
    
    // Counting unique phenotypes
    memset(supp->phen_bits, 0, UINT8_CNT(phen_ucnt));
    size_t phen_pop_cnt = phen_bits_init(supp->phen_bits, cnt, phen_ucnt, supp->filter, phen);
    if (phen_pop_cnt < 2) return;

    // Building contingency table
    memset(supp->table + table_disp, 0, table_disp * sizeof(*supp->table));
    contingency_table_init(supp->table + table_disp, gen, phen, cnt, supp->filter);

    // Performing computations for each alternative
    size_t gen_mar[GEN_CNT];
    for (size_t i = 0; i < ALT_CNT; i++)
    {
        gen_pop_cnt = gen_pop_cnt_alt[i];
        if (gen_pop_cnt)
        {
            table_shuffle(supp->table, supp->table + table_disp, gen_bits, gen_pop_cnt, supp->phen_bits, phen_pop_cnt, get_gen_shuffle_alt(i));

            // Computing sums
            size_t gen_phen_mar = 0;
            memset(gen_mar, 0, gen_pop_cnt * sizeof(*gen_mar));
            memset(supp->phen_mar, 0, phen_pop_cnt * sizeof(*supp->phen_mar));
            gen_phen_mar_init(supp->table, gen_mar, supp->phen_mar, &gen_phen_mar, gen_pop_cnt, phen_pop_cnt);
            
            if (outer_prod_combined(supp->table, supp->outer, gen_mar, supp->phen_mar, gen_phen_mar, gen_pop_cnt, phen_pop_cnt))
            {
                supp->nlpv[i] = stat_chisq(supp->table, supp->outer, gen_pop_cnt, phen_pop_cnt);
                supp->qas[i] = 0.;
            }
            else
            {
                supp->nlpv[i] = stat_exact(supp->table, gen_mar, supp->phen_mar, rel_err);
                supp->qas[i] = 0.;
            }
        }
    }
}

void maver_adj_impl(struct maver_adj_supp *supp, uint8_t *gen, size_t *phen, size_t snp_cnt, size_t phen_cnt, size_t phen_ucnt, size_t *p_rpl, size_t k, gsl_rng *rng, enum categorical_flags flags)
{
    const size_t table_disp = GEN_CNT * phen_ucnt;
        
    //  Initialization
    double density[ALT_CNT] = { 0. };
    size_t density_cnt[ALT_CNT] = { 0 };

    for (size_t j = 0, off = 0; j < snp_cnt; j++, off += phen_cnt)
    {
        // Initializing genotype filter
        size_t cnt = filter_init(supp->filter + off, gen + off, phen_cnt);
        if (!cnt) continue;
        supp->snp_data[j].cnt = cnt;

        // Counting unique genotypes
        size_t gen_pop_cnt = gen_bits_init(supp->snp_data[j].gen_bits, cnt, GEN_CNT, supp->filter + off, gen + off);
        size_t flags_pop_cnt = gen_pop_cnt_alt_init(supp->snp_data[j].gen_pop_cnt_alt, supp->snp_data[j].gen_bits, gen_pop_cnt, flags);
        if (!flags_pop_cnt) continue;
        supp->snp_data[j].flags_pop_cnt = flags_pop_cnt;

        // Counting unique phenotypes
        memset(supp->phen_bits, 0, UINT8_CNT(phen_ucnt));
        size_t phen_pop_cnt = phen_bits_init(supp->phen_bits, cnt, phen_ucnt, supp->filter + off, phen);
        if (phen_pop_cnt < 2) continue;

        // Building contingency table
        memset(supp->table + table_disp, 0, table_disp * sizeof(*supp->table));
        contingency_table_init(supp->table + table_disp, gen + off, phen, cnt, supp->filter);

        // Performing computations for each alternative
        for (size_t i = 0; i < ALT_CNT; i++) 
        {
            gen_pop_cnt = supp->snp_data[j].gen_pop_cnt_alt[i];
            if (gen_pop_cnt)
            {
                table_shuffle(supp->table, supp->table + table_disp, supp->snp_data[j].gen_bits, gen_pop_cnt, supp->phen_bits, phen_pop_cnt, get_gen_shuffle_alt(i));
                
                // Computing sums
                memset(supp->phen_mar, 0, phen_pop_cnt * sizeof(*supp->phen_mar));
                gen_phen_mar_init(supp->table, supp->snp_data[j].gen_mar + i * GEN_CNT, supp->phen_mar, supp->snp_data[j].gen_phen_mar + i, gen_pop_cnt, phen_pop_cnt);
                outer_prod_chisq(supp->table, supp->outer, supp->snp_data[j].gen_mar + i * GEN_CNT, supp->phen_mar, supp->snp_data[j].gen_phen_mar[i], gen_pop_cnt, phen_pop_cnt);
                density[i] += stat_chisq(supp->table, supp->outer, gen_pop_cnt, phen_pop_cnt);
                density_cnt[i]++;
            }
        }
    }

    bool alt[ALT_CNT];
    for (size_t i = 0; i < ALT_CNT; i++, flags >>= 1) alt[i] = (flags & 1) && isfinite(density[i] /= density_cnt[i]);

    // Simulations
    size_t qc[ALT_CNT] = { 0 }, qt[ALT_CNT] = { 0 }, rpl_max = *p_rpl;
    for (size_t rpl = 0; rpl < rpl_max; rpl++) // Entering 'fixed' mode if 'k' is 0
    {
        bool alt_rpl[ALT_CNT], alt_any = 0;
        for (size_t i = 0; i < ALT_CNT; i++) alt_any |= (alt_rpl[i] = alt[i] && (!k || qc[i] < k));
        if (!alt_any) break;

        // Generating random permutation
        memcpy(supp->phen_perm, phen, phen_cnt * sizeof(*supp->phen_perm));
        perm_init(supp->phen_perm, phen_cnt, rng);
        
        // Density computation
        double density_perm[ALT_CNT] = { 0. };
        size_t density_perm_cnt[ALT_CNT] = { 0 };

        for (size_t j = 0, off = 0; j < snp_cnt; j++, off += phen_cnt)
        {
            size_t cnt = supp->snp_data[j].cnt;
            if (!cnt || !supp->snp_data[j].flags_pop_cnt) continue;

            // Counting unique phenotypes
            memset(supp->phen_bits, 0, UINT8_CNT(phen_ucnt));
            size_t phen_pop_cnt = phen_bits_init(supp->phen_bits, cnt, phen_ucnt, supp->filter + off, supp->phen_perm);
            if (phen_pop_cnt < 2) continue;

            // Building contingency table
            memset(supp->table + table_disp, 0, table_disp * sizeof(*supp->table));
            contingency_table_init(supp->table + table_disp, gen + off, phen, cnt, supp->filter);

            // Performing computations for each alternative
            for (size_t i = 0; i < ALT_CNT; i++)
            {
                size_t gen_pop_cnt = supp->snp_data[j].gen_pop_cnt_alt[i];
                if (alt_rpl[i] && gen_pop_cnt)
                {
                    table_shuffle(supp->table, supp->table + table_disp, supp->snp_data[j].gen_bits, gen_pop_cnt, supp->phen_bits, phen_pop_cnt, get_gen_shuffle_alt(i));

                    // Computing sums
                    memset(supp->phen_mar, 0, phen_pop_cnt * sizeof(*supp->phen_mar));
                    phen_mar_init(supp->table, supp->phen_mar, gen_pop_cnt, phen_pop_cnt);
                    outer_prod_chisq(supp->table, supp->outer, supp->snp_data[j].gen_mar + i * GEN_CNT, supp->phen_mar, supp->snp_data[j].gen_phen_mar[i], gen_pop_cnt, phen_pop_cnt);
                    density_perm[i] += stat_chisq(supp->table, supp->outer, gen_pop_cnt, phen_pop_cnt);
                    density_perm_cnt[i]++;
                }
            }
        }

        for (size_t i = 0; i < ALT_CNT; i++) if (alt_rpl[i])
        {
            if (density_perm[i] > density[i] * (double) density_perm_cnt[i]) qc[i]++;
            qt[i]++;
        }
    }

    for (size_t i = 0; i < ALT_CNT; i++)
    {
        if (alt[i]) supp->nlpv[i] = (double) qc[i] / (double) qt[i], supp->rpl[i] = qt[i];
        else supp->rpl[i] = 0;
    }
}

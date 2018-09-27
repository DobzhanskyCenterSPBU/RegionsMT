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

static bool outer_prod(size_t *table, double *outer, size_t *gen_mar, size_t *phen_mar, size_t gen_phen_mar, size_t gen_pop_cnt, size_t phen_pop_cnt)
{
    if (gen_pop_cnt > 2 || phen_pop_cnt > 2)
    {
        for (size_t i = 0; i < phen_pop_cnt; i++) for (size_t j = 0; j < gen_pop_cnt; j++)
            outer[j + gen_pop_cnt * i] = (double) gen_mar[j] * (double) phen_mar[i] / (double) gen_phen_mar;
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

typedef void (*gen_copy_callback)(size_t *, size_t *, uint8_t *, size_t);

gen_copy_callback get_gen_copy(enum categorical_test_type type)
{
    return (gen_copy_callback[]) { gen_shuffle_codominant, gen_shuffle_recessive, gen_shuffle_dominant, gen_shuffle_allelic }[type];
}

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

void table_shuffle(size_t *dst, size_t *src, uint8_t *gen_bits, size_t gen_pop_cnt, uint8_t *phen_bits, size_t phen_pop_cnt, gen_copy_callback gen_copy)
{
    size_t off = 0;
    for (size_t i = 0, j = 0; i < phen_pop_cnt; i++, j += GEN_CNT)
    {
        if (!uint8_bit_test(phen_bits, i)) continue;
        gen_copy(dst + j, src + j, gen_bits, gen_pop_cnt);
        if (off < j) memcpy(dst + off, src + j, gen_pop_cnt * sizeof(*dst));
        off += gen_pop_cnt;
    }
}

struct categorical_supp {
    uint8_t *phen_bits;
    size_t *filter, *table, *phen_mar;
    double *outer;
};

struct categorical_snp_data {
    size_t gen_mar[GEN_CNT], gen_phen_mar, cnt, gen_pop_cnt_alt[ALT_CNT];
    uint8_t gen_bits[UINT8_CNT(GEN_CNT)];
};

struct maver_adj_supp {
    uint8_t *phen_bits;
    size_t *filter, *table, *phen_mar, *phen_perm;
    double *outer;
    struct categorical_snp_data *snp_data;
};

_Static_assert((2 * GEN_CNT * sizeof(size_t)) / (2 * GEN_CNT) == sizeof(size_t), "Multiplication overflow!");
_Static_assert((GEN_CNT * sizeof(double)) / GEN_CNT == sizeof(double), "Multiplication overflow!");

bool categorical_init(struct categorical_supp *supp, size_t phen_cnt, size_t phen_ucnt)
{
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
    size_t (*const gen_pop_cnt_alt_impl[])(uint8_t *, size_t) = { gen_pop_cnt_codominant, gen_pop_cnt_recessive, gen_pop_cnt_dominant, gen_pop_cnt_allelic };
    for (size_t i = 0; i < MIN(ALT_CNT, countof(gen_pop_cnt_alt_impl)); i++, flags >>= 1) if (flags & 1)
    {
        size_t tmp = gen_pop_cnt_alt_impl[i](gen_bits, gen_pop_cnt);
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

#if 0

bool categorical_c_impl(struct categorical_supp *supp, uint8_t *gen, size_t phen_cnt, size_t phen_ucnt)
{    
    table_shuffle(table, gen_bits, gen_pop_cnt, phen_bits, phen_pop_cnt, gen_shuffle_impl);

    // Computing sums
    size_t gen_phen_mar = 0;
    memset(phen_mar, 0, phen_pop_cnt * sizeof(*phen_mar));
    for (size_t t = 0; t < phen_pop_cnt; gen_phen_mar += phen_mar[t], t++) for (size_t s = 0; s < gen_pop_cnt; s++)
    {
        size_t el = table[s + gen_pop_cnt * t];
        gen_mar[s] += el;
        phen_mar[t] += el;
    }
    *p_gen_phen_mar = gen_phen_mar;
}

#endif

double maver_adj_impl(struct maver_adj_supp *supp, uint8_t *gen, size_t *phen, size_t snp_cnt, size_t phen_cnt, size_t phen_ucnt, size_t *p_rpl, size_t k, double rel_err, gsl_rng *rng, enum categorical_flags flags)
{
    if (phen_ucnt > phen_cnt) return 0; // Wrong parameter

    double res = nan(__func__);
    
    //gen_pop_cnt_callback gen_pop_cnt_impl = get_gen_pop_cnt(type);
    //gen_copy_callback gen_copy_impl = get_gen_copy(type);
    
    //  Initialization
    double density = 0.;
    size_t density_cnt = 0;

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

        // Counting unique phenotypes
        memset(supp->phen_bits, 0, UINT8_CNT(phen_ucnt));
        size_t phen_pop_cnt = phen_bits_init(supp->phen_bits, cnt, phen_ucnt, supp->filter + off, phen);
        if (phen_pop_cnt < 2) continue;

        // Building contingency table
        memset(supp->table, 0, GEN_CNT * phen_ucnt * sizeof(*supp->table));
        contingency_table_init(supp->table, gen + off, phen, cnt, supp->filter);

        for (size_t k = 0; k < ALT_CNT; k++) if (supp->snp_data[j].gen_pop_cnt_alt[k])
        {
            size_t disp;
            if (flags_pop_cnt--)
            {
                disp = GEN_CNT * phen_ucnt;
                memcpy(table + disp, table, disp )
            }
            else disp = 0;

        }

        table_shuffle(table, snp_data[j].gen_bits, gen_pop_cnt, phen_bits, phen_pop_cnt, gen_copy_impl);

        // Computing sums
        size_t *gen_mar = snp_data[j].gen_mar, gen_phen_mar = 0;
        memset(phen_mar, 0, phen_pop_cnt * sizeof(*phen_mar));
        for (size_t t = 0; t < phen_pop_cnt; gen_phen_mar += phen_mar[t], t++) for (size_t s = 0; s < gen_pop_cnt; s++)
        {
            size_t el = table[s + gen_pop_cnt * t];
            gen_mar[s] += el;
            phen_mar[t] += el;
        }
        snp_data[j].gen_phen_mar = gen_phen_mar;

        density += outer_prod(table, outer, gen_mar, phen_mar, gen_phen_mar, gen_pop_cnt, phen_pop_cnt) ?
            stat_chisq(table, outer, gen_pop_cnt, phen_pop_cnt) :
            stat_exact(table, gen_mar, phen_mar, rel_err);
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
            size_t swp = phen_perm[i];
            phen_perm[i] = phen_perm[j];
            phen_perm[j] = swp;
        }

        // Density computation
        double density_perm = 0.;
        size_t density_perm_cnt = 0;

        for (size_t j = 0, off = 0; j < snp_cnt; j++, off += phen_cnt) if (snp_data[j].cnt && snp_data[j].gen_pop_cnt)
        {
            size_t cnt = snp_data[j].cnt, gen_pop_cnt = snp_data[j].gen_pop_cnt;
            
            // Counting unique phenotypes
            memset(phen_bits, 0, UINT8_CNT(phen_ucnt));
            size_t phen_pop_cnt = phen_bits_init(phen_bits, cnt, phen_ucnt, filter + off, phen_perm);
            if (phen_pop_cnt < 2) continue;
            
            // Building contingency table
            memset(table, 0, GEN_CNT * phen_ucnt * sizeof(*table));
            for (size_t i = 0; i < cnt; i++)
            {
                size_t ind = filter[i + off];
                table[gen[ind + off] + GEN_CNT * phen_perm[ind]]++;
            }
            table_shuffle(table, snp_data[j].gen_bits, gen_pop_cnt, phen_bits, phen_pop_cnt, gen_copy_impl);

            // Computing sums
            size_t *gen_mar = snp_data[j].gen_mar; // Row sums have been already computed
            memset(phen_mar, 0, phen_pop_cnt * sizeof(*phen_mar));
            for (size_t t = 0; t < phen_pop_cnt; t++) for (size_t s = 0; s < gen_pop_cnt; phen_mar[t] += table[s + gen_pop_cnt * t], s++);
                        
            density_perm += outer_prod(table, outer, gen_mar, phen_mar, snp_data[j].gen_phen_mar, gen_pop_cnt, phen_pop_cnt) ?
                stat_chisq(table, outer, gen_pop_cnt, phen_pop_cnt) :
                stat_exact(table, gen_mar, phen_mar, rel_err);
            density_perm_cnt++;
        }
        if (!density_perm_cnt) continue;
        if (density_perm > density * (double) density_perm_cnt) qc++;
    }   

    res = (double) qc / (double) rpl;
    *p_rpl = rpl;

error:
    return res;
}

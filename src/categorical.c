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

static void gen_shuffle_codominant(size_t *row, uint8_t *bits, size_t pop_cnt)
{
    if (pop_cnt > 2) return;
    switch (bits[0])
    {
    case (1 | 4):
        row[1] = row[2];
        break;
    case (2 | 4):
        row[0] = row[1];
        row[1] = row[2];
        break;
    }
}

static void gen_shuffle_recessive(size_t *row, uint8_t *bits, size_t pop_cnt)
{
    (void) pop_cnt;
    switch (bits[0])
    {
    case (1 | 2 | 4):
    case (2 | 4):
        row[0] += row[1];
    case (1 | 4):
        row[1] = row[2];
        break;
    }
}

static void gen_shuffle_dominant(size_t *row, uint8_t *bits, size_t pop_cnt)
{
    (void) pop_cnt;
    switch (bits[0])
    {
    case (1 | 2 | 4):
    case (1 | 4):
        row[1] += row[2];
        break;
    }
}

static void gen_shuffle_allelic(size_t *row, uint8_t *bits, size_t pop_cnt)
{
    (void) pop_cnt;
    switch (bits[0])
    {
    case (1 | 2 | 4):
    case (2 | 4):
        row[0] += row[1];
    case (1 | 4):
        row[1] += row[2];
        break;
    case (1 | 2):
        row[0] += row[1];
        break;
    }
}

typedef size_t (*gen_pop_cnt_callback)(uint8_t *, size_t);
typedef void (*gen_shuffle_callback)(size_t *, uint8_t *, size_t);

gen_pop_cnt_callback get_gen_pop_cnt(enum categorical_test_type type)
{
    return (gen_pop_cnt_callback[]) { gen_pop_cnt_codominant, gen_pop_cnt_recessive, gen_pop_cnt_dominant, gen_pop_cnt_allelic }[type];
}

gen_shuffle_callback get_gen_shuffle(enum categorical_test_type type)
{
    return (gen_shuffle_callback[]) { gen_shuffle_codominant, gen_shuffle_dominant, gen_shuffle_recessive, gen_shuffle_allelic }[type];
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

#define GEN_MAX 3

void table_shuffle(size_t *table, uint8_t *gen_bits, size_t gen_pop_cnt, uint8_t *phen_bits, size_t phen_pop_cnt, gen_shuffle_callback gen_shuffle)
{
    size_t off = 0;
    for (size_t i = 0, j = 0; i < phen_pop_cnt; i++, j += GEN_MAX)
    {
        if (!uint8_bit_test(phen_bits, i)) continue;
        gen_shuffle(table + j, gen_bits, gen_pop_cnt);
        if (off < j) memcpy(table + off, table + j, gen_pop_cnt * sizeof(*table));
        off += gen_pop_cnt;
    }
}

enum categorical_flags {
    CATEGORICAL_CODOMINANT = 1,
    CATEGORICAL_RECESSIVE = 2,
    CATEGORICAL_DOMINANT = 4,
    CATEGORICAL_ALLELIC = 8,
    CATEGORICAL_FISHER = 16,
    CATEGORICAL_CHISQ = 32,
};

struct categorical_res {
    size_t *phen_mar, *p_gen_phen_mar;
    double *pv;
};

struct categorical_supp {
    uint8_t *gen_bits, *phen_bits;
    size_t *filter, *table, *table_tmp, *gen_mar, *p_cnt, *p_gen_pop_cnt;
};

#if 0

bool categorical_impl(uint8_t *gen, size_t *phen, size_t phen_cnt, size_t phen_ucnt, 
    size_t *filter, 
    size_t *table, 
    size_t *table_tmp, 
    size_t *gen_mar, 
    size_t *phen_mar, 
    size_t *p_gen_phen_mar, 
    size_t *p_cnt, 
    size_t *p_gen_pop_cnt)
{
    // Initializing genotype filter
    size_t cnt = 0;
    for (size_t i = 0; i < phen_cnt; i++) if (gen[i] < GEN_MAX) filter[cnt++] = i;

    if (cnt < 2) return 0; // There is nothing to do if the count is zero
    *p_cnt = cnt;

    // Counting unique genotypes
    size_t gen_pop_cnt = gen_bits_init(gen_bits, cnt, GEN_MAX, filter, gen);
    gen_pop_cnt = gen_pop_cnt_impl(gen_bits, gen_pop_cnt);
    if (gen_pop_cnt < 2) return 0;
    *p_gen_pop_cnt = gen_pop_cnt;

    // Counting unique phenotypes
    memset(phen_bits, 0, UINT8_CNT(phen_ucnt));
    size_t phen_pop_cnt = phen_bits_init(phen_bits, cnt, phen_ucnt, filter, phen);
    if (phen_pop_cnt < 2) return 0;

    // Building contingency table
    memset(table_tmp, 0, GEN_MAX * phen_ucnt * sizeof(*table_tmp));
    for (size_t i = 0; i < cnt; i++)
    {
        size_t ind = filter[i];
        table[gen[ind] + GEN_MAX * phen[ind]]++;
    }
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

_Static_assert((GEN_MAX * sizeof(size_t)) / GEN_MAX == sizeof(size_t), "Multiplication overflow!");
_Static_assert((GEN_MAX * sizeof(double)) / GEN_MAX == sizeof(double), "Multiplication overflow!");

double maver_adj(uint8_t *gen, size_t *phen, size_t snp_cnt, size_t phen_cnt, size_t phen_ucnt, size_t *p_rpl, size_t k, double rel_err, gsl_rng *rng, enum categorical_test_type type)
{
    if (phen_ucnt > phen_cnt) return 0; // Wrong parameter

    double res = nan(__func__), *outer = NULL;
    size_t *phen_perm = malloc(phen_cnt * sizeof(*phen_perm)), *filter = NULL, *table = NULL, *phen_mar = malloc(phen_ucnt * sizeof(*phen_mar));
    uint8_t *phen_bits = malloc(UINT8_CNT(phen_ucnt) * sizeof(*phen_bits));
    struct {
        size_t gen_mar[GEN_MAX], gen_phen_mar, cnt, gen_pop_cnt;
        uint8_t gen_bits[UINT8_CNT(GEN_MAX)];
    } *snp_data = NULL;
    
    if ((phen_ucnt && !phen_mar && !phen_bits) ||
        (phen_cnt && !phen_perm) ||
        !array_init(&snp_data, NULL, snp_cnt, sizeof(*snp_data), 0, ARRAY_CLEAR | ARRAY_STRICT) ||
        !array_init(&filter, NULL, snp_cnt * phen_cnt, sizeof(*filter), 0, ARRAY_STRICT) || // Result of 'snp_cnt * phen_cnt' is assumed not to be wrapped due to the validness of the 'gen' array
        !array_init(&outer, NULL, phen_ucnt, GEN_MAX * sizeof(*outer), 0, ARRAY_STRICT) ||
        !array_init(&table, NULL, phen_ucnt, GEN_MAX * sizeof(*table), 0, ARRAY_STRICT)) goto error;

    //  Initialization
    gen_pop_cnt_callback gen_pop_cnt_impl = get_gen_pop_cnt(type);
    gen_shuffle_callback gen_shuffle_impl = get_gen_shuffle(type);
    
    double density = 0.;
    size_t density_cnt = 0;

    for (size_t j = 0, off = 0; j < snp_cnt; j++, off += phen_cnt)
    {
        // Initializing genotype filter
        size_t cnt = 0;
        for (size_t i = 0; i < phen_cnt; i++) if (gen[i + off] < GEN_MAX) filter[cnt++ + off] = i;

        if (!cnt) continue; // There is nothing to do if the count is zero
        snp_data[j].cnt = cnt;

        // Counting unique genotypes
        size_t gen_pop_cnt = gen_bits_init(snp_data[j].gen_bits, cnt, GEN_MAX, filter + off, gen + off);
        gen_pop_cnt = gen_pop_cnt_impl(snp_data[j].gen_bits, gen_pop_cnt);
        if (gen_pop_cnt < 2) continue;
        snp_data[j].gen_pop_cnt = gen_pop_cnt;
        
        // Counting unique phenotypes
        memset(phen_bits, 0, UINT8_CNT(phen_ucnt));
        size_t phen_pop_cnt = phen_bits_init(phen_bits, cnt, phen_ucnt, filter + off, phen);
        if (phen_pop_cnt < 2) continue;

        // Building contingency table
        memset(table, 0, GEN_MAX * phen_ucnt * sizeof(*table));
        for (size_t i = 0; i < cnt; i++)
        {
            size_t ind = filter[i + off];
            table[gen[ind + off] + GEN_MAX * phen[ind]]++;
        }
        table_shuffle(table, snp_data[j].gen_bits, gen_pop_cnt, phen_bits, phen_pop_cnt, gen_shuffle_impl);

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
            memset(table, 0, GEN_MAX * phen_ucnt * sizeof(*table));
            for (size_t i = 0; i < cnt; i++)
            {
                size_t ind = filter[i + off];
                table[gen[ind + off] + GEN_MAX * phen_perm[ind]]++;
            }
            table_shuffle(table, snp_data[j].gen_bits, gen_pop_cnt, phen_bits, phen_pop_cnt, gen_shuffle_impl);

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
    free(phen_perm);
    free(phen_mar);
    free(phen_bits);
    free(snp_data);
    free(filter);        
    free(outer);
    free(table);
    return res;
}

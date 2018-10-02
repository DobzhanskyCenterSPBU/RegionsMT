#pragma once

#include "common.h"

#include <gsl/gsl_rng.h>

#define ALT_CNT 4

enum categorical_flags {
    TEST_TYPE_CODOMINANT = 1,
    TEST_TYPE_RECESSIVE = 2,
    TEST_TYPE_DOMINANT = 4,
    TEST_TYPE_ALLELIC = 8
};

struct categorical_supp {
    uint8_t *phen_bits;
    size_t *filter, *table, *phen_mar;
    double *outer;
    double nlpv[ALT_CNT], qas[ALT_CNT];
};

struct maver_adj_supp {
    uint8_t *phen_bits;
    size_t *filter, *table, *phen_mar, *phen_perm;
    double *outer;
    struct categorical_snp_data *snp_data;
    double nlpv[ALT_CNT];
    size_t rpl[ALT_CNT];
};

bool categorical_init(struct categorical_supp *, size_t, size_t);
void categorical_impl(struct categorical_supp *, uint8_t *, size_t *, size_t, size_t, size_t, double, enum categorical_flags);
void categorical_close(struct categorical_supp *);

bool maver_adj_init(struct maver_adj_supp *, size_t, size_t, size_t);
void maver_adj_impl(struct maver_adj_supp *, uint8_t *, size_t *, size_t, size_t, size_t, size_t *, size_t, gsl_rng *, enum categorical_flags);
void maver_adj_close(struct maver_adj_supp *);

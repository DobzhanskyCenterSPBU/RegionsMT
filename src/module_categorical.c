#include "memory.h"
#include "tblproc.h"
#include "categorical.h"

#include "module_categorical.h"

DECLARE_PATH

bool tbl_phen_selector_eol(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *Context)
{
    if (!array_test(tbl, (size_t *) Context, 1, 0, 0, ARG_SIZE(row, 1))) return 0;
    *cl = (struct tbl_col) { .handler = { .read = uint8_handler }, .ptr = *(uint8_t **) tbl + row };
    return 1;
}

bool tbl_phen_selector(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *Context)
{
    cl->handler.read = NULL;
    return 1;
}

struct gen_context {
    size_t gen_cap, gen_cnt, phen_cnt;
};

bool tbl_gen_selector_eol(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *Context)
{
    struct gen_context *context = Context;
    if (col > context->phen_cnt)
    {
        cl->handler.read = NULL;
        return 1;
    }
    if (!array_test(tbl, &context->gen_cap, 1, 0, 0, ARG_SIZE(context->gen_cnt, 1))) return 0;
    *cl = (struct tbl_col) { .handler = { .read = uint8_handler }, .ptr = *(uint8_t **) tbl + context->gen_cnt++ };
    return 1;
}

bool tbl_gen_selector(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *Context)
{
    if (col) return tbl_gen_selector_eol(cl, row, col, tbl, Context);
    cl->handler.read = NULL;
    return 1;
}

static void gsl_error(const char *reason, const char * file, int line, int gsl_errno)
{
    return;
}

bool categorical_run(const char *path_phen, const char *path_gen, struct log *log)
{
    uint8_t *phen = NULL, *gen = NULL;
    size_t phen_cap = 0;

    size_t skip = 1, cnt = 0, length = 0;
    tbl_read(path_phen, tbl_phen_selector, tbl_phen_selector_eol, &phen_cap, &phen, &skip, &cnt, &length, ',', log);

    size_t phen_cnt = cnt;
    struct gen_context gen_context = { .phen_cnt = phen_cnt };

    skip = 1, cnt = 0, length = 0;
    tbl_read(path_gen, tbl_gen_selector, tbl_gen_selector_eol, &gen_context, &gen, &skip, &cnt, &length, ',', log);

    size_t snp_cnt = cnt;

    gsl_set_error_handler(gsl_error);

    gsl_rng *rng = gsl_rng_alloc(gsl_rng_taus);
    size_t rpl = 10000000;
    double x = maver_adj(gen, phen, snp_cnt, phen_cnt, &rpl, 10, 1. + 1.e-7, rng);
        
    log_message_var(log, &MESSAGE_VAR_GENERIC(MESSAGE_TYPE_NOTE), "Adjucted density: %f.\n", x);

    gsl_rng_free(rng);
    free(phen);
    free(gen);
    return 1;
}
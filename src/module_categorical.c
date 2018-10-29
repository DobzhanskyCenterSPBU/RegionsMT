#include "np.h"
#include "ll.h"
#include "memory.h"
#include "tblproc.h"
#include "categorical.h"
#include "sort.h"

#include "module_categorical.h"

#include <string.h>

DECLARE_PATH

struct phen_context {
    struct str_tbl_handler_context handler_context;
    size_t cap;
};

static bool tbl_phen_selector(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *Context)
{
    if (col != 2)
    {
        cl->handler.read = NULL;
        return 1;
    }
    struct phen_context *context = Context;
    if (!array_test(tbl, &context->cap, sizeof(ptrdiff_t), 0, 0, ARG_SIZE(row, 1))) return 0;
    *cl = (struct tbl_col) { .handler = { .read = str_tbl_handler }, .ptr = *(ptrdiff_t **) tbl + row, .context = &context->handler_context };
    return 1;
}

struct gen_context {
    size_t gen_cap, gen_cnt, phen_cnt;
};

static bool tbl_gen_selector(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *Context)
{
    struct gen_context *context = Context;
    if (!col || col > context->phen_cnt)
    {
        cl->handler.read = NULL;
        return 1;
    }
    if (!array_test(tbl, &context->gen_cap, 1, 0, 0, ARG_SIZE(context->gen_cnt, 1))) return 0;
    *cl = (struct tbl_col) { .handler = { .read = uint8_handler }, .ptr = *(uint8_t **) tbl + context->gen_cnt++ };
    return 1;
}

/*static void gsl_error_a(const char *reason, const char * file, int line, int gsl_errno)
{
    (void) reason;
    return;
}*/

bool append_out(const char *path_out, double val, size_t off, size_t cnt, struct log *log)
{
    bool succ = 0;
    FILE *f = fopen(path_out, "a");
    if (!f) log_message_fopen(log, CODE_METRIC, MESSAGE_ERROR, "", errno);
    int tmp = fprintf(f, "%.15f\n", val);
    if (tmp < 0) goto error;
    succ = 1;
error:
    Fclose(f);
    return 1;
}

bool categorical_run(const char *path_phen, const char *path_gen, const char *path_out, size_t rpl, struct log *log)
{
    uint8_t *gen = NULL;
    gsl_rng *rng = NULL;    
    size_t *phen = NULL;
    struct phen_context phen_context = { 0 };
    size_t phen_cap = 0, phen_skip = 1, phen_cnt = 0, phen_length = 0;
    if (!tbl_read(path_phen, 0, tbl_phen_selector, NULL, &phen_context, &phen, &phen_skip, &phen_cnt, &phen_length, ',', log)) goto error;

    uintptr_t *phen_ptr = pointers_stable(phen, phen_cnt, sizeof(*phen), str_off_stable_cmp, phen_context.handler_context.str);
    if (!phen_ptr) goto error;
    size_t phen_ucnt = phen_cnt;
    ranks_unique_from_pointers_impl(phen, phen_ptr, (uintptr_t) phen, &phen_ucnt, sizeof(*phen), str_off_cmp, phen_context.handler_context.str);
    free(phen_ptr);

    struct gen_context gen_context = { .phen_cnt = phen_cnt };
    size_t gen_skip = 1, snp_cnt = 0, gen_length = 0;
    if (!tbl_read(path_gen, 0, tbl_gen_selector, NULL, &gen_context, &gen, &gen_skip, &snp_cnt, &gen_length, ',', log)) goto error;

    rng = gsl_rng_alloc(gsl_rng_taus);
    if (!rng) goto error;
    //gsl_set_error_handler(gsl_error_a);

    uint64_t start = get_time();

    struct maver_adj_supp supp;
    maver_adj_init(&supp, snp_cnt, phen_cnt, phen_ucnt);

    struct maver_adj_res x = maver_adj_impl(&supp, gen, phen, snp_cnt, phen_cnt, phen_ucnt, rpl, 10, rng, 7);
    maver_adj_close(&supp);

    log_message_generic(log, CODE_METRIC, MESSAGE_INFO, "Results of the computation of adjusted P-value (model: -log10(adjusted P-value), count of replications):"
        "%s: %f, %zu; %s: %f, %zu; %s: %f, %zu; %s: %f, %zu\n",
        "CD", x.nlpv[0], x.rpl[0], "R", x.nlpv[1], x.rpl[1], "D", x.nlpv[2], x.rpl[2], "A", x.nlpv[3], x.rpl[3]);
    log_message_time_diff(log, CODE_METRIC, MESSAGE_INFO, start, get_time(), "Adjusted P-value computation took");
    if (path_out) append_out(path_out, x.nlpv[0], 0, snp_cnt, log);

error:
    gsl_rng_free(rng);
    free(phen_context.handler_context.str);
    free(phen);
    free(gen);
    return 1;
}
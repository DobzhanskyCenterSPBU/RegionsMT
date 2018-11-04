#include "np.h"
#include "ll.h"
#include "memory.h"
#include "tblproc.h"
#include "categorical.h"
#include "sort.h"

#include "module_categorical.h"

#include <string.h>
#include <inttypes.h>

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
    if (!array_test(tbl, &context->cap, sizeof(ptrdiff_t), 0, 0, row, 1)) return 0;
    *cl = (struct tbl_col) { .handler = { .read = str_tbl_handler }, .ptr = *(ptrdiff_t **) tbl + row, .context = &context->handler_context };
    return 1;
}

struct gen_context {
    size_t gen_cap, gen_cnt, phen_cnt;
};

static bool tbl_gen_selector(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *Context)
{
    (void) row;
    struct gen_context *context = Context;
    if (!col || col > context->phen_cnt)
    {
        cl->handler.read = NULL;
        return 1;
    }
    if (!array_test(tbl, &context->gen_cap, 1, 0, 0, context->gen_cnt, 1)) return 0;
    *cl = (struct tbl_col) { .handler = { .read = uint8_handler }, .ptr = *(uint8_t **) tbl + context->gen_cnt++ };
    return 1;
}

static bool gen_handler(const char *str, size_t len, void *res, void *Context)
{
    struct gen_context *context = Context;
    if (len != context->phen_cnt) return 0;
    for (size_t i = 0; i < len; i++) ((uint8_t *) res)[i]  = (uint8_t) str[i] - '0';
    return 1;
}

static bool tbl_gen_selector2(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *Context)
{
    (void) row;
    struct gen_context *context = Context;
    if (col != 1)
    {
        cl->handler.read = NULL;
        return 1;
    }
    if (!array_test(tbl, &context->gen_cap, 1, 0, 0, context->gen_cnt, context->phen_cnt)) return 0;
    *cl = (struct tbl_col) { .handler = { .read = gen_handler }, .ptr = *(uint8_t **) tbl + context->gen_cnt, .context = context };
    context->gen_cnt += context->phen_cnt;
    return 1;
}

struct interval { size_t left; size_t right; };

static bool tbl_top_hit_selector(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *p_Cap)
{
    if (col < 3)
    {
        cl->handler.read = NULL;
        return 1;
    }
    else if (col == 3)
    {
        if (!array_test(tbl, p_Cap, sizeof(struct interval), 0, 0, row, 1)) return 0;
        *cl = (struct tbl_col) { .handler = { .read = size_handler }, .ptr = &(*(struct interval **) tbl)[row].left };
    }
    else if (col == 4)
    {
        *cl = (struct tbl_col) { .handler = { .read = size_handler }, .ptr = &(*(struct interval **) tbl)[row].right };
    }
    return 1;
}

static void gsl_error_a(const char *reason, const char *file, int line, int gsl_errno)
{
    (void) reason;
    (void) file;
    (void) line;
    (void) gsl_errno;
    return;
}

bool append_out(const char *path_out, struct maver_adj_res res, struct log *log)
{
    bool succ = 1;
    FILE *f = fopen(path_out, "a");
    for (;;)
    {
        if (!f) log_message_fopen(log, CODE_METRIC, MESSAGE_ERROR, path_out, errno);
        else break;
        return 0;
    }
    int tmp = fprintf(f, "%.15f, %.15f, %.15f, %.15f, %zu, %zu, %zu, %zu\n", 
        res.nlpv[0], res.nlpv[1], res.nlpv[2], res.nlpv[3], res.rpl[0], res.rpl[1], res.rpl[2], res.rpl[3]);
    if (tmp < 0) succ = 0;
    Fclose(f);
    return succ;
}

bool categorical_run(const char *path_phen, const char *path_gen, const char *path_top_hit, const char *path_out, size_t rpl, uint64_t seed, struct log *log)
{
    uint8_t *gen = NULL;
    gsl_rng *rng = NULL;    
    size_t *phen = NULL;
    FILE *f = NULL;
    struct interval *top_hit = NULL;
    struct phen_context phen_context = { 0 };
    size_t phen_skip = 0, phen_cnt = 0, phen_length = 0;
    if (!tbl_read(path_phen, 0, tbl_phen_selector, NULL, &phen_context, &phen, &phen_skip, &phen_cnt, &phen_length, ',', log)) goto error;

    uintptr_t *phen_ptr = pointers_stable(phen, phen_cnt, sizeof(*phen), str_off_stable_cmp, phen_context.handler_context.str);
    if (!phen_ptr) goto error;
    size_t phen_ucnt = phen_cnt;
    ranks_unique_from_pointers_impl(phen, phen_ptr, (uintptr_t) phen, &phen_ucnt, sizeof(*phen), str_off_cmp, phen_context.handler_context.str);
    free(phen_ptr);

    struct gen_context gen_context = { .phen_cnt = phen_cnt };
    size_t gen_skip = 0, snp_cnt = 0, gen_length = 0;
    if (!tbl_read(path_gen, 0, tbl_gen_selector2, NULL, &gen_context, &gen, &gen_skip, &snp_cnt, &gen_length, ',', log)) goto error;

    size_t top_hit_cap = 0;
    size_t top_hit_skip = 0, top_hit_cnt = 0, top_hit_length = 0;
    if (!tbl_read(path_top_hit, 0, tbl_top_hit_selector, NULL, &top_hit_cap, &top_hit, &top_hit_skip, &top_hit_cnt, &top_hit_length, ',', log)) goto error;

    gsl_set_error_handler(gsl_error_a);
    rng = gsl_rng_alloc(gsl_rng_taus);
    if (!rng) goto error;
    gsl_rng_set(rng, (unsigned long) seed);

    struct maver_adj_supp supp;

    size_t wnd = 0;
    for (size_t i = 0; i < top_hit_cnt; i++)
    {
        size_t left = top_hit[i].left - 1, right = top_hit[i].right - 1;
        if (left > right || right >= snp_cnt)
            log_message_generic(log, CODE_METRIC, MESSAGE_WARNING, "Wrong interval: %zu:%zu!\n", left, right);
        else
        {
            size_t tmp = right - left + 1;
            if (wnd < tmp) wnd = tmp;
        }
    }

    f = fopen(path_out, "w");
    for (;;)
    {
        if (!f) log_message_fopen(log, CODE_METRIC, MESSAGE_ERROR, path_out, errno);
        else break;
        goto error;
    }

    if (!maver_adj_init(&supp, wnd, phen_cnt, phen_ucnt)) goto error;

    for (size_t i = 0; i < top_hit_cnt; i++)
    {
        size_t left = top_hit[i].left - 1, right = top_hit[i].right - 1;
        if (left > right || right >= snp_cnt) continue;

        uint64_t t0 = get_time();
        struct maver_adj_res x = maver_adj_impl(&supp, gen + left * phen_cnt, phen, right - left + 1, phen_cnt, phen_ucnt, rpl, 10, rng, 15);
        log_message_generic(log, CODE_METRIC, MESSAGE_INFO, "Adjusted P-value for window %zu:%zu no. %zu: "
            "[%s] %f, %zu; [%s] %f, %zu; [%s] %f, %zu; [%s] %f, %zu.\n",
            left + 1, right + 1, i + 1,
            "CD", x.nlpv[0], x.rpl[0], "R", x.nlpv[1], x.rpl[1], "D", x.nlpv[2], x.rpl[2], "A", x.nlpv[3], x.rpl[3]);
        uint64_t t1 = get_time();
        log_message_time_diff(log, CODE_METRIC, MESSAGE_INFO, t0, t1, "Adjusted P-value computation took ");

        int64_t diff = t1 - t0, mdq = diff / 60000000, mdr = diff % 60000000;
        double sec = 1.e-6 * (double) mdr;
        fprintf(f, "%zu,%.15e,%zu,%.15e,%zu,%.15e,%zu,%.15e,%zu,%" PRId64 " min,%.6f sec\n",
            i + 1, x.nlpv[0], x.rpl[0], x.nlpv[1], x.rpl[1], x.nlpv[2], x.rpl[2], x.nlpv[3], x.rpl[3], mdq, sec);
        fflush(f);
    }
    
error:
    maver_adj_close(&supp);
    Fclose(f);
    gsl_rng_free(rng);
    free(top_hit);
    free(phen_context.handler_context.str);
    free(phen);
    free(gen);
    return 1;
}
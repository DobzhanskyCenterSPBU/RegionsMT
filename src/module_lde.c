#include "np.h"
#include "ll.h"
#include "lde.h"
#include "memory.h"
#include "tblproc.h"

#include "module_lde.h"

struct gen_context {
    size_t gen_cap, gen_cnt, phen_cnt;
};

static bool tbl_gen_selector(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *Context)
{
    struct gen_context *context = Context;
    if (!row) context->phen_cnt++;
    if (!col)
    {
        cl->handler.read = NULL;
        return 1;
    }
    if (!array_test(tbl, &context->gen_cap, 1, 0, 0, ARG_SIZE(context->gen_cnt, 1))) return 0;
    *cl = (struct tbl_col) { .handler = { .read = uint8_handler }, .ptr = *(uint8_t **) tbl + context->gen_cnt++ };
    return 1;
}

enum tbl_gen_col_type {
    TBL_GEN_COL_IND = 0,
    TBL_GEN_COL_GEN = 1,
};

struct tbl_gen_head_context {
    size_t cl_cnt, phen_cnt;
    size_t *cl_type, *gen;
    uint8_t *cl_bits, *gen_bits;
};

static bool tbl_gen_head_callback(const char *str, size_t len, void *dst, void *Context)
{
    size_t ind;
    struct tbl_gen_head_context *context = Context;
    if (size_handler(str, len, &ind, NULL))
    {
        // if (!array_test(&context->gen_bits, , sizeof(*context->gen_bits), 0, 0, ARG_SIZE(UINT8_CNT(ind)))) return 0;
        // if (uint8_bit_test_set(context->gen_bits, ind))
    }
}

static bool tbl_gen_head_selector(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *Context)
{
    
}

bool lde_run(const char *path_gen, const char *path_out, struct log *log)
{
    uint8_t *gen = NULL;
    struct gen_context gen_context = { 0 };
    size_t gen_skip = 1, snp_cnt = 0, gen_length = 0;
    if (!tbl_read(path_gen, 0, tbl_gen_selector, NULL, &gen_context, &gen, &gen_skip, &snp_cnt, &gen_length, ',', log)) goto error;
    
    FILE *f = fopen(path_out, "w");
    if (!f) goto error;

    for (size_t i = 0; i < size_sub_sat(gen_context.gen_cnt, 30); i++)
    {
        for (size_t j = 0; j < 30; j++)
        {
            double lde = lde_impl(gen + gen_context.phen_cnt * i, gen + gen_context.phen_cnt * (i + j), gen_context.phen_cnt);
            //unsigned res = -lde > .9;
            if (j) fprintf(f ,"%zu,%zu,%.15f\n%zu,%zu,%.15f\n", i, i + j, lde, i + j, i, lde);
            else fprintf(f, "%zu,%zu,%.15f\n", i, i, lde);
        }
    }

    fclose(f);

error:
    free(gen);
    return 1;
}

#include "np.h"
#include "genotypes.h"
#include "tblproc.h"
#include "memory.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct
{
    char **strtbl;
    size_t *strtblcnt, *strtblcap;
} strTableHandlerContext;

bool strTableHandler(const char *str, size_t len, ptrdiff_t *offset, strTableHandlerContext *context)
{
    if (!len && *context->strtblcnt) // Last null-terminator is used to store zero-length strings
    {
        *offset = *context->strtblcnt - 1;
        return 1;
    }

    if (!array_test(context->strtbl, context->strtblcap, 1, 0, 0, ARG_SIZE(*context->strtblcnt, len, 1))) return 0;

    *offset = *context->strtblcnt;
    memcpy(*context->strtbl + *context->strtblcnt, str, len + 1);
    *context->strtblcnt += len + 1;

    return 1;
}

static const struct tbl_sch statSchFam = CLII((struct tbl_col[])
{
    { .handler = { .read = NULL } },
    { .handler = { .read = NULL } },
    { .handler = { .read = NULL } },
    { .handler = { .read = NULL } },
    { .handler = { .read = NULL } },
    { .handler = { .read = (read_callback) strTableHandler }, .offset = 0 } // phenotype
});

bool read_phenotypes(void *Res, void *Context)
{
    struct phenotype *res = Res;
    struct phenotype_context *context = Context;
    
    FILE *f = fopen(context->path, "rb");
    // if (!f) ...

    size_t sz = file_get_size(f);
    size_t row_cnt = row_count(f, 0, sz);
    ptrdiff_t *tbl = malloc(row_cnt * sizeof(ptrdiff_t));

    char *str = NULL;
    size_t strtblcnt = 0, strtblcap = 0;
    void *cont[6] = { [0] = &(strTableHandlerContext) { .strtbl = &str,.strtblcnt = &strtblcnt, .strtblcap = &strtblcap } };

    Fseeki64(f, 0, SEEK_SET);
    bool r = row_read(f, &statSchFam, tbl, NULL, NULL, NULL, NULL, ' ', 0, 0);

    res->name_off = tbl[0];
    res->cnt = row_cnt;
    res->name = str;
    res->name_sz = strtblcnt;
        
    return 1;
}

union gen {
    uint8_t bin;
    struct {
        uint8_t n0 : 2;
        uint8_t n1 : 2;
        uint8_t n2 : 2;
        uint8_t n3 : 2;
    };
};

bool read_genotypes(void *Res, void *Context)
{
    struct genotype *res = Res;
    struct genotype_context *context = Context;

    char remap[] = { 0, 3, 1, 2 };
    FILE *f = fopen(context->path, "rb");
    char buff[BLOCK_READ];
    size_t snp_ind = 0, phn_ind = 0;;

    size_t rd = fread(buff, sizeof(*buff), countof(buff), f), pos = 0;
    if (strncmp(buff, "\x6c\x1b\x01", 3)) return 0;
    pos = 3;

    res->gen = malloc(context->snp->cnt * (context->phenotype->cnt + 3) / 4);

    for (; rd; rd = fread(buff, sizeof(*buff), countof(buff), f), pos = 0)
    {
        for (size_t i = pos; i < rd; i++)
        {
            union gen gen_in = { .bin = buff[i] }, gen_out = { .n0 = remap[gen_in.n0],.n1 = remap[gen_in.n1],.n2 = remap[gen_in.n2],.n3 = remap[gen_in.n3] };
            res->gen[context->snp->cnt * (context->phenotype->cnt + 3) / 4 + phn_ind / 4] = gen_out.bin;
            phn_ind += 4;
            if (phn_ind >= context->phenotype->cnt) phn_ind = 0, snp_ind++;
            if (snp_ind == context->snp->cnt) return 0; // error msg
        }
    }

    return 1;
}
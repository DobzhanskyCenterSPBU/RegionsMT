#pragma once

#include "common.h"

struct snp {
    size_t *pos; // length = snp_cnt
    uint8_t *all; // length = (snp_cnt + 3) / 4
    ptrdiff_t *name_off; // length = snp_cnt
    struct {        
        char *name; // length = snp_name_sz
        size_t name_sz; 
    };
    size_t cnt, off;
};

struct phenotype {
    ptrdiff_t *name_off;
    struct {        
        char *name;
        size_t name_sz;        
    };
    size_t cnt, off;
};

struct genotype {
    uint8_t *gen; // length = (gen_cnt + 3) / 4 
    size_t cnt /* = snp_cnt * phn_cnt */;
};

struct snp_context {
    char *path;
};

struct phenotype_context {
    char *path;
};

struct genotype_context {
    struct snp *snp;
    struct phenotype *phenotype;
    char *path;
};

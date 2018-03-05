#pragma once

#include "Common.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    struct {
        // 'testname' -- offsets in the 'teststr' array
        size_t *testname;
        // 'testcnt' -- count of elements in the 'test' array
        size_t testcnt;
    };
    struct {
        // 'testnamestr', 'testnamestrsz' -- text array of test names and its size
        char *testnamestr;
        size_t testnamestrsz;
    };
    struct {
        // 'chrlen' -- count of SNP's in each of chromosome
        // 'chroff' -- sums of the starting elements of 'chrom' array (required by technical reasons)
        // 'chrname' -- offsets in the 'chrominfostr' array
        size_t *chrlen, *chroff, *chrname;
        
        // 'chromcnt' -- count of chromosomes
        size_t chrcnt;
    };
    struct {
        // 'chrnamestr', 'chrnamestrsz' -- text array of chromosome names and its size
        char *chrnamestr;
        size_t chrnamestrsz;
    };
    struct {
        // 'snpname' -- offsets in the 'snpnamestr' array
        // 'genename' -- offsets in the 'genenamestr' array
        // 'allelename' -- offsets in the 'allelestr' array
        size_t *snpname, *genename;

        // 'pos' -- SNP positions
        size_t *pos;

        // 'tmaf' -- total maf
        double *tmaf;
                        
        // 'snpcnt' -- equal to the sum of elements in 'chrom'
        size_t snpcnt; 
    };
    struct {
        // 'snpstr', 'snpstrsz' -- text array of SNP names and its size
        char *snpnamestr;
        size_t snpnamestrsz;
    };
    struct {
        // 'genestr', 'genestrsz' -- text array of gene names and its size
        char *genenamestr;
        size_t genenamestrsz;
    };
    struct {
        // 'nlpv' -- array of negated logarithms of P-values
        // 'qas' -- array of quantitative association statistics (QAS)
        // 'maf' -- array of minor allele frequencies (MAF)
        double *nlpv, *qas, *maf;

        // 'allele' -- allele configuration: first four bits -- common allele, last four bits -- minor allele 
        uint8_t *allele;
        
        // 'rlpv' -- ranks of P-values
        // 'rqas' -- ranks of QAS
        uintptr_t *rnlpv, *rqas;

        // 'pvcnt' -- number equal to the product of 'snpcnt' and 'testcnt'
        size_t pvcnt;
    };
} testData;

// A variant of the binary search used to find chromosome index
inline size_t findBound(size_t val, size_t *restrict arr, size_t cnt)
{
    size_t left = 0;
    while (left + 1 < cnt)
    {
        size_t mid = left + ((cnt - left) >> 1);
        if (val >= arr[mid]) left = mid;
        else cnt = mid;
    }

    return left;
}

void testDataClose(testData *);

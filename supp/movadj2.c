#include <R.h>
#include <Rmath.h>

#define USE_RINTERNALS
#include <Rinternals.h>

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))

#define REMAP(remap, cd) \
    switch ((cd)) \
    { \
    case 3: /* = 1 | 2 */ \
        (remap)[0] = 0; \
        (remap)[1] = 1; \
        (cd) = 2; \
        break; \
        \
    case 5: /* = 1 | 4 */ \
        (remap)[0] = 0; \
        (remap)[2] = 1; \
        (cd) = 2; \
        break; \
        \
    case 6: /* = 2 | 4 */ \
        (remap)[1] = 0; \
        (remap)[2] = 1; \
        (cd) = 2; \
        break; \
        \
    case 7: /* = 1 | 2 | 4 */ \
        (remap)[0] = 0; \
        (remap)[1] = 1; \
        (remap)[2] = 2; \
        (cd) = 3; \
        break; \
        \
    default: \
        continue; \
    }

/*
# Usage in R:
movadj2 <- function(genotype, pht, r.max = 10^7, k = 10L, wnd = 10) 
{ 
    storage.mode(genotype) <- "integer"
    .Call("_movadj2", as.matrix(genotype), as.integer(pht), as.numeric(r.max), as.integer(k), as.integer(wnd))
}
*/

SEXP _movadj2(SEXP genotype, SEXP phenotype, SEXP rmax, SEXP k, SEXP wnd)
{
    SEXP res, dim = getAttrib(genotype, R_DimSymbol);
    const unsigned nrow = INTEGER(dim)[0], ncol = INTEGER(dim)[1];
    const double relError = 1. + 1.e-7;
    
    PROTECT(res = allocVector(REALSXP, 1));
    
    unsigned table[9], sumy[3], remapy[3];
    double outer[9];
    
    unsigned *permut = NULL;
    unsigned *row_count = NULL, *row_tablesum = NULL, *row_filter = NULL, *row_sums = NULL;
    unsigned char *row_unique = NULL, *row_remap = NULL;
    double *row_density = NULL;
    
    if (nrow != LENGTH(phenotype)) goto error0; // Dimensions of genotype and phenotype are discordant        
    
    // Memory management
    permut = malloc((size_t) nrow * sizeof *permut);
    row_count = calloc((size_t) ncol, sizeof *row_count); // Filled with zeros!
    row_tablesum = malloc((size_t) ncol * sizeof *row_tablesum);
    row_filter = malloc((size_t) nrow * (size_t) ncol * sizeof *row_filter);
    row_sums = malloc(3 * (size_t) ncol * sizeof *row_sums);
    row_unique = calloc((size_t) ncol, sizeof *row_unique); // Filled with zeros!
    row_remap = malloc(3 * (size_t) ncol * sizeof *row_remap);
    row_density = malloc((size_t) ncol * sizeof *row_density);
    
    if (!(permut && row_count && row_tablesum && row_filter && row_sums && row_unique && row_remap && row_density)) goto error1; // Memory allocation failed
    
    ///////////////////////////////////////////////////////////////////////////
    //
    //  Initialization
    //
    
    unsigned density_count = 0;
    
    for (unsigned j = 0; j < ncol; j++)
    {
        const size_t nrowj = (size_t) nrow * (size_t) j;
        
        // Filtering genotypes
        unsigned selcount = 0;
        for (unsigned i = 0; i < nrow; i++) if (INTEGER(genotype)[i + nrowj] != NA_INTEGER && INTEGER(genotype)[i + nrowj] != 3) row_filter[selcount++ + nrowj] = i;
        
        if (!selcount) continue; // There is nothing to do if the count is zero
        row_count[j] = selcount;
                
        // Working with rows
        unsigned char cdx = 0;        
        unsigned char *const remapx = row_remap + 3 * j;
        for (unsigned i = 0; i < selcount && cdx != 7; cdx |= 1 << INTEGER(genotype)[row_filter[i++ + nrowj] + nrowj]);
        REMAP(remapx, cdx); // Remapping table row indeces
        row_unique[j] = cdx;
        
        // Working with columns
        unsigned char cdy = 0;        
        for (unsigned i = 0; i < selcount && cdy != 7; cdy |= 1 << INTEGER(phenotype)[row_filter[i++ + nrowj]]);
        REMAP(remapy, cdy); // Remapping table column indeces
        
        // Building contingency table
        memset(table, 0, cdx * cdy * sizeof table[0]);
        
        for (unsigned i = 0; i < selcount; i++)
        {
            unsigned ind = row_filter[i + nrowj];
            table[remapx[INTEGER(genotype)[ind + nrowj]] + cdx * remapy[INTEGER(phenotype)[ind]]]++;
        }
        
        // Computing sums
        unsigned *const sumx = row_sums + 3 * j;
        
        memset(sumx, 0, cdx * sizeof sumx[0]);
        memset(sumy, 0, cdy * sizeof sumy[0]);
        row_tablesum[j] = 0;
        
        for (unsigned char t = 0; t < cdy; row_tablesum[j] += sumy[t], t++) for (unsigned char s = 0; s < cdx; s++)
        {
            unsigned el = table[s + cdx * t];
            sumx[s] += el;
            sumy[t] += el;
        }
                
        // Computing outer product
        double outer_min = DBL_MAX;
        
        for (unsigned char t = 0; t < cdy; t++) for (unsigned char s = 0; s < cdx; s++)
        {
            unsigned char ind = s + cdx * t;
            outer[ind] = (double) sumx[s] * (double) sumy[t] / (double) row_tablesum[j];
            if (outer[ind] < outer_min) outer_min = outer[ind];
        }
        
        // Computing p-values
        if (outer_min <= 4. && cdx == 2 && cdy == 2)
        {
            const unsigned lo = max(0, (signed) sumx[0] - (signed) sumy[1]);
            const unsigned hi = min(sumx[0], sumy[0]);
            
            const double hyp_comp = Rf_dhyper(table[0], sumy[0], sumy[1], sumx[0], 0);
            double hyp_sum = 0., hyp_less = 0.;
            
            for (unsigned ind = lo; ind <= hi; ind++)
            {
                double hyp = Rf_dhyper(ind, sumy[0], sumy[1], sumx[0], 0);
                hyp_sum += hyp;
                if (hyp <= hyp_comp * relError) hyp_less += hyp;
            }
            
            row_density[density_count] = log(hyp_sum) - log(hyp_less);
        }
        else
        {
            double stat = 0.;
            
            for (unsigned char ind = 0; ind < cdx * cdy; ind++)
            {
                double diff = outer[ind] - (double) table[ind];
                stat += diff * diff / outer[ind];
            }
            
            row_density[density_count] = -Rf_pchisq(stat, (cdx - 1) * (cdy - 1), 0, 1); // Upper tail and logarithm are enabled
        }
        
        density_count++;
    }
    
    double density, tempd = 0.;
    const unsigned  newwnd = min(INTEGER(wnd)[0], density_count);
    
    for (unsigned k = 0; k < newwnd; tempd += row_density[k++]);
    density = tempd;
    
    for (unsigned k = 0; k < density_count - newwnd; k++)
    {
        tempd += row_density[k + newwnd] - row_density[k];
        if (tempd > density) density = tempd;
    }
    
    density /= newwnd;
    
    ///////////////////////////////////////////////////////////////////////////
    //
    //  Simulations
    //
    
    const size_t rbnd = (size_t) REAL(rmax)[0], qcbnd = INTEGER(k)[0];
    const _Bool fixed = qcbnd == NA_INTEGER; // Entering 'fixed' mode if 'k' is 'NA'
    
    size_t rpl = 0, qc = 0;
    
    for (; rpl < rbnd && (fixed || qc <= qcbnd); rpl++)
    {
        // Generating random permutation
        for (unsigned i = 0; i < nrow; permut[i] = i, i++); // Starting with identity permutation
        
        for (unsigned i = 0; i < nrow; i++) // Performing 'Knuth shuffle'
        {
            unsigned k = i + (unsigned) floor(unif_rand() * (nrow - i)), tmp = permut[k];
            permut[k] = permut[i];
            permut[i] = tmp;
        }
        
        // Density computation
        unsigned density_count = 0;
        
        for (unsigned j = 0; j < ncol; j++) if (row_count[j] && row_unique[j])
        {
            const size_t nrowj = (size_t) nrow * (size_t) j;
            const unsigned selcount = row_count[j];
            const unsigned char cdx = row_unique[j];
            unsigned char *const remapx = row_remap + 3 * j;
            
            // Working with columns
            unsigned char cdy = 0;
            for (unsigned i = 0; i < selcount && cdy != 7; cdy |= 1 << INTEGER(phenotype)[permut[row_filter[i++ + nrowj]]]);
            REMAP(remapy, cdy); // Remapping table column indeces
                        
            // Building contingency table
            memset(table, 0, cdx * cdy * sizeof table[0]);
            
            for (unsigned i = 0; i < selcount; i++)
            {
                unsigned ind = row_filter[i + nrowj];
                table[remapx[INTEGER(genotype)[ind + nrowj]] + cdx * remapy[INTEGER(phenotype)[permut[ind]]]]++;
            }
            
            // Computing sums
            unsigned *const sumx = row_sums + 3 * j; // Row sums are already computed
            
            memset(sumy, 0, cdy * sizeof sumy[0]);
            for (unsigned char t = 0; t < cdy; t++) for (unsigned char s = 0; s < cdx; sumy[t] += table[s + cdx * t], s++);
            
            // Computing outer product
            double outer_min = DBL_MAX;
            
            for (unsigned char t = 0; t < cdy; t++) for (unsigned char s = 0; s < cdx; s++)
            {
                unsigned char ind = s + cdx * t;
                outer[ind] = (double) sumx[s] * (double) sumy[t] / (double) row_tablesum[j];
                if (outer[ind] < outer_min) outer_min = outer[ind];
            }
            
            // Computing p-values
            if (outer_min <= 4. && cdx == 2 && cdy == 2)
            {
                const unsigned lo = max(0, (signed) sumx[0] - (signed) sumy[1]);
                const unsigned hi = min(sumx[0], sumy[0]);
                
                const double hyp_comp = Rf_dhyper(table[0], sumy[0], sumy[1], sumx[0], 0);
                double hyp_sum = 0., hyp_less = 0.;
                
                for (unsigned ind = lo; ind <= hi; ind++)
                {
                    double hyp = Rf_dhyper(ind, sumy[0], sumy[1], sumx[0], 0);
                    hyp_sum += hyp;
                    if (hyp <= hyp_comp * relError) hyp_less += hyp;
                }
                
                row_density[density_count] = log(hyp_sum) - log(hyp_less);
            }
            else
            {
                double stat = 0.;
                
                for (unsigned char ind = 0; ind < cdx * cdy; ind++)
                {
                    double diff = outer[ind] - (double) table[ind];
                    stat += diff * diff / outer[ind];
                }
                
                row_density[density_count] = -Rf_pchisq(stat, (cdx - 1) * (cdy - 1), 0, 1); // Upper tail and logarithm are enabled
            }
            
            density_count++;
        }
        
        double density_, tempd = 0.;
        const unsigned  newwnd = min(INTEGER(wnd)[0], density_count);
        
        for (unsigned k = 0; k < newwnd; tempd += row_density[k++]);
        density_ = tempd;
        
        for (unsigned k = 0; k < density_count - newwnd; k++)
        {
            tempd += row_density[k + newwnd] - row_density[k];
            if (tempd > density_) density_ = tempd;
        }
        
        density_ /= newwnd;
        if (density_ > density) qc++;
    }
    
    REAL(res)[0] = (double) qc / (double) rpl;
    if (!fixed && rpl == rbnd) REAL(res)[0] *= -1;
    
epilogue:
    // Memory should be cleared after all...
    free(permut);
    free(row_count);
    free(row_tablesum);
    free(row_filter);
    free(row_sums);
    
    free(row_unique);
    free(row_remap);
    free(row_density);
    
    UNPROTECT(1); 
    return res;
    
    // Error handling
    for (;;)
    {
    error0:
        Rf_warning("Function '%s' exited with an error: dimensions of genotype and phenotype are discordant!\n", __FUNCTION__);
        break;
    
    error1:
        Rf_warning("Function '%s' exited with an error: memory allocation failed!\n" , __FUNCTION__);
        break;
    }
    
    REAL(res)[0] = NA_REAL;
    goto epilogue;
}
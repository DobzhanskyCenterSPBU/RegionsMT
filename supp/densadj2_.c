#include <R.h>
#include <Rmath.h>

#define USE_RINTERNALS
#include <Rinternals.h>

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))

/*
# Usage in R:
densadj <- function(genotype, pht, r.max = 10^7, k = 10L) 
{ 
    storage.mode(genotype) <- "integer"
    .Call("_densadj", as.matrix(genotype), as.integer(pht), as.numeric(r.max), as.integer(k))
}
*/

SEXP _densadj2(SEXP genotype, SEXP phenotype, SEXP rmax, SEXP k)
{
    SEXP res, dim = getAttrib(genotype, R_DimSymbol);
    const unsigned nrow = INTEGER(dim)[0], ncol = INTEGER(dim)[1];
    const double relError = 1. + 1.e-7;
    
    PROTECT(res = allocVector(REALSXP, 1));
    
    unsigned table[6], sumx[2], sumy[3], remapy[3];
    double outer[6];
    
    unsigned *permut = NULL;
    unsigned *row_count = NULL, *row_tablesum = NULL, *row_filter = NULL, *row_sums = NULL;
    
    if (nrow != LENGTH(phenotype)) goto error0; // Dimensions of genotype and phenotype are discordant        
    
    // Memory management ('table' and 'outer' are initialized elsewhere)
    permut = malloc(nrow * sizeof *permut);
    row_count = calloc(ncol, sizeof *row_count); // Filled with zeros!
    row_tablesum = malloc(ncol * sizeof *row_tablesum);
    row_filter = malloc(nrow * ncol * sizeof *row_filter);
    row_sums = malloc(2 * ncol * sizeof *row_sums);
    
    if (!(permut && row_count && row_tablesum && row_filter && row_sums)) goto error1; // Memory allocation failed
    
    ///////////////////////////////////////////////////////////////////////////
    //
    //  Initialization
    //
    
    double density = 0.;
    size_t density_count = 0;
    
    for (unsigned j = 0; j < ncol; j++)
    {
        const size_t nrowj = (size_t) nrow * (size_t) j;
        
        // Filtering genotypes
        unsigned selcount = 0;
        for (unsigned i = 0; i < nrow; i++) if (INTEGER(genotype)[i + nrowj] != NA_INTEGER && INTEGER(genotype)[i + nrowj] != 3) row_filter[selcount++ + nrowj] = i;
        if (!selcount) continue; // There is nothing to do if the count is zero
        
        // Building contingency table
        unsigned cd = 0;
        
        for (unsigned i = 0; i < selcount && cd != 3; cd |= 1 << INTEGER(genotype)[row_filter[i++ + nrowj] + nrowj]);
        if (cd != 3) continue;
        
        row_count[j] = selcount; // Saving correct indeces count here
        
        for (unsigned i = 0; i < selcount && cd != 7; cd |= 1 << INTEGER(phenotype)[row_filter[i++ + nrowj]]);
        
        switch (cd) // Remapping table column indeces
        {
        case 3: // = 1 | 2
            remapy[0] = 0;
            remapy[1] = 1;
            cd = 2;
            break;
        
        case 5: // = 1 | 4
            remapy[0] = 0;
            remapy[2] = 1;
            cd = 2;
            break;
            
        case 6: // = 2 | 4
            remapy[1] = 0;
            remapy[2] = 1;
            cd = 2;
            break;
        
        case 7: // = 1 | 2 | 4
            remapy[0] = 0;
            remapy[1] = 1;
            remapy[2] = 2;
            cd = 3;
            break;
        
        default:
            continue;
        }
        
        memset(table, 0, sizeof table);
        
        for (unsigned i = 0; i < selcount; i++)
        {
            unsigned ind = row_filter[i + nrowj];
            table[INTEGER(genotype)[ind + nrowj] + 2 * remapy[INTEGER(phenotype)[ind]]]++;
        }
        
        // Computing sums
        sumx[0] = table[0] + table[2] + table[4]; // Row sums
        sumx[1] = table[1] + table[3] + table[5];
        
        row_tablesum[j] = sumx[0] + sumx[1]; // Table sum
        
        sumy[0] = table[0] + table[1]; // Col sums
        sumy[1] = table[2] + table[3];
        sumy[2] = table[4] + table[5];
        
        // Saving common data
        memcpy(row_sums + 2 * j, sumx, sizeof sumx); // Saving row sums
        
        // Computing outer product
        double outer_min = DBL_MAX;
        
        for (unsigned t = 0; t < cd; t++)
        {
            unsigned t2 = 2 * t;
            
            outer[t2] = (double) sumx[0] * (double) sumy[t] / (double) row_tablesum[j];
            if (outer[t2] < outer_min) outer_min = outer[t2];
            
            outer[1 + t2] = (double) sumx[1] * (double) sumy[t] / (double) row_tablesum[j];
            if (outer[1 + t2] < outer_min) outer_min = outer[1 + t2];
        }
        
        // Computing p-values
        if (outer_min <= 4. && cd == 2)
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
            
            density += log(hyp_sum) - log(hyp_less);
        }
        else
        {
            double stat = 0.;
            
            for (unsigned ind = 0; ind < 2 * cd; ind++)
            {
                double diff = fabs(outer[ind] - (double) table[ind]);
                stat += diff * diff / outer[ind];
            }
            
            density -= Rf_pchisq(stat, cd - 1, 0, 1); // Upper tail and logarithm are enabled
        }
        
        density_count++;
    }
    
    density /= density_count;
    
    ///////////////////////////////////////////////////////////////////////////
    //
    //  Simulations
    //
    
    const size_t rbnd = (size_t) REAL(rmax)[0];
    size_t rpl = 0, qc = 0;
    
    for (; rpl < rbnd && qc <= INTEGER(k)[0]; rpl++)
    {
        // Generating random permutation
        for (unsigned i = 0; i < nrow; permut[i] = i, i++); // Starting with identity permutation
        
        for (unsigned i = 0; i < nrow; i++) // Performing Knuth shuffle
        {
            unsigned k = i + (unsigned) floor(unif_rand() * (nrow - i)), tmp = permut[k];
            permut[k] = permut[i];
            permut[i] = tmp;
        }
        
        // Density computation
        double density_ = 0.;
        size_t density_count_ = 0;
        
        for (unsigned j = 0; j < ncol; j++) if (row_count[j])
        {
            const size_t nrowj = (size_t) nrow * (size_t) j;
            const unsigned selcount_ = row_count[j];
            
            // Building contingency table
            unsigned cd = 0;
            
            for (unsigned i = 0; i < selcount_ && cd != 7; cd |= 1 << INTEGER(phenotype)[permut[row_filter[i++ + nrowj]]]);
            
            switch (cd) // Remapping table column indeces
            {
            case 3: // = 1 | 2
                remapy[0] = 0;
                remapy[1] = 1;
                cd = 2;
                break;
                
            case 5: // = 1 | 4
                remapy[0] = 0;
                remapy[2] = 1;
                cd = 2;
                break;
                
            case 6: // = 2 | 4
                remapy[1] = 0;
                remapy[2] = 1;
                cd = 2;
                break;
                
            case 7: // = 1 | 2 | 4
                remapy[0] = 0;
                remapy[1] = 1;
                remapy[2] = 2;
                cd = 3;
                break;
                
            default:
                continue;
            }
            
            memset(table, 0, sizeof table);
            
            for (unsigned i = 0; i < selcount_; i++)
            {
                unsigned ind = row_filter[i + nrowj];
                table[INTEGER(genotype)[ind + nrowj] + 2 * remapy[INTEGER(phenotype)[permut[ind]]]]++;
            }
            
            // Computing sums
            unsigned *const sumx_ = row_sums + 2 * j; // Row sums
            
            sumy[0] = table[0] + table[1]; // Col sums
            sumy[1] = table[2] + table[3];
            sumy[2] = table[4] + table[5];
            
            // Computing outer product
            double outer_min = DBL_MAX;
            
            for (unsigned t = 0; t < cd; t++)
            {
                unsigned t2 = 2 * t;
                
                outer[t2] = (double) sumx_[0] * (double) sumy[t] / (double) row_tablesum[j];
                if (outer[t2] < outer_min) outer_min = outer[t2];
                
                outer[1 + t2] = (double) sumx_[1] * (double) sumy[t] / (double) row_tablesum[j];
                if (outer[1 + t2] < outer_min) outer_min = outer[1 + t2];
            }
            
            // Computing p-values
            if (outer_min <= 4. && cd == 2)
            {
                const unsigned lo = max(0, (signed) sumx_[0] - (signed) sumy[1]);
                const unsigned hi = min(sumx_[0], sumy[0]);
                
                const double hyp_comp = Rf_dhyper(table[0], sumy[0], sumy[1], sumx_[0], 0);
                double hyp_sum = 0., hyp_less = 0.;
                
                for (unsigned ind = lo; ind <= hi; ind++)
                {
                    double hyp = Rf_dhyper(ind, sumy[0], sumy[1], sumx_[0], 0);
                    hyp_sum += hyp;
                    if (hyp <= hyp_comp * relError) hyp_less += hyp;
                }
                 
                density_ += log(hyp_sum) - log(hyp_less);
            }
            else
            {
                double stat = 0.;
                
                for (unsigned ind = 0; ind < 2 * cd; ind++)
                {
                    double diff = fabs(outer[ind] - (double) table[ind]);
                    stat += diff * diff / outer[ind];
                }
                
                density_ -= Rf_pchisq(stat, cd - 1, 0, 1); // Upper tail and logarithm are enabled
            }
            
            density_count_++;
        }
        
        density_ /= density_count_;
        if (density_ > density) qc++;
    }
    
    REAL(res)[0] = (double) qc / (double) rpl;
    if (rpl == rbnd) REAL(res)[0] *= -1;
    
epilogue:
    // Memory should be cleared after all...
    free(permut);
    free(row_count);
    free(row_tablesum);
    free(row_filter);
    free(row_sums);
        
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
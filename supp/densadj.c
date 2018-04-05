#include <R.h>
#include <Rmath.h>

#define USE_RINTERNALS
#include <Rinternals.h>

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))

int _comp_unsigned(const void *a, const void *b) 
{ 
    if (*(unsigned *) a > *(unsigned *) b) return 1;
    if (*(unsigned *) a < *(unsigned *) b) return -1;
    return 0;
}

int _comp_unsignedptr(const void *a, const void *b) 
{ 
    if (**(unsigned **) a > **(unsigned **) b) return 1;
    if (**(unsigned **) a < **(unsigned **) b) return -1;
    return 0;
}

// '*pporder' is a temprorary array with pointers to the base array 'ar'
unsigned _getRanksUnique(unsigned *ar, size_t count, unsigned *presult, unsigned **pporder)
{
    unsigned unique = 0;
    
    for (size_t i = 0; i < count; pporder[i] = ar + i, i++);
    qsort(pporder, count, sizeof *pporder, &_comp_unsignedptr);

    if (count) presult[pporder[0] - ar] = 0; 
    else return 0;
    
    for (size_t i = 1; i < count; i++)
    {
        if ((*_comp_unsigned)(pporder[i - 1], pporder[i])) ++unique;
        presult[pporder[i] - ar] = unique;
    }

    return unique + 1;
}

/*
# Usage in R:
densadj <- function(genotype, pht, r.max = 10^7, k = 10L) 
{ 
    storage.mode(genotype) <- "integer"
    .Call("_densadj", as.matrix(genotype), as.integer(pht), as.numeric(r.max), as.integer(k))
}
*/

SEXP _densadj(SEXP genotype, SEXP pht, SEXP rmax, SEXP k)
{
    SEXP res, dim = getAttrib(genotype, R_DimSymbol);
    const unsigned nrow = INTEGER(dim)[0], ncol = INTEGER(dim)[1];
    const double relError = 1. + 1.e-7;
    
    PROTECT(res = allocVector(REALSXP, 1));
    
    unsigned *selx = NULL, *sely = NULL, *rankx = NULL, *ranky = NULL, *table = NULL, **temp = NULL;
    unsigned *row_count = NULL, *row_unique = NULL, *row_tablesum = NULL, *row_filter = NULL, *row_ranks = NULL, *row_sums = NULL;
    double *outer = NULL;
    
    double density = 0.;
    size_t table_size = 0, density_count = 0;
        
    if (nrow != LENGTH(pht)) goto error0; // Dimensions of genotype and phenotype are discordant        
    
    // Memory management ('table' and 'outer' are initialized elsewhere)
    selx = malloc(nrow * sizeof *selx);
    sely = malloc(nrow * sizeof *sely);
    rankx = malloc(nrow * sizeof *rankx);
    ranky = malloc(nrow * sizeof *ranky);
    temp = malloc(nrow * sizeof *temp);
    
    row_count = malloc(ncol * sizeof *row_count);
    row_unique = malloc(ncol * sizeof *row_unique);
    row_tablesum = malloc(ncol * sizeof *row_tablesum);
    row_filter = malloc(nrow * ncol * sizeof *row_filter);
    row_ranks = malloc(nrow * ncol * sizeof *row_ranks);
    row_sums = malloc(nrow * ncol * sizeof *row_sums);
    
    if (!(selx && sely && rankx && ranky && temp && row_count && row_unique && row_tablesum && row_filter && row_ranks && row_sums)) goto error1; // Memory allocation failed
    
    ///////////////////////////////////////////////////////////////////////////
    //
    //  Initialization
    //
    
    for (unsigned j = 0; j < ncol; j++)
    {
        const size_t nrowj = (size_t) nrow * (size_t) j;
        
        size_t test_size;
        unsigned selxu, selyu, tablesum = 0, selcount = 0;
        double outer_min = DBL_MAX;
        
        // Filtering genotypes
        for (unsigned i = 0; i < nrow; i++)
        {
            unsigned gt = INTEGER(genotype)[i + nrowj];
            
            if (gt != NA_INTEGER && gt != 3)
            {
                row_filter[selcount + nrowj] = i; // Saving correct index
                selx[selcount] = gt;
                sely[selcount] = INTEGER(pht)[i];
                selcount++;
            }
        }
        
        row_count[j] = selcount; // Saving correct indeces count        
        if (!selcount) continue; // There is nothing to do if the count is zero
        
        // Building contingency table        
        row_unique[j] = selxu = _getRanksUnique(selx, selcount, rankx, temp); 
        if (selxu < 2) continue; // Nothing to do!
        selyu = _getRanksUnique(sely, selcount, ranky, temp);
        if (selyu < 2) continue; // Nothing to do!
        
        if ((test_size = (size_t) selxu * (size_t) selyu) > table_size) // Allocating memory space for the contingency table if needed...
        {
            free(table);
            free(outer);
            table_size = test_size;
            table = calloc(table_size, sizeof *table); // Table should be filled with zeros
            outer = malloc(table_size * sizeof *outer);
            if (!(table && outer)) goto error1; // Memory allocation failed 
        }
        else memset(table, 0, test_size * sizeof *table); // ...or just cleansing the existing memory
        
        for (unsigned i = 0; i < selcount; table[rankx[i] + selxu * ranky[i]]++, i++); // Initializing the table
        
        // Computing sums
        memset(selx, 0, selxu * sizeof *selx); // We will use 'selx' array for row sums
        memset(sely, 0, selyu * sizeof *sely); // We will use 'sely' array for col sums
        
        for (unsigned t = 0; t < selyu; tablesum += sely[t++]) for (unsigned s = 0; s < selxu; s++)
        {
            unsigned el = table[(size_t) s + (size_t) selxu * (size_t) t];
            selx[s] += el; // Row sums
            sely[t] += el; // Col sums
        }
        
        // Saving common data
        row_tablesum[j] = tablesum; // Saving table sum
        memcpy(row_ranks + nrowj, rankx, nrow * sizeof *rankx); // Saving row ranks
        memcpy(row_sums + nrowj, selx, selxu * sizeof *selx); // Saving row sums
        
        // Computing outer product
        for (unsigned s = 0; s < selxu; s++) for (unsigned t = 0; t < selyu; t++)
        {
            size_t ind = (size_t) s + (size_t) selxu * (size_t) t;            
            outer[ind] = (double) selx[s] * (double) sely[t] / (double) tablesum;
            if (outer[ind] < outer_min) outer_min = outer[ind];
        }
        
        // Computing p-values
        if (outer_min <= 4. && selxu == 2 && selyu == 2)
        {
            const unsigned lo = max(0, (signed) selx[0] - (signed) sely[1]);
            const unsigned hi = min(selx[0], sely[0]);
            
            const double hyp_comp = Rf_dhyper(table[0], sely[0], sely[1], selx[0], 0);
            double hyp_sum = 0., hyp_less = 0.;
            
            for (unsigned ind = lo; ind <= hi; ind++)
            {
                double hyp = Rf_dhyper(ind, sely[0], sely[1], selx[0], 0);
                hyp_sum += hyp;
                if (hyp <= hyp_comp * relError) hyp_less += hyp;
            }
            
            density += log(hyp_sum) - log(hyp_less);
        }
        else
        {
            double stat = 0.;                
            
            for (unsigned ind = 0; ind < selxu * selyu; ind++)
            {
                double diff = outer[ind] - (double) table[ind];
                stat += diff * diff / outer[ind];
            }
            
            density -= Rf_pchisq(stat, (selxu - 1) * (selyu - 1), 0, 1); // Upper tail and logarithm are enabled
        }
        
        density_count++;
    }
    
    density /= density_count;
    
    free(rankx); // 'rankx' no longer needed
    rankx = NULL;
    
    ///////////////////////////////////////////////////////////////////////////
    //
    //  Simulations
    //
    
    const size_t rbnd = (size_t) REAL(rmax)[0], qcbnd = INTEGER(k)[0];
    const _Bool fixed = qcbnd == NA_INTEGER; // Entering 'fixed' mode if 'k' is 'NA'
    
    size_t rpl = 0, qc = 0;
    
    for (; rpl < rbnd && (fixed || qc <= qcbnd); rpl++)
    {
        // Generating random permutation, which  will be stored in 'selx'
        for (unsigned i = 0; i < nrow; selx[i] = i, i++); // Identity permutation
        
        for (unsigned i = 0; i < nrow; i++)
        {
            unsigned k = i + (unsigned) floor(unif_rand() * (nrow - i)), tmp = selx[k];
            selx[k] = selx[i];
            selx[i] = tmp;
        }
        
        // Density computation
        double density_ = 0.;
        size_t density_count_ = 0;
        
        for (unsigned j = 0; j < ncol; j++) if (row_count[j] && row_unique[j] >= 2)
        {
            const size_t nrowj = (size_t) nrow * (size_t) j;
            
            // Loading common data
            const unsigned selcount_ = row_count[j], selxu_ = row_unique[j], tablesum_ = row_tablesum[j];
            unsigned *const rankx_ = row_ranks + nrowj, *const selx_ = row_sums + nrowj;
            
            size_t test_size;
            unsigned selyu;
            double outer_min = DBL_MAX;
            
            // Filtering and applying the permutation (which is stored in 'selx')
            for (unsigned i = 0; i < selcount_; sely[i] = INTEGER(pht)[selx[row_filter[i + nrowj]]], i++);
            
            // Building contingency table        
            selyu = _getRanksUnique(sely, selcount_, ranky, temp);
            if (selyu < 2) continue; // Nothing to do!
            
            if ((test_size = (size_t) selxu_ * (size_t) selyu) > table_size) // Allocating memory space for the contingency table if needed...
            {
                free(table);
                free(outer);
                table_size = test_size;
                table = calloc(table_size, sizeof *table); // Table should be filled with zeros
                outer = malloc(table_size * sizeof *outer);
                if (!(table && outer)) goto error1; // Memory allocation failed 
            }
            else memset(table, 0, test_size * sizeof *table); // ...or just cleansing the existing memory
            
            for (unsigned i = 0; i < selcount_; table[rankx_[i] + selxu_ * ranky[i]]++, i++); // Initializing the table
            
            // Computing col sums (row sums are already computed) 
            memset(sely, 0, selyu * sizeof *sely); // We will use 'sely' array for col sums
            for (unsigned t = 0; t < selyu; t++) for (unsigned s = 0; s < selxu_; s++) sely[t] += table[(size_t) s + (size_t) selxu_ * (size_t) t];
            
            // Computing outer product
            for (unsigned s = 0; s < selxu_; s++) for (unsigned t = 0; t < selyu; t++)
            {
                size_t ind = (size_t) s + (size_t) selxu_ * (size_t) t;
                outer[ind] = (double) selx_[s] * (double) sely[t] / (double) tablesum_;
                if (outer[ind] < outer_min) outer_min = outer[ind];
            }
            
            // Computing p-values
            if (outer_min <= 4. && selxu_ == 2 && selyu == 2)
            {
                const unsigned lo = max(0, (signed) selx_[0] - (signed) sely[1]);
                const unsigned hi = min(selx_[0], sely[0]);
                
                const double hyp_comp = Rf_dhyper(table[0], sely[0], sely[1], selx_[0], 0);
                double hyp_sum = 0., hyp_less = 0.;
                
                for (unsigned ind = lo; ind <= hi; ind++)
                {
                    double hyp = Rf_dhyper(ind, sely[0], sely[1], selx_[0], 0);
                    hyp_sum += hyp;
                    if (hyp <= hyp_comp * relError) hyp_less += hyp;
                }
                 
                density_ += log(hyp_sum) - log(hyp_less);
            }
            else
            {
                double stat = 0.;
                
                for (unsigned ind = 0; ind < selxu_ * selyu; ind++)
                {
                    double diff = outer[ind] - (double) table[ind];
                    stat += diff * diff / outer[ind];
                }
                
                density_ -= Rf_pchisq(stat, (selxu_ - 1) * (selyu - 1), 0, 1); // Upper tail and logarithm are enabled
            }
            
            density_count_++;
        }
        
        density_ /= density_count_;
        if (density_ > density) qc++;
    }
    
    REAL(res)[0] = (double) qc / (double) rpl;
    if (!fixed && rpl == rbnd) REAL(res)[0] *= -1;
    
epilogue:
    // Memory should be cleared after all...
    free(selx);
    free(sely);
    free(rankx); // Normally it is cleared before
    free(ranky);
    free(temp);
    
    free(row_count);
    free(row_unique);
    free(row_tablesum);
    free(row_filter);
    free(row_ranks);
    free(row_sums);
    
    free(table);
    free(outer);
        
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
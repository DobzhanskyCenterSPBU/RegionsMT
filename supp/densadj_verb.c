#include <R.h>
#include <Rmath.h>

#define USE_RINTERNALS
#include <Rinternals.h>

#define INT_ISNA(X) ((unsigned) (X) == 0x80000000)

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))

int _comp_int(const void *a, const void *b) 
{ 
    if (*(int *) a > *(int *) b) return 1;
    if (*(int *) a < *(int *) b) return -1;
    return 0;
}

int _comp_intptr(const void *a, const void *b) 
{ 
    if (**(int **) a > **(int **) b) return 1;
    if (**(int **) a < **(int **) b) return -1;
    return 0;
}

int _comp_doubleptr(const void *a, const void *b) 
{ 
    if (*(double *) a > *(double *) b) return 1;
    if (*(double *) a < *(double *) b) return -1;
    return 0;
}

// '*pporder' is a temprorary array with pointers to the base array 'ar'
unsigned _getRanksUnique(int *ar, size_t count, unsigned *presult, int **pporder)
{
    unsigned unique = 0;
    
    for (size_t i = 0; i < count; pporder[i] = ar + i, i++);
    qsort(pporder, count, sizeof *pporder, &_comp_intptr);

    if (count) presult[pporder[0] - ar] = 0; 
    else return 0;
    
    for (size_t i = 1; i < count; i++)
    {
        if ((*_comp_int)(pporder[i - 1], pporder[i])) ++unique;
        presult[pporder[i] - ar] = unique;
    }

    return unique + 1;
}

SEXP _ranks(SEXP x)
{
    int *m = malloc(LENGTH(x) * sizeof *m), **temp = malloc(LENGTH(x) * sizeof *temp);
    unsigned *rank = malloc(LENGTH(x) * sizeof *rank);
    for (int i = 0; i < LENGTH(x); m[i] = INTEGER(x)[i], i++);
    
    _getRanksUnique(m, LENGTH(x), rank, temp);
    free(m);
    free(temp);
    
    SEXP z;
    PROTECT(z = allocVector(INTSXP, LENGTH(x)));
    
    for (int i = 0; i < LENGTH(x); INTEGER(z)[i] = rank[i], i++);
    free(rank);
    UNPROTECT(1);
    
    return z;
}

SEXP _zz(SEXP z)
{
    SEXP res;
    int p = INTEGER(z)[0];
    PROTECT(res = allocVector(REALSXP, p));
    for (int i = 0; i < p; i++) REAL(res)[i] = unif_rand();
    UNPROTECT(1);
    return res; 
}

SEXP _zz2(SEXP z)
{
    SEXP res;
    int p = INTEGER(z)[0];
    double *rnd = malloc(p * sizeof *rnd);
    void **ptr = malloc(p * sizeof *ptr);
    
    for (int i = 0; i < p; rnd[i] = unif_rand(), ptr[i] = rnd + i, i++);
    qsort(ptr, p, sizeof *ptr, &_comp_doubleptr);
    
    PROTECT(res = allocVector(INTSXP, p));
    for (int i = 0; i < p; INTEGER(res)[i] = (double *) ptr[i] - rnd, i++);
    free(rnd);
    free(ptr);
    
    UNPROTECT(1);
    return res; 
}

SEXP _tableTest(SEXP table)
{
    SEXP res, dim = getAttrib(table, R_DimSymbol);
    int n = INTEGER(dim)[0], m = INTEGER(dim)[1];
    
    PROTECT(res = allocVector(INTSXP, n * m));
    
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < m; j++)
            INTEGER(res)[i + n * j] = INTEGER(table)[i + n * j];
    
    UNPROTECT(1);        
    
    return res; 
}

SEXP _cTable(SEXP x, SEXP y)
{
    SEXP res;
    if (LENGTH(x) != LENGTH(y)) error("!");
    
    size_t len = LENGTH(x);
    int *a = malloc(len * sizeof *a), *b = malloc(len * sizeof *b);
    int **temp = malloc(len * sizeof *temp);
    unsigned *rankx = malloc(len * sizeof *rankx), *ranky = malloc(len * sizeof *ranky);
    unsigned xu, yu;
    
    for (int i = 0; i < len; a[i] = INTEGER(x)[i], b[i] = INTEGER(y)[i], i++);
        
    xu = _getRanksUnique(a, len, rankx, temp);
    yu = _getRanksUnique(b, len, ranky, temp);
    
    free(a);
    free(b);
    free(temp);
    
    PROTECT(res = allocMatrix(INTSXP, xu, yu));
    
    for (size_t i = 0; i < xu * yu; INTEGER(res)[i++] = 0);    
    for (size_t i = 0; i < len; INTEGER(res)[rankx[i] + xu * ranky[i]]++, i++);
    
    UNPROTECT(1);        
    
    return res; 
}

/*
# Usage in R:
densadj <- function(genotype, pht, max = 10^7, k = 10L) 
{ 
    storage.mode(genotype) <- "integer"
    .Call("_densadj", as.matrix(genotype), as.integer(pht), as.integer(max), as.integer(k))
}
*/

SEXP _densadj(SEXP genotype, SEXP pht, SEXP max, SEXP k)
{
    SEXP res;
    PROTECT(res = allocVector(REALSXP, 1)); // Result will be stored here
    
    _Bool success = 0;
    SEXP dim = getAttrib(genotype, R_DimSymbol);
    const size_t nrow = INTEGER(dim)[0], ncol = INTEGER(dim)[1];
    const double relError = 1. + 1.e-7;
    
    int *selx = NULL, *sely = NULL;
    unsigned *rankx = NULL, *ranky = NULL, *table = NULL;
    void **temp = NULL;
    double *rndunif = NULL, *outer = NULL;
    unsigned *row_count = NULL, *row_unique = NULL, *row_tablesum = NULL, *row_filter = NULL, *row_ranks = NULL, *row_sums = NULL;
    
    double density = 0.;
    size_t table_size = 0, density_count = 0;
        
    if (nrow != LENGTH(pht)) goto error; // Dimensions of genotype and phenotype are discordant        
    
    // Memory management ('table' and 'outer' are initialized elsewhere)
    selx = malloc(nrow * sizeof *selx);
    sely = malloc(nrow * sizeof *sely);
    rankx = malloc(nrow * sizeof *rankx);
    ranky = malloc(nrow * sizeof *ranky);
    temp = malloc(nrow * sizeof *temp);
    rndunif = malloc(nrow * sizeof *rndunif);
    
    row_count = malloc(ncol * sizeof *row_count);
    row_unique = malloc(ncol * sizeof *row_unique);
    row_tablesum = malloc(ncol * sizeof *row_tablesum);
    row_filter = malloc(nrow * ncol * sizeof *row_filter);
    row_ranks = malloc(nrow * ncol * sizeof *row_ranks);
    row_sums = malloc(nrow * ncol * sizeof *row_sums);
    
    if (!(selx && sely && rankx && ranky && temp && rndunif)) goto error; // Memory allocation failed
    if (!(row_count && row_unique && row_tablesum && row_filter && row_ranks && row_sums)) goto error;
    
    ///////////////////////////////////////////////////////////////////////////
    //
    //  Initialization
    //
    
    for (size_t j = 0; j < ncol; j++)
    {
        const size_t nrowj = nrow * j;
        
        size_t test_size;
        unsigned selxu, selyu, tablesum = 0, selcount = 0;
        double outer_min = DBL_MAX;
        
        // Filtering genotypes
        for (size_t i = 0; i < nrow; i++)
        {
            int gt = INTEGER(genotype)[i + nrowj];
            
            if (!INT_ISNA(gt) && gt != 3)
            {
                row_filter[selcount + nrowj] = (unsigned) i; // Saving correct indeces
                selx[selcount] = gt;
                sely[selcount] = INTEGER(pht)[i];
                selcount++;
            }           
        }
        
        row_count[j] = selcount; // Saving correct indeces count
        
        // Building contingency table        
        selxu = _getRanksUnique(selx, selcount, rankx, (int **) temp);
        selyu = _getRanksUnique(sely, selcount, ranky, (int **) temp);
        test_size = (size_t) selxu * (size_t) selyu;
        
        if (test_size > table_size) // Allocating memory space for the contingency table if needed...
        {
            free(table);
            free(outer);
            table_size = test_size;
            table = calloc(table_size, sizeof *table);
            outer = malloc(table_size * sizeof *outer);
            if (!(table && outer)) goto error; // Memory allocation failed 
        }
        else memset(table, 0, table_size * sizeof *table); // ...or just cleansing the existing memory
        
        for (size_t i = 0; i < selcount; table[rankx[i] + selxu * ranky[i]]++, i++); // Initializing the table
        
        // Computing sums
        memset(selx, 0, selxu * sizeof *selx); // We will use 'selx' array for row sums
        for (unsigned s = 0; s < selxu; tablesum += selx[s++]) for (unsigned t = 0; t < selyu; t++) selx[s] += table[(size_t) s + (size_t) selxu * (size_t) t]; // Computing row sums and total sum
        memset(sely, 0, selyu * sizeof *sely); // We will use 'sely' array for col sums
        for (unsigned t = 0; t < selyu; t++) for (unsigned s = 0; s < selxu; s++) sely[t] += table[(size_t) s + (size_t) selxu * (size_t) t]; // Computing col sums
        
        // Saving common data
        memcpy(row_ranks + nrowj, rankx, nrow * sizeof *rankx); // Saving row ranks
        memcpy(row_sums + nrowj, selx, selxu * sizeof *selx); // Saving row sums
        row_unique[j] = selxu; // Saving row count
        row_tablesum[j] = tablesum; // Saving table sum
        
        // Computing outer product
        for (unsigned s = 0; s < selxu; s++) for (unsigned t = 0; t < selyu; t++)
        {
            size_t ind = (size_t) s + (size_t) selxu * (size_t) t;            
            outer[ind] = (double) selx[s] * (double) sely[t] / (double) tablesum;
            if (outer[ind] < outer_min) outer_min = outer[ind];
        }
        
        // Computing p-values
        if (selxu >= 2 && selyu >= 2)
        {
            if (outer_min <= 4. && selxu == 2 && selyu == 2)
            {
                const unsigned lo = max(0, (signed) selx[0] - (signed) sely[1]);
                const unsigned hi = min(selx[0], sely[0]);
                
                const double hyp_comp = Rf_dhyper(table[0], sely[0], sely[1], selx[0], 0);
                double hyp_sum = 0., hyp_less = 0.;
                
                for (unsigned ind = lo; ind < hi; ind++)
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
                    double diff = fabs(outer[ind] - (double) table[ind]);
                    stat += diff * diff / outer[ind];
                }
                
                density -= Rf_pchisq(stat, (selxu - 1) * (selyu - 1), 0, 1); // Upper tail and logarithm are enabled                
            }
            
            density_count++;
        }
    }
    
    density /= density_count;
    
    ///////////////////////////////////////////////////////////////////////////
    //
    //  Simulations
    //
    
    size_t rpl = 0, qc = 0;
    
    for (; rpl < INTEGER(max)[0] && qc <= INTEGER(k)[0]; rpl++)
    {
        // Generating random permutation
        for (size_t i = 0; i < nrow; rndunif[i] = unif_rand(), temp[i] = rndunif + i, i++);
        qsort(temp, nrow, sizeof *temp, &_comp_doubleptr);
        
        // Density computation
        double density_ = 0.;
        size_t density_count_ = 0;
        
        for (size_t j = 0; j < ncol; j++)
        {
            const size_t nrowj = nrow * j;
            
            // Loading common data
            const unsigned selcount_ = row_count[j], selxu_ = row_unique[j], tablesum_ = row_tablesum[j];
            unsigned *const rankx_ = row_ranks + nrowj, *const selx_ = row_sums + nrowj;
                        
            size_t test_size;
            unsigned selyu;
            double outer_min = DBL_MAX;
            
            // Filtering and applying permutation
            for (size_t i = 0; i < selcount_; sely[i] = INTEGER(pht)[(double *) temp[row_filter[i + nrowj]] - rndunif], i++); // Applying permutation here
            
            // Building contingency table        
            selyu = _getRanksUnique(sely, selcount_, ranky, (int **) temp);
            test_size = (size_t) selxu_ * (size_t) selyu;
            
            if (test_size > table_size) // Allocating memory space for the contingency table if needed...
            {
                free(table);
                free(outer);
                table_size = test_size;
                table = calloc(table_size, sizeof *table);
                outer = malloc(table_size * sizeof *outer);
                if (!(table && outer)) goto error; // Memory allocation failed 
            }
            else memset(table, 0, table_size * sizeof *table); // ...or just cleansing the existing memory
        
            for (size_t i = 0; i < selcount_; table[rankx_[i] + selxu_ * ranky[i]]++, i++); // Initializing the table
            
            // Computing sums
            memset(sely, 0, selyu * sizeof *sely); // We will use 'sely' array for col sums
            for (unsigned t = 0; t < selyu; t++) for (unsigned s = 0; s < selxu_; s++) sely[t] += table[(size_t) s + (size_t) selxu_ * (size_t) t]; // Computing col sums
        
            // Computing outer product
            for (unsigned s = 0; s < selxu_; s++) for (unsigned t = 0; t < selyu; t++)
            {
                size_t ind = (size_t) s + (size_t) selxu_ * (size_t) t;            
                outer[ind] = (double) selx_[s] * (double) sely[t] / (double) tablesum_;
                if (outer[ind] < outer_min) outer_min = outer[ind];
            }
            
            // Computing p-values
            if (selxu_ >= 2 && selyu >= 2)
            {
                if (outer_min <= 4. && selxu_ == 2 && selyu == 2)
                {
                    const unsigned lo = max(0, (signed) selx_[0] - (signed) sely[1]);
                    const unsigned hi = min(selx_[0], sely[0]);
                    
                    const double hyp_comp = Rf_dhyper(table[0], sely[0], sely[1], selx_[0], 0);
                    double hyp_sum = 0., hyp_less = 0.;
                    
                    for (unsigned ind = lo; ind < hi; ind++)
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
                        double diff = fabs(outer[ind] - (double) table[ind]);
                        stat += diff * diff / outer[ind];
                    }
                    
                    density_ -= Rf_pchisq(stat, (selxu_ - 1) * (selyu - 1), 0, 1); // Upper tail and logarithm are enabled                
                }
            }
            
            density_count_++;
        }
        
        density_ /= density_count_;
        if (density_ > density) qc++;
    }
    
    REAL(res)[0] = (double) qc / (double) rpl;
    if (rpl == INTEGER(max)[0]) REAL(res)[0] *= -1;
    
    success = 1;
    
error:
    // Memory should be cleared after all...
    free(selx);
    free(sely);
    free(rankx);
    free(ranky);
    free(temp);
    free(rndunif);
    
    free(row_count);
    free(row_unique);
    free(row_tablesum);
    free(row_filter);
    free(row_ranks);
    free(row_sums);
    
    free(table);
    free(outer);
     
    UNPROTECT(1); 
    if (!success) error("Function '%s' exited with an error!\n", __FUNCTION__);
    return res; 
}
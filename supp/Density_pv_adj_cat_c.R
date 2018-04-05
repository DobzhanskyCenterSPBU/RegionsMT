library(gdsfmt)
library(SNPRelate)
#library(parallel)
library(plyr)
#library(doMC)
base.pth <- "~/"
# categorical
dll.pth <- paste0(base.pth, "densadj.dll")
dll.pth2 <- paste0(base.pth, "movadj2.dll")
dyn.load(dll.pth)
dyn.load(dll.pth2)

densadj2 <- function(genotype, pht, r.max = 10^7, k = 10) 
{ 
    storage.mode(genotype) <- "integer"
    .Call("_densadj", as.matrix(genotype), as.integer(pht), as.integer(r.max), as.integer(k))
}

densadj3 <- function(genotype, pht, r.max = 10^7, k = 10L) 
{ 
    storage.mode(genotype) <- "integer"
    .Call("_densadj2", as.matrix(genotype), as.integer(pht), as.numeric(r.max), as.integer(k))
}

maxadj2 <- function(genotype, pht, r.max = 10^7, k = 10L) 
{ 
    storage.mode(genotype) <- "integer"
    .Call("_maxadj2", as.matrix(genotype), as.integer(pht), as.numeric(r.max), as.integer(k))
}

movadj2 <- function(genotype, pht, r.max = 10^7, k = 10L, wnd = 10) 
{ 
    storage.mode(genotype) <- "integer"
    .Call("_movadj2", as.matrix(genotype), as.integer(pht), as.numeric(r.max), as.integer(k), as.integer(wnd))
}

densadj<-function(genotype, pht, r.max = 10^7, k = 10)     
    # genotype - vector length n
    # pht - matrix nx(2w+1) (w - parameter of the window)
{    																# Interior functions that can't be used ouside
    chisq_pv<-function(pht){                                        # functions are included into adj_dns_adapt_c to avoid parameters transfer (work with global variables)
        st<-sum(abs(xv-E.i)^2/E.i)									    # test statistic 
        par <- (nr - 1L) * (nc - 1L)									# degrees of freedom
        return(pchisq(st,par,lower.tail = FALSE))			
    }
    #
    fisher_pv<-function(pht){										#                                      
        m <- sum(xv[, 1L])
        n <- sum(xv[, 2L])
        k <- sum(xv[1L, ])
        x <- xv[1L, 1L]
        lo <- max(0L, k - n)
        hi <- min(k, m)
        support <- lo:hi
        logdc <- dhyper(support, m, n, k, log = TRUE)                     
        d <- logdc
        d <- exp(d - max(d))
        d<-d/sum(d)
        d.comp <- d[x-lo+1]
        #d.comp <- dhyper(x, m, n, k, log = FALSE)
        #d <- dhyper(support, m, n, k, log = FALSE)
        #d <- d / sum(d)
        return(sum(d[d <= d.comp * relErr]))
    }
    ##	cleaning	
    if (is.data.frame(genotype)||is.numeric(genotype)) genotype<-as.matrix(genotype)		 # this can be omitted in beta version
    if (!is.matrix(genotype))																 # this can be omitted in beta version
        stop("Genotype should be a matrix")
    d<-dim(genotype)												 # calculate range of genotype
    d.p<-d[1]
    d.g<-d[2]
    if (d.p!=length(pht))
        stop("Dimensions of genotype and phenotype are discordant")
    ## common calculations 
    num.p<-length(pht)
    nlev.g<-NULL                    # number of levels of genotype		
    cts.g<-list()                  # counts of genotypes 
    gt<-list()					   # list of genotypes
    pvt<-NULL                      # baseline p-values
    ftr<-list()                    # filter for genotypes
    FOK<-NULL
    relErr <- 1 + 10^(-7)
    for (i in 1:d.g) {             # 
        ggtype<-genotype[,i]                    # original genotype
        OK<- !is.na(ggtype) & ggtype!=3        # filter by genotype
        FOK[i]<-sum(as.numeric(OK))>0
        if (FOK[i]){
            ftr[[i]]<-OK								# the same for all replications
            ptt<-pht[OK]							# genotyped phenotype
            gtt<-ggtype[OK]							# filtered genotypes
            gt[[i]] <-gtt	                         # the same for all replications 				
            xv<-table(gtt,ptt)						# contingency table
            nlev.g[i]<-dim(xv)[1] 					# the same for all replications
            nr<-nlev.g[i]
            nc<-dim(xv)[2]							# 
            sr<-rowSums(xv)
            sc<-colSums(xv)
            cts.g[[i]]<-sr
            E.i<- outer(sr,sc,"*")/sum(xv)			# !! total number of observations in the denominator here / length(ptt)
            ft<-min(E.i)<=4L && nr==2L && nc==2L
            ft1<-nr<2L || nc<2L
            if (ft1) pvt[i]<-NA				# baseline p-values (just for control, can be fixed on preliminary stage)
            else if (ft) pvt[i]<- -log(fisher_pv(ptt))
            else  pvt[i]<- -log(chisq_pv(ptt))
        } 
        else pvt[i]<-NA
    } 
    
    dns.b<-mean(pvt, na.rm=TRUE)                             # if it will be fixed on preliminary stage we need not control '!is.na(pvt)'
    return(dns.b)
    
    # Simulations 			
    j<-0
    qc<-0
    while (qc<=k & j<r.max)
    {
        j<-j+1
        pht1<-pht[sample(num.p)]
        pv<-NULL
        for (i in 1:d.g){
            if (FOK[i]){
                gtt<-gt[[i]]
                OK<-ftr[[i]]
                ptt<-pht1[OK]
                xv<-table(gtt,ptt)
                nr<-nlev.g[i]
                nc<-dim(xv)[2]
                sr<-cts.g[[i]]
                sc<-colSums(xv)
                E.i<- outer(sr,sc,"*")/sum(xv) 					# !! total number of observations in the denominator here / length(ptt) 
                ft<-min(E.i)<=4L && nr==2L && nc==2L
                ft1<- nr<2L || nc<2L
                if (ft1) pv[i]<-NA						# baseline p-values (just for control, can be fixed on preliminary stage) !!
                else if (ft) pv[i]<-fisher_pv(ptt)				# !! pv in place of pvt
                else  pv[i]<-chisq_pv(ptt)		
            } 
            else pv[i]<-NA 		
        }
        dns<-mean(-log(pv[!is.na(pv)]))
        qc<-qc+as.numeric(dns>dns.b)
    }
    adns<-qc/j
    if (j==r.max) adns = -adns
    adns
}	
#################### Density p-values base ###############################
densadj_base_c<-function(i,gen,pht,r.max,k) densadj(gen[[i]],pht[[i]],r.max,k)
#################### Density p-values base ###############################
densadj_par_c<-function(gent.l,pht.l,kl)
{
	cl<-makeCluster(kl) # number of cores here
	res.t<-parLapply(cl,1:kl,densadj_base_c,gen=gent.l,pht=pht.l,r.max=10^7,k=10)
	stopCluster(cl)
	res.t
}
##########################################################################
########################### R-program ####################################
kl<-2
lb<-0
#registerDoMC(kl)
#
pth.l<-paste0(base.pth, "DATA/TABLES/1npc")           # list of tests
# pth.f<-"~/SOFTPARALLEL3/FUNCTIONS.R"    # 
pth.q<-paste0(base.pth, "DATA/TABLES/Density_10(Row)_Average.txt") # table of tops non-adjusted p-values
#
pv.adj<-NULL                                         
tst.lst<-c("CAT")                                    # to select categorical tests only from the list
#
inf.f<-read.table(pth.q,sep=",")                     # reading table of tops
datl<-read.table(pth.l)                              # reading table of tests
# filter by tests
ftr.t<-array(FALSE,dim=dim(datl)[1])                 # 
for (ii in 1:length(tst.lst))                        # selection categorical tests only
{
	ts.s<-as.character(datl[,2])
	tst.s<-data.frame(strsplit(ts.s,'/'))
	ftr.t[tst.s[1,]==tst.lst[ii]]<-TRUE	
}
rm(ts.s)
rm(tst.s)
ftr.t2<-as.numeric(datl[,1])[ftr.t]					#
ftr.t3<-array(FALSE,dim=dim(inf.f)[1])
for (ii in 1:length(ftr.t2)) ftr.t3[inf.f[,2]==ftr.t2[ii]]<-TRUE
inff<-inf.f[ftr.t3,]								#   only categorical tests  
nt<-25#dim(inff)[1]
gent.l<-list()
pht.l<-list()
pv<-NULL
lpv<-NULL
pv.adj<-NULL

for (i in 1:nt)                # parallel on this circle
{
	inff.i<-inff[i,]
	tst<-as.character(datl[as.numeric(inff.i[2]),2])
	tsts<-strsplit(tst,'/')[[1]]                        # split test name 
	lts<-length(tsts)									# 
	alt<-tsts[lts]										# the last site is the alternative
	grp<-tsts[3]											# the third site is the groupping / population filter
	ts<-tsts[1]											# the first site is the test name (CAT)
	#
	if (grp=="N+N-") pth.d<-paste0(base.pth, "DATA/GDS/npc_cleaned_2v01.gds") # data files
	if (grp=="N+E-") pth.d<-paste0(base.pth, "DATA/GDS/npc_cleaned_2v0.gds")
	if (grp=="N+E+") pth.d<-paste0(base.pth, "DATA/GDS/npc_cleaned_2v1.gds")
	if (grp=="N+E+E-") pth.d<-paste0(base.pth, "DATA/GDS/npc_cleaned.gds")
	#
	dat.f<-openfn.gds(pth.d)
	chr<-read.gdsn(index.gdsn(dat.f,"snp.chromosome"))
	pos<-read.gdsn(index.gdsn(dat.f,"snp.position"))
	gen<-read.gdsn(index.gdsn(dat.f,"genotype"))
	pht<-read.gdsn(index.gdsn(dat.f,"sample.annot"))$phenotype
	closefn.gds(dat.f)
	#  selection genotype matrix by window
	ftr<- chr==as.integer(inff.i[5])  
	gent<-gen[,ftr]
	wnd<-c(as.integer(inff.i[6]):as.integer(inff.i[7]))
	pht.l[[i]]<-pht
	gent<-gent[,wnd]
	# genotype selection (alternative) 
	dg<-dim(gent)
	num.p<-dg[1]
	num.g<-dg[2] 
	if (alt=="D") genotype=1*(gent==0)+3*(gent==3)
	if (alt=="R") genotype=1*(gent==2)+3*(gent==3)
	if (alt=="A") {
		pht<-c(pht,pht)
		num.p2<-2*num.p
		genotype2<-matrix(nrow=num.p2,ncol=num.g)
		genotype2[1:num.p,]<-1*(gent==2 | gent==1)+ 3*(gent==3)
		genotype2[(num.p+1):num.p2,]<-1*(gent==2)+3*(gent==3)
		genotype<-genotype2
		rm(genotype2)
		}
	gent.l[[i]]<-genotype
	pv[i]<-as.numeric(inff.i[3])
	lpv[i]<-as.numeric(inff.i[4])*log(10)                        # genotypes and phenotypes are completed (they can be written to files signed by test No)
}        
#

#system.time(print(densadj2(gent.l[[1]],pht.l[[1]], 10^5)))

if(0)
{
nt <- 20
dns <- list(b=c(), b2=c(), diff=c(),rel=c())
skip <- c(22,28,31,32,34,42,45,48,50)

for (i in 1:nt) if (!(i %in% skip))
{
    dns$b[i]<-densadj(gent.l[[i]],pht.l[[i]], 100)
    dns$b2[i]<-densadj3(gent.l[[i]],pht.l[[i]], 100)
}
    
dns$diff <- abs(dns$b-dns$b2)
dns$rel <- dns$diff / dns$b
print(as.data.frame(dns))
}

if(0)
{
pv.adj<-densadj_par_c(gent.l,pht.l,kl)
#exp
# output
pva<-NULL
ind<-NULL
for (i in 1:nt)
{
	rt<-pv.adj[[i]]
	pva[i]<-rt$apv
	ind[i]<-as.numeric(rt$eq)
}
out<-inff
out$apv<-pva
out$less<-ind
write.table(out,paste0(base.pth, "Density_10(Row)_Average_adj.txt"),sep=",",row.names=FALSE)
rk<-order(pva)
out2<-out[,rk]
write.table(out2,paste0(base.pth, "Density_10(Row)_Average_adj_sort.txt"),sep=",",row.names=FALSE)
# single test parallel
}

dyn.unload(dll.pth)
dyn.unload(dll.pth2)

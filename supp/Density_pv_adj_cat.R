library(gdsfmt)
library(SNPRelate)
library(parallel)
library(plyr)
library(doMC)
# categorical
densadj_base_c<-function(i,alt,pht,gtype,expl,pth.f="~/SOFTPARALLEL3/FUNCTIONS.R")
# i - number of sequential pvd
# alt - alternative
# phtpe - original phenotype
# pth.d - filetred genotype
# gent - list of genotypes (block)
# expl - counts in the corresponding (block)
{
	library(gdsfmt)
	source(pth.f)
	#
	adj_dns_adapt_c<-function(alt,genotype,pht,num.g,n.lev,num.p,r.max=10^7,k=10)     
	{
		pvt<-tests_mc(alt,genotype,pht,num.g,n.lev)$pv
		dns.b<-mean(-log(pvt[!is.na(pvt)]))
 		j<-0
 		qc<-0
 		while (qc<=k & j<r.max)
 		{
 			j<-j+1
 			pht1<-pht[sample(num.p)]
 			pvt<-tests_mc(alt,genotype,pht1,num.g,n.lev)$pv
 			dns<-mean(-log(pvt[!is.na(pvt)]))
 			qc<-qc+as.numeric(dns>dns.b)
 		}
		adns<-list()
 		adns$apv<-qc/j
 		adns$eq<-(j==r.max)
 		adns
	}
	#
	genotype<-gtype[[i]]
 	annot<-expl[[i]]
	ant<-(annot[,1:3]>0)
	dg<-dim(genotype)
	num.p<-dg[1]
	num.g<-dg[2]
	n.lev<-((ant[,1]&ant[,2])|(ant[,2]&ant[,3])|(ant[,1]&ant[,3]))
	phtpe<-pht[[i]]
	if (alt=="D")
	{
		n.lev<-ant[,1]&(ant[,2]|ant[,3]) 
		genotype=1*(genotype==0)+3*(genotype==3)
	}
	if (alt=="R") 
	{ 
		n.lev<-((ant[,1]|ant[,2])&(ant[,3]))
		genotype=1*(genotype==2)+3*(genotype==3)
	}
	if (alt=="A") 
	{
		n.lev<-ant[,2]|(ant[,1] & ant[,3])
		phtpe<-c(phtpe,phtpe)
		num.p2<-2*num.p
		genotype2<-matrix(nrow=num.p2,ncol=num.g)
		genotype2[1:num.p,]<-1*(genotype==2 | genotype==1)+ 3*(genotype==3)
		genotype2[(num.p+1):num.p2,]<-1*(genotype==2)+3*(genotype==3)
		genotype<-genotype2
		rm(genotype2)
	}
	pva<-adj_dns_adapt_c(alt,genotype,phtpe,num.g,n.lev,num.p)
	pva
}
# 
density_par_c<-function(alt,phtpe,gtype,expl,pth.f,kk,kl)
{
	cl<-makeCluster(kl) # number of cores here
	res.t<-parLapply(cl,1:kk,densadj_base_c,alt=alt,pht=phtpe,gtype=gtype,expl=expl,pth.f=pth.f)
	stopCluster(cl)
	res.t
}
#
kl<-12
lb<-0
#registerDoMC(kl)
#
pth.l<-"~/NPC/DATA/TABLES/1npc" 
pth.f<-"~/SOFTPARALLEL3/FUNCTIONS.R"
pth.q<-"~/NPC/DATA/TABLES/Density_10(Row)_Average.txt" 
source(pth.f)
#
pv.adj<-NULL
tst.lst<-c("CAT")
#
inf.f<-read.table(pth.q,sep=",")
datl<-read.table(pth.l)
# filter by tests
ftr.t<-array(FALSE,dim=dim(datl)[1])
for (ii in 1:length(tst.lst)) 
{
	ts.s<-as.character(datl[,2])
	tst.s<-data.frame(strsplit(ts.s,'/'))
	ftr.t[tst.s[1,]==tst.lst[ii]]<-TRUE	
}
rm(ts.s)
rm(tst.s)
ftr.t2<-as.numeric(datl[,1])[ftr.t]
ftr.t3<-array(FALSE,dim=dim(inf.f)[1])
for (ii in 1:length(ftr.t2)) ftr.t3[inf.f[,2]==ftr.t2[ii]]<-TRUE
inff<-inf.f[ftr.t3,]
nt<-dim(inff)[1]
gent.l<-list()
expl.l<-list()
pht.l<-list()
pv<-NULL
lpv<-NULL
#
for (i in 1:nt)
{
inff.i<-inff[i,]
tst<-as.character(datl[as.numeric(inff.i[2]),2])
tsts<-strsplit(tst,'/')[[1]]
lts<-length(tsts)
alt<-tsts[lts]
grp<-tsts[3]
ts<-tsts[1]
#
if (grp=="N+N-") pth.d<-"~/NPC/ROOT/DATA/npc_cleaned_2v01.gds"
if (grp=="N+E-") pth.d<-"~/NPC/ROOT/DATA/npc_cleaned_2v0.gds"
if (grp=="N+E+") pth.d<-"~/NPC/ROOT/DATA/npc_cleaned_2v1.gds"
if (grp=="N+E+E-") pth.d<-"~/NPC/ROOT/DATA/npc_cleaned.gds"
#
dat.f<-openfn.gds(pth.d)
chr<-read.gdsn(index.gdsn(dat.f,"snp.chromosome"))
pos<-read.gdsn(index.gdsn(dat.f,"snp.position"))
gen<-read.gdsn(index.gdsn(dat.f,"genotype"))
pht<-read.gdsn(index.gdsn(dat.f,"sample.annot"))$phenotype
expl<-read.gdsn(index.gdsn(dat.f,"explanatory"))
closefn.gds(dat.f)
#
ftr<- chr==as.integer(inff.i[5])  
gent<-gen[,ftr]
expll<-expl[ftr,]
wnd<-c(as.integer(inff.i[6]):as.integer(inff.i[7]))
gent.l[[i]]<-gent[,wnd]
expl.l[[i]]<-expll[wnd,]
pht.l[[i]]<-pht
pv[i]<-as.numeric(inff.i[3])
lpv[i]<-as.numeric(inff.i[4])*log(10)
}
#
kk<-nt
#
pv.adj<-density_par_c(alt,pht.l,gent.l,expl.l,pth.f,kk,kl)
# output
pva<-NULL
ind<-NULL
for (i in 1:nt)
{
	rt<-pv.adj[[i]]
	pva[i]<-rt$apv
	ind[i]<-as.numeric(rt$eq)
}
out<-inf.f
out$apv<-pva
out$less<-ind
write.table(out,"~//NPC/DATA/TABLES/Density_10(Row)_Average_adj.txt",sep=",",row.names=FALSE)
# single test parallel





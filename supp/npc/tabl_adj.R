library(gdsfmt)
library(SNPRelate)

setwd("f:/data/npc/")

out.d<-"tbl_adj/"
par.d<-"tests/"
path2<-"npc_cleaned.gds"

dat<-openfn.gds(path2)
alias<-read.gdsn(index.gdsn(dat,"snp.id"))
pos<-read.gdsn(index.gdsn(dat,"snp.position"))
chr<-read.gdsn(index.gdsn(dat,"snp.chromosome"))
allele<-strsplit(read.gdsn(index.gdsn(dat,"snp.allele")),'/')

genotype<-read.gdsn(index.gdsn(dat,"genotype"))
phenotype<-read.gdsn(index.gdsn(dat,"sample.annot/phenotype"))
sex<-read.gdsn(index.gdsn(dat,"sample.annot/sex"))

closefn.gds(dat)


for (ch in unique(chr))
{
  ftr <- chr == ch
  al <- allele[ftr]
  maj <- unlist(lapply(al, `[[`, 1))
  min <- unlist(lapply(al, `[[`, 2))
  snp.out<-data.frame(pos=pos[ftr],alias=alias[ftr],allele.major=maj,allele.minor=min)
  write.csv(snp.out, file=paste0(out.d, "snp-", ch, ".csv"))
}

phenotype.out<-data.frame(sex=sex, phenotype=phenotype)
write.csv(phenotype.out, file=paste0(paste0(out.d,"phenotype.csv")),quote=F)

genotype.out<-as.data.frame(t(genotype))
colnames(genotype.out)<-rownames(phenotype.out)
write.csv(genotype.out, file=paste0(paste0(out.d,"genotype.csv")),quote=F)

genotype.out100<-genotype.out[1:100,]
write.csv(genotype.out100, file=paste0(paste0(out.d,"genotype.100.csv")),quote=F)

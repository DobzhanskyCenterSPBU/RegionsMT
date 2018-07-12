library(gdsfmt)
library(SNPRelate)

setwd("f:/data/npc")

out.d<-"tbl/"
par.d<-"tests/"
path2<-"npc_cleaned.gds"

pop<-"NPC"
tpe<-c("CAT","GLM")
sub<-c("N+N-","N+E+","N+E-","N+E+E-")
grp<-c("CD","D","R","A")
col.no<-0
tst.lst<-"NULL"
f.lst<-"NULL"
i<-0
TST.RT<-list()
for (tp in tpe)
  for (sb in sub)
    for (gp in grp)
    {
      i<-i+1
      col.no[i]<-i
      tst.lst[i]<-paste0(tp,"_",pop,"_",sb,"_",gp)
      f.lst[i]<-paste0(tp,"_",pop,"_",sb,"_",gp,".gds")
      dat<-openfn.gds(paste(par.d,f.lst[i],sep=""))   
      TST.RT[[i]]<-read.gdsn(index.gdsn(dat,"tests.results"))
      closefn.gds(dat)
    }


## TABLE 1 (tests)

out.test<-list()
out.test$col<-col.no
out.test$test<-tst.lst
out.test<-as.data.frame(out.test)

dest <- file(paste0(out.d, "test.csv"), open="wb")
write.table(out.test, dest, row.names = F, sep = ",", eol="\n")
close(dest) 

## TABLE 2 (chr)

dat<-openfn.gds(path2)
snp<-read.gdsn(index.gdsn(dat,"snp.id"))
pos<-read.gdsn(index.gdsn(dat,"snp.position"))
chr<-read.gdsn(index.gdsn(dat,"snp.chromosome"))
all<-read.gdsn(index.gdsn(dat,"snp.allele"))
closefn.gds(dat)

chr.name<-unique(chr)

out.chr<-list()
out.chr$chr<-1:length(chr.name)
out.chr$name<-chr.name

dest <- file(paste0(out.d, "chr.csv"), open="wb")
write.csv(out.chr, dest, row.names = F, eol = "\n")
close(dest) 

## TABLE 3 (row)

chr.len<-sapply(1:length(chr.name), function(x) { sum(chr==x) })
nrow<-unlist(lapply(1:length(chr.name), function(x) {1:chr.len[x]}))

maf<-array(0,dim=length(nrow))

out.row<-list()
out.row$chr<-chr
out.row$nrow<-nrow
out.row$snp<-snp
out.row$pos<-pos
out.row$all<-all
out.row$maf<-maf
out.row<-as.data.frame(out.row)

dest <- file(paste0(out.d, "row.csv"), open="wb")
write.csv(out.row, dest, row.names = F)
close(dest) 

## TABLE 3 (val)

val<-data.frame(chr=integer(), nrow=integer(), col=integer(), pval=double(), qas=double(), maf=double())

dest <- file(paste0(out.d, "val.csv"), open="wb")
write.csv(val, dest, row.names = F)
close(dest) 

dest <- file(paste0(out.d, "val.csv"), open="ab")

for (cl in col.no)
{
  out.val<-list()
  out.val$chr<-chr
  out.val$nrow<-nrow
  out.val$col<-rep(cl, length(nrow))
  out.val$pval<-TST.RT[[cl]]$pv
  out.val$qas<-TST.RT[[cl]]$or
  out.val$maf<-rep(0, length(nrow))
  out.val<-as.data.frame(out.val)
  
  write.table(out.val, dest, row.names = F, sep = ",", col.names = F, na="-1")
}

close(dest)



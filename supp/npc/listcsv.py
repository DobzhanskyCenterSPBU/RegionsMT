import glob

wdir = "/mnt/f/data/npc/rep/"
wdir2 = "f:/data/npc/rep/"
gl = glob.glob(wdir + "*.csv")

for g in gl:
    t0=g.split('/')[-1]
    t1=t0.split('.')[0].split('_')
    print('load data concurrent local infile "' + wdir2 + t0 + '" into table `report_top_hits_' + t1[0] + '` fields terminated by \',\' optionally enclosed by \'"\' lines terminated by \'\\n\' ignore 1 lines (`v_ind`, `r_density`, `density`, `left_ind`, `right_ind`, `left_count`, `right_count`) set `radius_ind` = ' + t1[1] + ', `fold_ind` = ' + t1[2] + ';')
    
    
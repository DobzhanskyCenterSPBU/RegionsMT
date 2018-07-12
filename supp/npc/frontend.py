from io import BytesIO
from collections import namedtuple
import xml.etree.ElementTree as ET
from xml.dom import minidom
import MySQLdb
import sys
import time

mysqlinf = namedtuple("mysqlinf", "host port login password")
fileinf = namedtuple("fileinf", "chr test row val")
rdir = "f:/data/npc/tbl/"
wdir = "f:/data/npc/rep/"
inf = mysqlinf(host = "127.0.0.1", port=3306, login = "root", password = "root")
finf = fileinf(chr=rdir+"chr.csv", test=rdir+"test.csv", row=rdir+"row.csv", val=rdir+"val.csv")

db = MySQLdb.connect(host=inf.host, user=inf.login, passwd=inf.password, db="module_z")
cur = db.cursor()

cur.execute('select * from `radius_ind`')
rad = cur.fetchall()
cur.execute('select * from `fold_ind`')
fold = cur.fetchall()

path_log = "stderr"
root = ET.Element("RegionsMT", threads = '8', log = path_log)
ld = ET.SubElement(root, "Data.Load",{ "logarithm" : "False", "path.chr" : finf.chr, "path.test" : finf.test, "path.row" : finf.row, "path.val" : finf.val, "header" : "True" })
for r in rad:
    dns = ET.SubElement(ld, "Density", type = "average", radius = str(r[2]), pos = str(r[1] == 'pos'))
    for f in fold:
        fld = ET.SubElement(dns, "Fold", type = f[1], group = f[2])
        ET.SubElement(fld, "Report", path = wdir + 'density_' + str(r[0]) + '_' + str(f[0]) + '.csv', type = "density", limit = "5000", header = "True")
        ET.SubElement(fld, "Report", path = wdir + 'pval_' + str(r[0]) + '_' + str(f[0]) + '.csv', type = "nlpv", limit = "5000", header = "True")
        ET.SubElement(fld, "Report", path = wdir + 'qas_' + str(r[0]) + '_' + str(f[0]) + '.csv', type = "qas", limit = "5000", header = "True")

reparsed = minidom.parseString(ET.tostring(root, 'utf-8'))
print(reparsed.toprettyxml(indent="    "))

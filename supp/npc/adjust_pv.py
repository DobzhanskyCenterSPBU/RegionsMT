import MySQLdb
g_pref = "/home/aantonik/npc/tmp/gtype-"
from collections import namedtuple
mysqlinf = namedtuple("mysqlinf", "host port login password")
inf = mysqlinf(host = "127.0.0.1", port=3306, login = "aantonik", password = "aantonik")
db = MySQLdb.connect(host=inf.host, user=inf.login, passwd=inf.password, db="Module_NPC")
cur = db.cursor()
cur.execute('select * from `report_top_hits_density`')
hits = cur.fetchall()
cur.execute('select count(*) from `phenotypes`')
cnt = cur.fetchall()[0][0]

g_list = [];

for h in hits:
    cur.execute('select * from `genotypes` where ind >= ' + str(h[5]) + ' and ind <= ' + str(h[6]))
    g_name = g_pref + str(h[5]) + '-' + str(h[6]) + '.csv'
    ind = 1
    g_list.append(g_name)
    with open(g_name, 'w') as f:
        for i in range(1, cnt + 1):
            f.write(',' + str(i))
        f.write('\n')
        gtypes = cur.fetchall()
        for g in gtypes:
            f.write(str(ind) + ',' + ','.join(g[0]) + '\n')
            ind += 1
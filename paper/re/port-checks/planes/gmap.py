import sys
RE = '/home/dennis/Downloads/acme_303/paper/re/'
col5 = {}
for l in open(RE + 'full.ticks'):
    a = l.split(); col5[int(a[0])] = int(a[4])
G = {}; g = -2
for t in range(8818, 10968):
    g += col5[t]; G[t] = g
ts = [int(x) for x in sys.argv[1:]]
print(','.join('%.4f' % (8818 + (G[t] - 1) * 70.0018 / 34) for t in ts))

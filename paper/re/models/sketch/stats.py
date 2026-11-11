import pickle, collections, ast
from cmp1 import MM
from mm import M
img = M('img')
dumps = pickle.load(open('d2000.dumps', 'rb'))
s = MM([d for d in dumps if d['addr']==0x604a and 'mem' in d][0]['mem'])
objs = [s.d(0x5db8 + 4*k) for k in range(11)]
fmap = {}
for k, o in enumerate(objs):
    fa = s.d(o+0x74)
    for f in range(s.d(o+0x70)): fmap[fa+f*0x4c] = (k, f)
vis_disp = 0; hidden = 0; notsil = []
pairs = collections.Counter()
for e in range(0x113):
    a = 0x5073 + 5*e; fp = s.d(a); fl = img.by(a+4)
    i = {1:0, 2:1, 4:2}[fl & 7]
    vs = [s.d(fp), s.d(fp+4), s.d(fp+8)]
    pairs[frozenset((vs[i], vs[(i+1)%3]))] += 1
    f1d = s.by(fp+0x1d)
    if f1d & 0x80: hidden += 1; continue
    if f1d & ((1 << i) | (8 << i)): vis_disp += 1
    else: notsil.append((e, fmap[fp]))
print('hidden faces', hidden, 'displayed after completion', vis_disp, 'not displayed', notsil)
print('edges stroked twice', [c for c in pairs.values() if c > 1])
# displayed edges NOT in the stroke table? count faces with sil/crease bits but no flags
tot = 0
fl20 = collections.Counter()
for k, o in enumerate(objs):
    fa = s.d(o+0x74)
    for f in range(s.d(o+0x70)):
        fp = fa + f*0x4c
        if s.by(fp+0x1d) & 0x80: continue
        b = s.by(fp+0x1d)
        for i in range(3):
            if b & ((1<<i)|(8<<i)): tot += 1
print('silhouette/crease edges of visible faces (all objects)', tot)

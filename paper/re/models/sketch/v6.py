from mm import M
import collections, math
img = M('img'); s = M(1250)
objs = [s.d(0x5db8 + 4*k) for k in range(11)]
fmap = {}
for k, o in enumerate(objs):
    fa = s.d(o+0x74); nf = s.d(o+0x70); va = s.d(o+0x78)
    for f in range(nf): fmap[fa + f*0x4c] = (k, f)
vmap = {}
for k, o in enumerate(objs):
    va = s.d(o+0x78); nv = s.d(o+0x6c)
    for v in range(nv): vmap[va + v*0x24] = (k, v)
rows = []
for e in range(0x113):
    a = 0x5073 + 5*e
    ordn = img.d(a); fl = img.by(a+4); ptr = s.d(a)
    k, f = fmap.get(ptr, (None, None))
    fp = ptr
    vs = [s.d(fp), s.d(fp+4), s.d(fp+8)]
    if fl & 1: A, B = vs[0], vs[1]
    if fl & 2: A, B = vs[1], vs[2]
    if fl & 4: A, B = vs[2], vs[0]
    if fl & 8: A, B = B, A
    ax, ay = s.i(A+0x18) >> 5, s.i(A+0x1c) >> 5
    bx, by = s.i(B+0x18) >> 5, s.i(B+0x1c) >> 5
    rows.append((e, ordn, fl, k, f, vmap[A][1], vmap[B][1], ax, ay, bx, by))
print('flag values', collections.Counter(r[2] for r in rows))
print('objects', collections.Counter(r[3] for r in rows))
print('ordinals range', min(r[1] for r in rows), max(r[1] for r in rows), 'dups', [x for x,c in collections.Counter((r[1], r[2]&7) for r in rows).items() if c>1])
for r in rows: print(r)

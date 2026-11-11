import numpy as np
from mm import M
img = M('img'); s = M(1250)
X = [img.d(0x9a0 + 4*k) for k in range(64)]
i, j = img.by(0xaa0), img.by(0xaa1)
N = 200000
out = np.zeros(N, np.uint32)
for n in range(N):
    X[j] = (X[i] + X[j]) & 0xffffffff
    out[n] = X[j]
    i = (i + 1) & 63; j = (j + 1) & 63
np.save('rng.npy', out)
# grey table
g = np.frombuffer(s.b, np.uint8, 0x400, s.d(0x1ee09))
def grey(o):
    al = out[o:o+0x400] & 0x1f
    v = np.where(al < 5, (al >> 2) + 0x1e, (al >> 3) + 0x1a)
    return np.minimum(v, 0x1f)  # >0x20 -> 0x1f never happens
for o in range(0, 2000):
    if (grey(o) == g).all(): print('grey table starts at call', o); go = o; break
j8 = np.frombuffer(s.b, np.int8, 0x400, s.d(0x1ee0d))
o = go + 0x400
r = ((out[o:o+0x400*120].astype(np.int64) & 0xf) - 7).reshape(0x400, 120).sum(1)
jv = ((r >> 6) - 1).astype(np.int8)
print('jitter table from calls', o, '..', o + 0x400*120 - 1, 'match', (jv == j8).all())
tex = np.frombuffer(s.b, np.uint8, 64000, s.d(0x1edf5))
def texat(o):
    w = out[o:o+0x3ed1] & 0x0f0f0f0f
    r = w.view(np.uint8).astype(np.int16)
    d = (r[:64000] - r[321:64321]).astype(np.int8) >> 3
    return (d + 0x1e).astype(np.uint8)
start = o + 0x400*120
for o2 in range(start, start + 20000):
    w = out[o2:o2+200] & 0x0f0f0f0f
    rr = w.view(np.uint8).astype(np.int16)
    d = ((rr[:400] - rr[321:721]).astype(np.int8) >> 3) + 0x1e
    if (d.astype(np.uint8) == tex[:400]).all():
        print('texture random words start at call', o2, 'full match', (texat(o2) == tex).all()); break

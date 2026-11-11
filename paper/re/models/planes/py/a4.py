import sys
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import rep, numpy as np
S = rep.SEG
a = 0x187d5
nv, nf = S[a], S[a + 1]; p = a + 2
V = []
for i in range(nv):
    V.append(tuple(x - 256 if x > 127 else x for x in S[p:p + 3])); p += 3
F = []
for i in range(nf):
    F.append(tuple(S[p:p + 4])); p += 4
pos = {}
for i, v in enumerate(V): pos.setdefault(v, []).append(i)
print('distinct positions', len(pos))
for v, ids in sorted(pos.items(), key=lambda kv: kv[1][0]): print(' ', v, ids)
# map faces to position triples
def key(f): return tuple(V[i] for i in f[:3])
tw = 0
for i, f in enumerate(F):
    k = key(f)
    rev = (k[0], k[2], k[1])
    js = [j for j, g in enumerate(F) if j != i and set(key(g)) == set(k)]
    if js: tw += 1
print('faces with a twin (same 3 positions):', tw)
# normals of faces (by positions) and area
for i, f in enumerate(F):
    A, B, C = (np.array(V[q]) for q in f[:3])
    n = np.cross(B - A, B - C)
    print(i, f[:3], 'flags', f[3], 'n', (n / (np.linalg.norm(n) or 1)).round(2), 'area', round(np.linalg.norm(n) / 2, 1))

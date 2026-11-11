import sys, pickle, struct, glob, os
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import unicorn  # needed for unpickling
img = open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()
seg = img[0xd00:]
T0 = list(struct.unpack_from('<64I', seg, 0x9a0)); i0 = seg[0xaa0] & 63; j0 = seg[0xaa1] & 63
print('init idx', i0, j0)
NMAX = 3_000_000
T = T0[:]; i, j = i0, j0
out = [0]*(NMAX+1)
for n in range(1, NMAX+1):
    v = (T[i] + T[j]) & 0xffffffff; T[j] = v; i = (i+1)&63; j = (j+1)&63; out[n] = v
# index of positions by value for quick lookup
from collections import defaultdict
pos = defaultdict(list)
for n in range(1, NMAX+1): pos[out[n]].append(n)
snaps = sorted(glob.glob(sys.argv[1] + '/t*.snap'), key=lambda p: int(os.path.basename(p)[1:-5]))
snaps += sys.argv[2:]
for p in snaps:
    s = pickle.load(open(p, 'rb'))
    m = s['mem']
    TS = list(struct.unpack_from('<64I', m, 0x9a0)); si = m[0xaa0] & 63; sj = m[0xaa1] & 63
    last = TS[(sj-1)&63]
    found = []
    for n in pos.get(last, []):
        if (j0 + n) & 63 != sj: continue
        # verify full table: replay state after n
        ok = all(TS[(j0 + n - 1 - k) & 63] == out[n-k] for k in range(64) if n-k >= 1)
        if ok: found.append(n)
    print(os.path.basename(p), 'tick', s['tick'], 'i,j', si, sj, 'calls', found)

import sys
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re'); sys.path.insert(0, sys.argv[1])
import numpy as np, m, rep
M = m.load('r_after_init')
sq = [m.i32(M, 0x21455 + 4*k) for k in range(512)]
print('square table after init?', sum(sq[k] == (k-256)**2 for k in range(512)))
sb = m.d32(M, 0x1eded)
S = np.array(M[sb:sb+0x20000]).reshape(512, 256)
bad1 = set(); bad2 = set()
for ch in range(65):
    k = ch if ch <= 32 else ((ch - 32) >> 1) + ((ch - 32) >> 3)
    for cl in range(256):
        mul = min(255, (cl*ch) >> 5)
        if S[256 + ch, cl] != mul: bad2.add(ch)
        if cl < 64: v = max(0, cl + 2*k - 64)
        else: v = max(0, (cl & 31) + k - 32) + (cl & 0xe0)
        if S[ch, cl] != v: bad1.add(ch)
print('shade bad rows', sorted(bad1), 'mul bad rows', sorted(bad2))
print('rows 65.. nonzero', np.count_nonzero(S[65:256]), np.count_nonzero(S[256+65:]))
for r in (0, 16, 32, 33, 40, 64):
    print(r, list(S[r, :8]), list(S[r, 60:70]), list(S[r, 0x40:0x48]), list(S[r, 0xe0:0xe4]))
# compare with t120
T = m.load('t120')
sb2 = m.d32(T, 0x1eded)
S2 = np.array(T[sb2:sb2+0x20000]).reshape(512, 256)
rows = sorted(set(np.nonzero((S != S2).any(axis=1))[0].tolist()))
print('rows changed by t120:', rows[:20], len(rows))

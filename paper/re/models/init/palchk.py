import sys, struct, pickle
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re'); sys.path.insert(0, sys.argv[1])
import rep, m
import numpy as np
S = rep.SEG
sinT = None
def sin_tab(M):
    return [m.i32(M, 0x22479 + 4*k) for k in range(1024)]
def pal14dc(A, sh, st):
    s = st[A & 0x3ff] >> sh
    c = st[(A + 0x100) & 0x3ff] >> sh
    R = (0x80 - s) & 0xff
    G = (0x80 + (s >> 1)) & 0xff
    B = G
    d = (c * 0xde) >> 8
    G = (G + d) & 0xff
    B = (B - d) & 0xff
    out = []
    for ch in range(128):
        out += [(R*ch) >> 9, (G*ch) >> 9, (B*ch) >> 9]
    for ch in range(128, 0, -1):
        bl = ((0x80 - ch) & 0xff) >> 1
        out += [(((R*ch) >> 9) + bl) & 0xff, (((G*ch) >> 9) + bl) & 0xff, (((B*ch) >> 9) + bl) & 0xff]
    return bytes(out), (R, G, B)
def upload(pal, level):
    L = level >> 8
    o = []
    if L <= 0x80:
        for p in pal: o.append(((p * L) >> 7) & 0xff)
    else:
        L = min(L, 0xff); b = L - 0x80; bh = 0x7f - b
        for p in pal: o.append(((((p * bh) >> 6) & 0xff) + b) >> 1 & 0xff)
    return bytes(o)
import glob
for f in sorted(glob.glob(sys.argv[1] + '/../snaps/t*.snap'), key=lambda s: int(s.split('/t')[-1].split('.')[0])):
    sn = pickle.load(open(f, 'rb'))
    mem = sn['mem']
    M = np.frombuffer(mem, np.uint8)
    st = sin_tab(M)
    work = bytes(M[0x1eaad:0x1eaad+768])
    A = m.i32(M, 0x14dd); sh = int(M[0x1505])
    lvl = m.i32(M, 0x1edb5)
    best = None
    for dA in (0, -1, -2):
        p, rgb = pal14dc(A + dA, sh, st)
        if p == work: best = dA
    isdef = work == S[0x169a3:0x169a3+768]
    up = upload(work, lvl) == sn['pal']
    print('tick %5d eff %x angle %d shift %d level %x target %x step %x | default=%s 14dc(dA=%s) dac==upload(work,level): %s ent127 %s 143 %s' % (
        sn['tick'], m.d32(M, 0x1e5ef), A, sh, lvl, m.i32(M, 0x1edbd), m.i32(M, 0x1edb9), isdef, best, up, tuple(work[127*3:128*3]), tuple(work[143*3:144*3])))

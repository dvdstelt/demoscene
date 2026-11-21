"""Model of the flare sprite (0x415878 with edx = 1, called as 0x415dc8), checked pixel by pixel against the writes
run_temple.py --pixframes logged. Inputs per flare: the object's camera-space position x128, y128, z128 (floats at
+0xf8, +0xfc, +0x100, written by 0x4157d4 from the ints at +0xb8..+0xc0)."""
import math, struct, sys, collections
import numpy as np
import dumpread as D
from tables import shd, fl

SPR = fl.reshape(104, 128)
FX, FY = 21760, 18133            # [0x4b9014], [0x4b8fec]: trunc(0.4 * 320 * 170), trunc(0.5333 * 200 * 170)
CX16, CY16 = 160 << 4, 100 << 4
NEAR, FAR = 0x2000 // 128, 0x400000


def trunc(x): return int(math.trunc(x))


def draw(buf, X128, Y128, Z128, sA=1.0, s=0.25, w=128, h=104, pitch=128, writes=None):
    """buf: 200x320 uint8 indices, modified in place (dst = flare.shd[texel][dst])"""
    Z = Z128 * (1 / 128)
    if NEAR > Z or FAR < Z:
        return
    Xs = X128 * sA * (FX / 1024) / Z            # offsets from the centre in 1/16 pixel
    Ys = Y128 * sA * (FX / 1024) / Z            # note: the x focal length, also for y
    W = w * s * FX / Z                          # 1/16 pixel
    H = h * s * FY / Z
    if W <= 0 or H <= 0:
        return
    yc = CY16 + Ys
    Hi = trunc(H)
    y0 = trunc(yc - H * 0.5); y1 = y0 + Hi
    Wi = trunc(W)
    du = 16 * w / Wi; dv = 16 * h / Hi           # 0x40ba78 truncates the copy of W in place: du uses trunc(W)
    x0 = trunc(CX16 + Xs - W * 0.5); x1 = x0 + Wi
    v_start = 0.0; u_start = 0.0
    if y0 < 0:
        v_start = -(y0 - 0) * dv * 0.0625; y0 = 0
    if x0 < 0:
        u_start = -(x0 - 0) * du * 0.0625; x0 = 0
    yf, xf = y0 & 15, x0 & 15                   # y0, x0 are not negative here
    y0p, y1p, x0p, x1p = int(y0 / 16), int(y1 / 16), int(x0 / 16), int(x1 / 16)
    x1p = min(x1p, 320); y1p = min(y1p, 200)
    if x1p <= x0p or y0p >= y1p:
        return
    u0 = du * (15 - xf) * 0.0625 + u_start
    v = v_start + (15 - yf) * dv * 0.0625
    ustep = trunc(du * 65536); u0i = trunc(u0 * 65536)
    for y in range(y0p, y1p):
        row = SPR[trunc(v)] if trunc(v) < 104 else None
        u = u0i
        for x in range(x0p, x1p):
            t = row[u >> 16] if row is not None else 0
            old = buf[y, x]
            buf[y, x] = shd[t, old]
            if writes is not None: writes.append((y * 320 + x, int(buf[y, x]), int(old)))
            u += ustep
        v += dv


if __name__ == '__main__':
    d = D.load(sys.argv[1])
    for rec in d:
        if 'pix' not in rec: continue
        m = D.Blocks(rec)
        logged = collections.defaultdict(list)
        for f, off, size, val, old in rec['pix']: logged[f].append((off, val, old[0]))
        flares = [m.d32(0x425e78 + 4 * i) for i in range(10)]
        tot = bad = 0
        for o in flares:
            face = o + 0xc8
            got = logged.get(face, [])
            X, Y, Z = (struct.unpack('<f', m.rd(o + a, 4))[0] for a in (0xf8, 0xfc, 0x100))
            buf = np.zeros((200, 320), np.uint8)
            for off, val, old in got: buf.flat[off] = old        # the pixels as they were before this flare
            w = []
            draw(buf, X, Y, Z, writes=w)
            same = len(w) == len(got) and all(a == (b[0], b[1], b[2]) for a, b in zip(w, got))
            tot += 1; bad += not same
            print(rec['frame'], 'flare', hex(o), 'cam xyz/128 %.1f %.1f %.1f' % (X / 128, Y / 128, Z / 128),
                  'logged writes', len(got), 'model writes', len(w), 'identical' if same else 'DIFFERENT')
        print('frame', rec['frame'], tot - bad, 'of', tot, 'flares reproduced exactly')

"""Model of Craft's grid filler 0x402970 (exact integer pipeline) and of the tables it reads.

  tables(snap)            -> T (0x420854, 512 bytes), Wt (0x420a54, 16x16x4 floats), tex ([0x4207c8], 0x10101 dwords)
  t_table(), w_table()    -> the same tables computed from their formulas (0x40347a, 0x4033ee)
  fill(grid, tex, T)      -> uint32 (160, 320): rows 20..179 of the frame, 0x00RRGGBB
  grid: 41 x 21 entries of three 16-bit words (u, v, light), row-major, 246 bytes per row (0xea57b4)

usage: python3 models/grid/filler.py   (from clone/re; needs dump/grid/p*.pkl from run_clone.py --dump)
       checks the tables against the snapshots and the filler against every dumped frame (bit-exact).
"""
import math
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
DUMP = os.path.join(RE, 'dump', 'grid')


def tables(snap_path):
    """T, weights and the grid texture from a snapshot (cached in dump/grid)."""
    cache = os.path.join(DUMP, os.path.basename(snap_path) + '.tables.npz')
    if os.path.exists(cache):
        z = np.load(cache)
        return z['T'], z['W'], z['tex']
    import unicorn  # noqa: F401  (the pickle needs it)
    s = pickle.load(open(snap_path, 'rb'))
    mem = s['mem']
    T = np.frombuffer(mem[0x420854:0x420854 + 512], np.uint8).copy()
    W = np.frombuffer(mem[0x420a54:0x420a54 + 4096], '<f4').reshape(16, 16, 4).copy()
    tp = struct.unpack_from('<I', mem, 0x4207c8)[0]
    tex = np.frombuffer(mem[tp:tp + 0x10101 * 4], '<u4').copy()
    np.savez(cache, T=T, W=W, tex=tex)
    return T, W, tex


def t_table():
    """0x40347a: T[i] = min(255, trunc(e^((i - 256) / 23)))  (0.0434782609 = 1/23 as a double)."""
    k = 1 / 23
    return np.array([min(255, int(math.exp((i - 256) * k))) for i in range(512)], np.uint8)


def w_table():
    """0x4033ee: entry [j][i] (j = v fraction >> 4, i = u fraction >> 4) = the four bilinear weights x 256:
    (16-i)(16-j) for (u,v), (16-i)j for (u,v+1), i(16-j) for (u+1,v), ij for (u+1,v+1)."""
    W = np.zeros((16, 16, 4), np.float32)
    for j in range(16):
        for i in range(16):
            W[j, i] = ((16 - i) * (16 - j), (16 - i) * j, i * (16 - j), i * j)
    return W


def sx16(x):
    x = np.asarray(x, np.int64) & 0xffff
    return x - ((x & 0x8000) << 1)


def fill(grid, tex, T):
    g = np.frombuffer(grid, '<u2').reshape(21, 41, 3).astype(np.int64)
    u = sx16(g[:, :, 0])            # 0x40298c: sign-extended
    v = sx16(g[:, :, 1])
    l = g[:, :, 2]                  # 0x402a43: zero-extended
    texc = tex.astype(np.int64)
    ch = [(texc >> 16) & 255, (texc >> 8) & 255, texc & 255]
    out = np.zeros((160, 320), np.uint32)

    def edge(a):
        """left / right edge start values and per-line steps for all 20 x 40 blocks."""
        tl, bl, tr, br = a[:-1, :-1], a[1:, :-1], a[:-1, 1:], a[1:, 1:]
        return tl, sx16(bl - tl) >> 3, tr, sx16(br - tr) >> 3

    uL0, uLs, uR0, uRs = edge(u)
    vL0, vLs, vR0, vRs = edge(v)
    lL0, lLs, lR0, lRs = edge(l)
    for j in range(8):
        uL, uR = uL0 + j * uLs, uR0 + j * uRs
        vL, vR = vL0 + j * vLs, vR0 + j * vRs
        lL, lR = lL0 + j * lLs, lR0 + j * lRs
        du = sx16(uR - uL) >> 3
        dv13 = (sx16(vR - vL) << 13) & 0xffffffff
        step = (dv13 & 0xffff0000) | (du & 0xffff)                 # 0x402af4..0x402af7: packed v:u step
        ebx = ((vL << 16) | (uL & 0xffff)) & 0xffffffff
        dl = sx16(lR - lL) >> 3
        edi = lL
        for i in range(8):
            ui, vi = (ebx >> 8) & 255, ebx >> 24
            fu, fv = (ebx >> 4) & 15, (ebx >> 20) & 15
            idx = ui * 256 + vi
            w0, w1, w2, w3 = (16 - fu) * (16 - fv), (16 - fu) * fv, fu * (16 - fv), fu * fv
            L = (edi & 0xffffffff) >> 8
            pix = 0
            for k, sh in ((0, 16), (1, 8), (2, 0)):
                c = ch[k]
                s = (w0 * c[idx] + w1 * c[idx + 1] + w2 * c[idx + 256] + w3 * c[idx + 257]) >> 8
                pix = pix | (T[L + s].astype(np.int64) << sh)
            out[j::8, i::8] = pix                                     # block rows x block columns
            ebx = (ebx + step) & 0xffffffff
            edi = edi + dl
    return out


def page_rows(page, y0=20, y1=180):
    return np.frombuffer(page, '<u4').reshape(200, 320)[y0:y1]


def main():
    snaps = {2: 't2316.snap', 5: 't6156.snap', 4: 't9324.snap'}
    for part, sn in snaps.items():
        T, W, tex = tables(os.path.join(RE, 'snaps', sn))
        print('part %d (%s): T table formula %s, weights formula %s' % (
            part, sn, 'exact' if np.array_equal(T, t_table()) else 'DIFFERS',
            'exact' if np.array_equal(W, w_table()) else 'DIFFERS'))
        p = os.path.join(DUMP, 'p%d.pkl' % part)
        if not os.path.exists(p): continue
        recs = [r for r in pickle.load(open(p, 'rb')) if r['part'] == part and 'grid' in r]
        ok = 0
        worst = 0
        for r in recs:
            ref = page_rows(r['filled'])
            got = fill(r['grid'], tex, T)
            n = int((ref != got).sum())
            ok += n == 0
            worst = max(worst, n)
        print('  filler: %d of %d dumped frames bit-exact (worst: %d pixels differ)' % (ok, len(recs), worst))


if __name__ == '__main__':
    main()

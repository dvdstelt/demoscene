"""Model of the radial zoom part 0x177bf (radius remap table, 0x16d43 warp, 0x17979/0x17999 white fades), checked
against frames recorded by hook_generic.py (out/zoom.cfg, run from snaps/t5205.snap) and full.frames.

usage: python3 zoom_model.py out/zoom.hook.pkl
"""
import math
import pickle
import struct
import sys

import numpy as np

import common as c
import polar
from jizz_model import darken

f32 = np.float32
TWO_PI_16K = struct.unpack('<d', c.rep.SEG[0x11596:0x1159e])[0]
K07 = f32(struct.unpack('<f', c.rep.SEG[0x179e2:0x179e6])[0])     # 0.7
K60 = f32(60.0)
K008 = f32(struct.unpack('<f', c.rep.SEG[0x179ee:0x179f2])[0])    # 0.008


def radius_table(t):
    """0x178ac..0x17900: T[r] = low byte of round(60 * (sin(phase + 0.008*(r+1)) + 0.7) + r)."""
    t &= 0xffffffff
    ts = t - (1 << 32) if t >= 1 << 31 else t
    p = (ts * -0x02066df7) >> 32                     # imul 0xfdf99209: edx
    st = f32(f32(p * TWO_PI_16K))
    out = []
    for i in range(256):
        st = f32(st + K008)
        v = f32(f32(f32(math.sin(st)) + K07) * K60)
        out.append(int(np.rint(f32(v + i))) & 255)
    return np.array(out, np.uint8)


def offsets(t, flip):
    """0x17902..0x17923: word [0x16cd1] = (radius offset << 8) | angle offset."""
    e = ((t & 0xffffffff) * 3) & 0xffffffff
    ro = ((e >> 15) & 255) ^ (flip & 255)
    ao = (e >> 16) & 255
    return ro << 8 | ao


def warp_r(tab, offset, rtab, add, tex16):
    """0x16d43 with [0x16d42] = 4: the table is indexed by the radius byte and replaces it."""
    row, col = divmod(offset // 4, 600)
    e = tab[row:row + 200, col:col + 320].astype(np.int64)
    a = e & 255
    r = rtab.astype(np.int64)[(e >> 8) & 255]
    idx = ((r << 8 | a) + add) & 0xffff
    return tex16[idx]


def fade_level(t, t0, K):
    """0x17999: (0x3ff - ((t - t0) * K >> 32)) clamped at 0; 0 = fade over (0x179f6 no longer called)."""
    dt = (t - t0) & 0xffffffff
    if dt >= 1 << 31: return 0
    lv = 0x3ff - ((dt * K) >> 32)
    return max(lv, 0)


def main():
    F = pickle.load(open(sys.argv[1], 'rb'))
    s = c.load_snap(5205)
    m = s['mem']
    tab = polar.table()
    texs = {}
    for p in (0x12425, 0x12429):
        b = c.u32(m, p)
        texs[b] = np.frombuffer(m[b:b + 0x20000], '<u2')
    for name, src in (('map5', 0x655e), ('map7', 0x6566)):
        mb = c.u32(m, src)
        pl = np.frombuffer(m[mb:mb + 0x30000], np.uint8).reshape(3, 65536).astype(np.uint32)
        conv = ((pl[0] >> 3) << 11) | ((pl[1] >> 2) << 5) | (pl[2] >> 3)
        print(name, 'converted = one of the textures:', any((conv == t).all() for t in texs.values()))
    start = (-c.i32(m, 0x6542)) & 0xffffffff
    fl, last = {}, {}
    for n, tk, tm, t, part in c.flips(): fl[t] = tk; last[tk] = t   # only the last flip of a tick is captured
    bad = [0, 0, 0, 0]
    for f in F[1:]:
        tl = f['timers']
        texp, flip = struct.unpack('<Ii', f['tex'])
        t0, lv0, K = struct.unpack('<iiI', f['fade'])
        k = 0
        if tl[0][0] == 0x17983:                        # a fade was started this frame
            k = 1
        assert tl[k][0] == 0x178b1 and tl[k + 1][0] == 0x17907
        t1 = (tl[k][1] - start) & 0xffffffff
        t2 = (tl[k + 1][1] - start) & 0xffffffff
        R = radius_table(t1)
        e_tab = int((R != np.frombuffer(f['tab'][:256], np.uint8)).sum())
        add = offsets(t2, flip)
        e_add = int(add != struct.unpack('<H', f['add'][:2])[0])
        w = warp_r(tab, 0x29270, R, add, texs[texp])
        e_w = int((w.ravel() != np.frombuffer(f['warp'], '<u2')).sum())
        page = w
        if lv0 != 0:
            t3 = (tl[k + 2][1] - start) & 0xffffffff
            assert tl[k + 2][0] == 0x179a7
            lv = fade_level(t3, t0, K)
            page = darken(w, lv >> 5, (31, 31, 31))     # also in the frame in which the level reaches 0
        e_p = int((page.ravel() != np.frombuffer(f['page'], '<u2')).sum())
        t0f = f['timers'][0][1]
        cap = c.frame565(fl[t0f]) if t0f in fl and last[fl[t0f]] == t0f else None
        e_c = int((page != cap).sum()) if cap is not None else -1
        for i, e in enumerate((e_tab, e_add, e_w, e_p)): bad[i] += e != 0
        print(f"tick {f['tick']} t {t1}: table {e_tab} offsets {e_add} warp {e_w} page {e_p} capture {e_c}"
              + (f'  fade start K={K}' if k else ''))
    print('frames with differences (table, offsets, warp, page):', bad, 'of', len(F) - 1)


if __name__ == '__main__':
    main()

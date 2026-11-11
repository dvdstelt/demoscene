"""Model of the field lines part 0x17c8a (table 0x17b2f, six poles, per-pixel signed sum), checked against frames
recorded by hook_generic.py (out/field.cfg, run from snaps/t5970.snap) and full.frames.

usage: python3 field_model.py out/field.hook.pkl
"""
import math
import pickle
import struct
import sys

import numpy as np

import common as c
from jizz_model import darken
from zoom_model import fade_level

f32 = np.float32
SEG = c.rep.SEG
FREQ = [struct.unpack_from('<f', SEG, 0x17c5a + 4 * k)[0] for k in range(12)]   # fx1, fy1, fx2, fy2, ...
ADD = [2, 10, 13, 19, 12, 15]                                                     # immediates in 0x17d00..0x17f60
AX, AY, STRIDE = struct.unpack_from('<III', SEG, 0x17c4e)                         # 124, 67, 600


def field_table():
    """0x17b2f: 600 x 340 dwords, x = i - 299.5, y = j - 169.5:
    low word  = round(atan2(y, x) * 10430.4 * 2) & 0xffff   (65536 per half turn)
    high word = round(45.6 * sqrt(x^2 + y^2) / (0.5 * (sin(3 atan2(y, x)) + 1) + 0.4)) & 0xffff"""
    j, i = np.mgrid[0:340, 0:600]
    x = (i - 299.5).astype(np.float32)
    y = (j - 169.5).astype(np.float32)
    th = np.arctan2(y.astype(np.float64), x.astype(np.float64))
    a = (th.astype(np.float32) * f32(SEG and struct.unpack_from('<f', SEG, 0x17c1a)[0])).astype(np.float32) * f32(2)
    ang = np.rint(a).astype(np.int64) & 0xffff
    rho = np.sqrt((x * x + y * y).astype(np.float32)).astype(np.float32)
    s = ((np.sin(3 * th) + 1).astype(np.float32) * f32(0.5) + f32(0.4)).astype(np.float32)
    r = (rho / s).astype(np.float32) * f32(struct.unpack_from('<f', SEG, 0x17c2a)[0])
    rad = np.rint(r.astype(np.float32)).astype(np.int64) & 0xffff
    return (ang | (rad << 16)).astype(np.uint32)


def tau_of(t, S):
    """0x17d00..0x17d37."""
    return ((((((t & 0xffffffff) >> 4) * S) & 0xffffffff) * 3) & 0xffffffff) >> 20


def poles(tau):
    """dword offsets of the six 320x200 windows in the table."""
    out = []
    for k in range(6):
        X = int(np.rint((f32(math.sin(f32(f32(tau) * f32(FREQ[2 * k])))) + 1) * AX))
        Y = int(np.rint((f32(math.sin(f32(f32(tau) * f32(FREQ[2 * k + 1])))) + 1) * AY))
        out.append((Y * STRIDE + X + ADD[k]) * 4)
    return out


def render(tab, offs, tex16):
    flat = tab.ravel()
    j, i = np.mgrid[0:200, 0:320]
    base = (j * 600 + i).astype(np.int64)
    E = [flat[base + o // 4].astype(np.int64) for o in offs]
    D = (E[0] + E[2] + E[4] - (0x12345678 + E[1] + E[3] + E[5])) & 0xffffffff
    idx = ((D >> 24) & 255) | (((D >> 8) & 255) << 8)
    return tex16[idx]


def main():
    F = pickle.load(open(sys.argv[1], 'rb'))
    s = c.load_snap(5970)
    m = s['mem']
    tabm = np.frombuffer(F[0]['tab'], '<u4').reshape(340, 600)       # grabbed when 0x17b2f returned
    tab = field_table()
    d = tab != tabm
    print('field table diffs', int(d.sum()), 'angle', int(((tab ^ tabm) & 0xffff != 0).sum()),
          'radius', int(((tab ^ tabm) >> 16 != 0).sum()))
    tab = tabm
    xb = c.u32(m, 0x12425)
    tex16 = np.frombuffer(m[xb:xb + 0x20000], '<u2')
    mb = c.u32(m, 0x6586)
    pl = np.frombuffer(m[mb:mb + 0x30000], np.uint8).reshape(3, 65536).astype(np.uint32)
    conv = ((pl[0] >> 3) << 11) | ((pl[1] >> 2) << 5) | (pl[2] >> 3)
    print('texture = map15 converted:', bool((conv == tex16).all()))
    start = (-struct.unpack('<i', F[1]['st'])[0]) & 0xffffffff
    fl, last = {}, {}
    for n, tk, tm, t, part in c.flips(): fl[t] = tk; last[tk] = t
    S = None
    bad = [0, 0, 0]
    for f in F[1:]:
        tl = f['timers']
        k = 1 if tl[0][0] == 0x1797e + 5 else 0
        assert tl[k][0] == 0x17d05, [hex(r) for r, v, t_ in tl]
        t = (tl[k][1] - start) & 0xffffffff
        Srec = struct.unpack('<I', f['S'])[0]
        if S is None: S = Srec
        else: S = max(S - 3, 0x200) if S != 0x200 else S
        tau = tau_of(t, S)
        e_tau = int(tau != struct.unpack('<I', f['tau'])[0]) + int(S != Srec)
        offs = poles(tau)
        code = f['code']
        rec = [struct.unpack_from('<I', code, a - 0x17f85)[0] for a in (0x17f8c, 0x17f92, 0x17f98, 0x17f9e, 0x17fa4, 0x17faa)]
        e_off = sum(a != b for a, b in zip(offs, rec))
        img = render(tab, offs, tex16)
        e_pre = int((img.ravel() != np.frombuffer(f['pre'], '<u2')).sum())
        t0, lv0, K = struct.unpack('<iiI', f['fade'])
        page = img
        if lv0 != 0:
            t3 = [v for r, v, _ in tl if r == 0x179a7][0]
            lv = fade_level((t3 - start) & 0xffffffff, t0, K)
            page = darken(img, lv >> 5, (31, 31, 31))
        e_p = int((page.ravel() != np.frombuffer(f['page'], '<u2')).sum())
        t0f = tl[0][1]
        cap = c.frame565(fl[t0f]) if t0f in fl and last[fl[t0f]] == t0f else None
        e_c = int((page != cap).sum()) if cap is not None else -1
        for i, e in enumerate((e_tau, e_off, e_p)): bad[i] += e != 0
        print(f"tick {f['tick']} t {t} S {S} tau {tau}: tau/S {e_tau} offsets {e_off} image {e_pre} page {e_p} capture {e_c}")
    print('frames with differences (tau/S, offsets, page):', bad, 'of', len(F) - 1)


if __name__ == '__main__':
    main()

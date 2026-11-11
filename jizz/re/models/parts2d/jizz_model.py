"""Model of the logo part 0x1212a (0x123df star table, 0x16dc8 warp, brightness + 0x179f6, 0x1239b logo), checked
against frames recorded by hook_generic.py (out/jizz.cfg, run from snaps/t4050.snap) and full.frames.

usage: python3 jizz_model.py out/jizz.hook.pkl
"""
import math
import pickle
import struct
import sys

import numpy as np

import common as c
import polar

f32 = np.float32
TWO_PI_256 = f32(struct.unpack('<f', c.rep.SEG[0x141e2:0x141e6])[0])          # 0.0245437
TWO_PI_16K = struct.unpack('<d', c.rep.SEG[0x11596:0x1159e])[0]


def star_table(A, B, C):
    """0x123df: 256 bytes, T[i] = low byte of round(B * sin((i*A + C) * 2pi/256))."""
    out = []
    for i in range(256):
        v = f32(f32(i * A + C) * TWO_PI_256)
        out.append(int(np.rint(f32(math.sin(v)) * B)) & 255)
    return np.array(out, np.uint8).view(np.int8)


def level(t, t_ev, pulse):
    """0x122dc..0x1236c: brightness parameter for 0x179f6 (0 = full, larger = darker)."""
    t &= 0xffffffff
    a = t >> 6
    w = f32(math.sin(f32(a * TWO_PI_16K))) * 5
    b = ((a * 5) & 0xffffffff) >> 2
    w = f32(w + f32(math.cos(f32(b * TWO_PI_16K))) * 6)
    lv = int(np.rint(w)) + 13
    if pulse:
        dt = (t - t_ev) & 0xffffffff
        k = 0x3ff - (dt >> 10)
        if k < 0: k = 0
        lv -= k >> 5
        if lv < 0: lv = 0
    return lv


def darken(page, lv, target=(0, 0, 0)):
    """0x179f6 with the tables of 0x1131b (target colour 0,0,0): each 5-bit channel c -> (c*(31-lv) + T*lv) >> 5
    (green: its top 5 bits, result back at bit 6, so the green LSB becomes 0)."""
    p = page.astype(np.uint32)
    r, g, b = (p >> 11) & 31, (p >> 6) & 31, p & 31
    f = lambda ch, T: (ch * (31 - lv) + T * lv) >> 5
    return ((f(r, target[0]) << 11) | (f(g, target[1]) << 6) | f(b, target[2])).astype(np.uint16)


def logo_over(page, logo):
    """0x1239b: logo rows 30..159 (non-zero pixels) onto page rows 50..179."""
    out = page.copy()
    src = logo[30:160]
    dst = out[50:180]
    nz = src != 0
    dst[nz] = src[nz]
    return out


def main():
    F = pickle.load(open(sys.argv[1], 'rb'))
    s = c.load_snap(4050)
    m = s['mem']
    tab = polar.table()
    tb = c.u32(m, 0x12425)
    tex16 = np.frombuffer(m[tb:tb + 0x20000], '<u2')
    # the texture is map5 ([0x655e]) converted by 0x13d73: (R>>3)<<11 | (G>>2)<<5 | B>>3
    mb = c.u32(m, 0x655e)
    pl = np.frombuffer(m[mb:mb + 0x30000], np.uint8).reshape(3, 65536).astype(np.uint32)
    conv = ((pl[0] >> 3) << 11) | ((pl[1] >> 2) << 5) | (pl[2] >> 3)
    print('texture = map5 converted:', bool((conv == tex16).all()))
    lb = c.u32(m, 0x658e)
    logo = np.frombuffer(m[lb:lb + 128000], '<u2').reshape(200, 320)
    start = (-c.i32(m, 0x6542)) & 0xffffffff
    print('part start timer', start)
    fl = {}
    for n, tk, tm, t, part in c.flips(): fl[t] = tk
    A, B, S = struct.unpack_from('<iii', c.rep.SEG, 0x1238e)
    pulse, t_ev = 0, 0
    tot = [0, 0, 0, 0]
    for f in F[1:]:
        tl = f['timers']
        ev = None
        A2, B2, S2 = struct.unpack_from('<iii', f['par'])
        if (A2, B2, S2) != (A, B, S) or f['par'][12] != pulse:
            A, B, S = A2, B2, S2
            pulse = f['par'][12]
            t_ev = (tl[0][1] - start) & 0xffffffff
            ev = 'event: A=%d B=%d S=%d' % (A, B, S)
            assert tl[0][0] == 0x12285
            tl = tl[1:]
        assert tl[0][0] == 0x1229c and tl[1][0] == 0x122e1
        t1 = (tl[0][1] - start) & 0xffffffff
        t2 = (tl[1][1] - start) & 0xffffffff
        C = ((t1 * 5) & 0xffffffff) >> 13
        C ^= S & 0xffffffff
        C = C - (1 << 32) if C >= 1 << 31 else C
        T = star_table(A, B, C)
        e_tab = int((T.view(np.uint8) != np.frombuffer(f['tab'][:256], np.uint8)).sum())
        w = polar.warp(tab, 0x29270, T, 0, tex16)
        e_warp = int((w.ravel() != np.frombuffer(f['warp'], '<u2')).sum())
        lv = level(t2, t_ev, pulse)
        e_lv = int(lv != f['1236e_eax'])
        page = logo_over(darken(w, lv), logo)
        e_page = int((page.ravel() != np.frombuffer(f['page'], '<u2')).sum())
        t0 = f['timers'][0][1]
        cap = c.frame565(fl[t0]) if t0 in fl else None
        e_cap = int((page != cap).sum()) if cap is not None else -1
        for k, e in enumerate((e_tab, e_warp, e_lv, e_page)): tot[k] += e != 0
        print(f"tick {f['tick']} t {t1} C {C} level {lv}: table diff {e_tab} warp diff {e_warp} level diff {e_lv} "
              f"page diff {e_page} capture diff {e_cap}" + (' ' + ev if ev else ''))
    print('frames with differences (table, warp, level, page):', tot, 'of', len(F) - 1)


if __name__ == '__main__':
    main()

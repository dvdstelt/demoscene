"""Model of the cloud part (precalc 0x1241b / 0x1259c / 0x124eb, draw 0x125f4, PIT callback 0x1278e).

usage: python3 cloud_model.py            checks the table against the snapshot and frames against full.frames
"""
import sys

import numpy as np

from common import out, file, frame, snap, ru32, rd32, ticks, save_png

SIN = np.frombuffer(file(33), np.int16).astype(np.int64)       # 2200 entries, amplitude 16383
TEX = np.frombuffer(file(30), np.uint8)[:65536].astype(np.int64)  # 256x256, values 0..15


def table(garbage=None):
    """0x1259c: per screen pixel a word angle << 8 | depth (circles of radius 2..200, first hit wins)."""
    T = np.zeros(64000, np.int64) if garbage is None else garbage.astype(np.int64).copy()
    hit = np.zeros(64000, bool)
    for n in range(1, 200):                # [0x125ea] = n before the increment
        depth = ((0x1400 // (n + 0x13)) << 8) & 0xffff      # 16-bit: n = 1 gives 256 << 8 = 0
        R = n + 1                                           # radius passed to 0x124eb (after the increment)
        acc = 0
        for k in range(2200):
            ang = (acc >> 16) & 0xffff
            acc += 0x1dca
            y = (((SIN[k] * R) >> 14) + 100) & 0xffff        # shrd ax, dx, 14 then 16-bit add
            if y >= 200:
                continue
            c = SIN[(k + 0x226) % 0x898]
            x = (((-c & 0xffff) - (0x10000 if (-c & 0xffff) >= 0x8000 else 0)) * R >> 14) + 0xa0
            x &= 0xffff
            if x >= 320:
                continue
            p = y * 320 + x
            if hit[p]:
                continue
            hit[p] = True
            w = (ang + depth) & 0xffff
            T[p] = ((w & 0xff) << 8) | (w >> 8)            # xchg ah, al: low byte depth, high byte angle
    return T, hit


def draw(T, S):
    """0x125f4 with the four offsets derived from the accumulated speed S (callback 0x1278e)."""
    o1e, o2, o6, oa = 0x14000 + S, 0x23000 + S, -S, -0xa000 - S
    def s16(v): v &= 0xffff; return v - 0x10000 if v & 0x8000 else v
    si = ((s16(o2 >> 10) >> 1) << 8) + (o1e >> 10)
    si &= 0xffff
    dx = ((s16(oa >> 10) >> 1) << 8) + (o6 >> 10)
    di = (dx << 1) & 0xffff
    out = np.zeros(64000, np.int64)
    p = np.arange(320, 320 + 63040)
    t = T[p]
    c1 = TEX[(t + si) & 0xffff]
    odd = p | 1                                   # each pixel pair reads layer 2 at the odd pixel's table word
    base = (T[odd] & 0xfffe) + di
    c2 = TEX[(base + (p & 1)) & 0xffff]
    out[p] = c1 + (c2 << 4)
    return out.reshape(200, 320)


if __name__ == '__main__':
    s = snap('f5421_126ae.snap')
    mem = s['mem']
    tp = ru32(mem, 0x11d8a)
    snapT = np.frombuffer(mem[tp:tp + 128000], np.uint16).astype(np.int64)
    T, hit = table(garbage=snapT)
    print('pixels never hit:', int((~hit).sum()), np.argwhere(~hit.reshape(200, 320))[:20].tolist())
    print('table equal to snapshot:', bool((T == snapT).all()), int((T != snapT).sum()))
    np.save(out('cloud_table.npy'), T)
    np.save(out('cloud_hit.npy'), hit)

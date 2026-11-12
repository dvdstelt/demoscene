"""Model of the picture warp 0x975b (curves 0x9989 / 0x9a02, patch fill 0x9a62, textures 0x9d95).

Reads the parameters the hook logged at 0x9a62 (run_hook.py --hook 0x9a62:9458/48,9734/8,9618/24,4a7a0/16
--hook 0x9d14:), rebuilds the four edge curves and the picture, and compares every completed frame (the record
after the 0x9d14 hook) with the capture.

usage: python3 warp_model.py runs/warp [--save F,F,...]
"""
import sys

import numpy as np

from common import *

SIN = sintab()
M32 = 0xffffffff


def textures():
    """0x9d95: centre 256 columns (32..287) of pictures 11, 12, 13, rows stretched 200 -> 256 (nearest)."""
    out = []
    for n in (11, 12, 13):
        d = ofile(n)
        pic = np.frombuffer(d[0x300:0x300 + 64000], np.uint8).reshape(200, 320)
        rows = (np.arange(256) * 0xc800) >> 16
        out.append(pic[rows, 32:288].reshape(-1).copy())
    return out


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


def curve(n, ph1, add, step, ph2, shift, amp, base):
    """0x9a02: n points; angle = (ph1 + add) << 20 stepped by `step` (2^-20 angle units); radius wobble."""
    pts = []
    e = ((ph1 + add) << 20) & M32
    for _ in range(n):
        a = e >> 20
        e = (e + step) & M32
        w = ((a << (shift & 31)) + ph2) & 0xfff
        r = s32((int(SIN[w]) * amp) & M32) >> 16
        r = s32(r + base)
        y = s32(int(SIN[a]) * r + 0x640000)
        x = s32(int(SIN[a + 1024]) * r + 0xa00000)
        pts.append((x, y))
    return pts


def curves(p):
    """0x9989: top (320), bottom (320, reversed angles), right (200), left (200, reversed)."""
    ph1, ph2, shift, amp, base = p['ph1'], p['ph2'], p['shift'] & 0xff, p['amp'], p['base']
    top = curve(320, ph1, 0, 0x333333, ph2, shift, amp, base)
    bot = curve(320, ph1, 0xc00, 0xffcccccd, ph2, shift, amp, base)
    right = curve(200, ph1, 0x400, 0x51eb85, ph2, shift, amp, base)
    left = curve(200, ph1, 0, 0xffae147b, ph2, shift, amp, base)
    return top, bot, right, left


def idiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def pack(x, y):
    """(x << 8) & 0xffff0000 | ((uint32) y >> 8) & 0xffff: u 8.8 in the high word, v 8.8 in the low word."""
    return ((x << 8) & 0xffff0000) | (((y & M32) >> 8) & 0xffff)


def pstep(dx, dy, n):
    sx, sy = idiv(dx, n), idiv(dy, n)
    v = ((sx << 8) & 0xffff0000) | (((sy & M32) >> 8) & 0xffff)
    if v & 0x8000:
        v = (v + 0x10000) & M32
    return v


def draw(top, bot, right, left, u0, v0, tex):
    avg = lambda a, b: (s32(a[0] + b[0]) >> 1, s32(a[1] + b[1]) >> 1)
    TL, TR = avg(top[0], left[0]), avg(top[319], right[0])
    BL, BR = avg(bot[0], left[199]), avg(bot[319], right[199])
    acc = np.array([pack(*top[x]) for x in range(320)], np.int64)
    stp = np.array([pstep(bot[x][0] - top[x][0], bot[x][1] - top[x][1], 200) for x in range(320)], np.int64)
    Lx, Ly, Rx, Ry = TL[0], TL[1], TR[0], TR[1]
    dLx, dLy = idiv(BL[0] - TL[0], 200), idiv(BL[1] - TL[1], 200)
    dRx, dRy = idiv(BR[0] - TR[0], 200), idiv(BR[1] - TR[1], 200)
    off = ((u0 << 16) | (v0 & 0xffff)) & M32
    out = np.zeros((200, 320), np.uint8)
    xs = np.arange(320, dtype=np.int64)
    for y in range(200):
        ldx, ldy = s32(left[y][0] - Lx), s32(left[y][1] - Ly)
        rdx, rdy = s32(right[y][0] - Rx), s32(right[y][1] - Ry)
        start = pack(ldx, ldy)
        h = pstep(s32(rdx - ldx), s32(rdy - ldy), 200)
        ebp = (start + off) & M32
        acc = (acc + stp) & M32
        e = (acc + ebp + xs * h) & M32
        out[y] = tex[((e >> 8) & 0xff) * 256 + (e >> 24)]
        Lx, Ly, Rx, Ry = s32(Lx + dLx), s32(Ly + dLy), s32(Rx + dRx), s32(Ry + dRy)
    return out


def parse(r):
    m = r[11:]
    b = bytes.fromhex(m[0])
    v = [int.from_bytes(b[4 * i:4 * i + 4], 'little', signed=True) for i in range(12)]
    p = dict(u0=v[0], v0=v[1], base=v[2], d1=v[3], d2=v[4], shift=v[5], amp=v[6], ph1=v[7], ph2=v[8], cnt=v[11])
    if len(m) < 2:
        return p
    c = bytes.fromhex(m[1])
    p['pic'] = int.from_bytes(c[:4], 'little')
    p['cycle'] = int.from_bytes(c[4:6], 'little')
    p['cyc_acc'] = int.from_bytes(c[6:8], 'little')
    return p


def main():
    pre = sys.argv[1]
    saves = set()
    if '--save' in sys.argv:
        saves = {int(x) for x in sys.argv[sys.argv.index('--save') + 1].split(',')}
    frames = Frames(pre + '.frames')
    f0 = int(open(pre + '.ticks').readline() and [l for l in open(pre + '.ticks') if not l.startswith('#')][0].split()[0])
    tex = textures()
    lines = [l.split() for l in open(pre + '.hook') if l.startswith('9a62 ') or l.startswith('9d14 ')]
    n = ok = 0
    bad = []
    for i in range(len(lines) - 1):
        if lines[i][0] != '9a62' or lines[i + 1][0] != '9d14':
            continue
        p = parse(lines[i])
        fend = int(lines[i + 1][1])
        img = draw(*curves(p), p['u0'], p['v0'], tex[p['pic']])
        cap = frames.vga(fend - f0)
        d = int(np.count_nonzero(cap != img))
        n += 1
        ok += d == 0
        if d:
            bad.append((fend, d))
        if fend in saves:
            save('warp_%d_model.png' % fend, rgb(img, frames.pal(fend - f0)), 2)
            save('warp_%d_capture.png' % fend, rgb(cap, frames.pal(fend - f0)), 2)
    print('%d frames compared, %d exact' % (n, ok))
    print('differing:', bad[:40])


if __name__ == '__main__':
    main()

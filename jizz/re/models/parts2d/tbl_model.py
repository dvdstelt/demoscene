"""Model of the TBL part frame (0x12013 + 0x11986 + 0x11e33 + 0x13f98 + 0x11f73), checked against frames recorded
with hook_tbl.py (run_hook.py from snaps/t440.snap) and against full.frames.

usage: python3 tbl_model.py out/tbl440.tbl.pkl
"""
import math
import pickle
import struct
import sys

import numpy as np

import common as c

TWO_PI_16K = struct.unpack('<d', c.rep.SEG[0x11596:0x1159e])[0]      # 2*pi/16384
f32 = np.float32


def x87round(v):
    """fistp with the default rounding mode: round half to even."""
    return int(np.rint(v))


def light_pos(T):
    """0x11986..0x119ef: light position from the timer value T (= xmpGetTimer + [0x6542])."""
    T &= 0xffffffff
    a1 = ((T * 0x5f0ff0) >> 32) & 0xffff
    a2 = ((T * 0x84e770) >> 32) & 0xffff
    a1 = a1 - 65536 if a1 >= 32768 else a1                              # fild word: signed
    a2 = a2 - 65536 if a2 >= 32768 else a2
    lx = x87round(f32(math.sin(f32(a1 * TWO_PI_16K))) * -90 + 158)
    ly = x87round(f32(math.sin(f32(a2 * TWO_PI_16K))) * 13 + 98)
    return lx, ly


def bump(tex, light, lx, ly):
    """0x11e33: displaced lookup into the 256x256 light table, floor 30."""
    y, x = np.mgrid[0:200, 0:320]
    idx = y * 256 + x                       # rows of 256 bytes, 320 columns read: x >= 256 runs into the next row
    G = tex[1].ravel()[idx].astype(np.int32)
    B = tex[2].ravel()[idx].astype(np.int32)
    u = x - lx + 128
    v = y - ly + 128
    inside = (u >= 0) & (u <= 255) & (v >= 0) & (v <= 255)
    li = (((v + B) & 255) << 8) | ((u + G) & 255)
    out = np.where(inside, light.ravel()[li & 0xffff], 0)
    return np.maximum(out, 30).astype(np.uint8)


def line(buf, x0, y0, x1, y1, col=0xf9):
    """0x1122f + 0x1145c: 4-pixel thick line into the 320-wide byte buffer (flat, 0x10000 bytes)."""
    if not (0 <= x0 < 320 and 0 <= x1 < 320 and 0 <= y0 < 200 and 0 <= y1 < 200): return
    if y1 < y0: x0, y0, x1, y1 = x1, y1, x0, y0
    flag = 0
    ady = y1 - y0
    bdx = x1 - x0
    if bdx < 0: flag = 2; bdx = -bdx
    if ady > bdx: flag += 1
    if ady + bdx == 0: return
    e = y0 * 320 + x0
    if not flag & 1:
        s = (ady << 16) // bdx
        if s >= 0x10000: s = 0xffffffff
        s &= 0xffff
        acc = 0
        step = -1 if flag == 2 else 1
        for _ in range(bdx + 1):
            for k in range(4): buf[e + 320 * k] = col
            e += step
            acc += s
            if acc >= 0x10000: acc -= 0x10000; e += 320
    else:
        s = (bdx << 16) // ady
        if s >= 0x10000: s = 0xffffffff
        s &= 0xffff
        acc = 0
        for _ in range(ady + 1):
            buf[e:e + 4] = col
            if flag == 1:
                acc += s
                e += 320 + (acc >> 16)
                acc &= 0xffff
            else:
                borrow = acc < s
                acc = (acc - s) & 0xffff
                e += 320 - borrow


def fbits(v): return struct.unpack('<I', struct.pack('<f', v))[0]


def rays(angles, lx, ly, mask):
    """0x11acf..0x11c0a: per spark i = 1..40 the offset (dx, dy) and the ray end (ex, ey) (0 = no ray)."""
    out = []
    corners = []
    for cx, cy in ((316, 196), (0, 196), (0, 0), (316, 0)):
        corners.append(f32(math.atan2(cy - ly, cx - lx) % (2 * math.pi)))
    for i in range(1, 41):
        w1, w2 = struct.unpack_from('<hh', angles, 4 * i)
        dx = f32(math.sin(f32(w1 * TWO_PI_16K)) * 30.0)
        dy = f32(math.sin(f32(w2 * TWO_PI_16K)) * 30.0)
        ix, iy = x87round(dx), x87round(dy)
        ex = ey = 0
        if mask[(iy + ly) * 320 + ix + lx] != 0:
            ang = f32(math.atan2(dy, dx) % (2 * math.pi))
            b = fbits(ang)
            if b < fbits(corners[0]): k = f32((316 - lx) / dx)
            elif b < fbits(corners[1]): k = f32((196 - ly) / dy)
            elif b < fbits(corners[2]): k = f32((0 - lx) / dx)
            elif b < fbits(corners[3]): k = f32((0 - ly) / dy)
            else: k = f32((316 - lx) / dx)
            k = f32(k * f32(0.95))
            ex = x87round(f32(k * dx))
            ey = x87round(f32(k * dy))
        out.append((dx, dy, ex, ey))
    return out


def glow(buf, lx, ly, R):
    """0x11c73..0x11d10: additive saturating cone 255 - |d| * 255/R over the square [-R, R)^2."""
    sc = f32(f32(255) / f32(R))
    j, i = np.mgrid[-R:R, -R:R]
    d = np.sqrt((i * i + j * j).astype(np.float32)).astype(np.float32)
    v = np.maximum(255 - np.rint(d * sc).astype(np.int32), 0)
    b = buf[:64000].reshape(200, 320)
    blk = b[ly - R:ly + R, lx - R:lx + R].astype(np.int32)
    b[ly - R:ly + R, lx - R:lx + R] = np.minimum(blk + v, 255).astype(np.uint8)


def frame(T0, angles, hist, tex, light, mask):
    """One frame of 0x11986. Returns (work buffer before >>3, new history, displayed 8-bit buffer)."""
    lx, ly = light_pos(T0)
    R = (int(mask[ly * 320 + lx]) >> 4) + 15
    buf = np.zeros(0x10000, np.uint8)
    buf[:64000] = bump(tex, light, lx, ly).ravel()
    rs = rays(angles, lx, ly, mask)
    for i in range(40, 0, -1):                                   # loop 2: spark lines from the light
        dx, dy, ex, ey = rs[i - 1]
        line(buf, lx, ly, x87round(f32(dx + lx)), x87round(f32(dy + ly)))
    glow(buf, lx, ly, R)
    buf[:64000] = ((buf[:64000].astype(np.uint32) * mask[:64000]) >> 8).astype(np.uint8)
    for i in range(40, 0, -1):                                   # loop 3: rays from the spark point outwards
        dx, dy, ex, ey = rs[i - 1]
        if ex != 0:
            line(buf, x87round(f32(dx + lx)), x87round(f32(dy + ly)), lx + ex, ly + ey)
    pre = buf[:64000].copy()
    cur = (buf >> 3).astype(np.int32)                           # 0x11dc6: whole 64000 bytes >> 3 (rest of 64K as is)
    cur[64000:] = buf[64000:]
    h = hist.astype(np.int32)
    newh = (((h << 3) & 0xff) + ((cur - h) & 0xff)) & 0xff     # 0x12111 cl=3, done on bytes
    newh = (newh >> 3).astype(np.uint8)
    disp = cur.astype(np.uint8).copy()
    hh = np.concatenate([newh, np.zeros(400, np.uint8)]).astype(np.int32)
    n = np.arange(62720)
    A = hh[n + 1] + hh[n + 321]
    Am1 = np.concatenate([[0], A[:-1]])
    disp[:62720] = ((A + Am1) & 0xff) >> 2
    return (lx, ly, R), pre, newh, disp


def hsv(h, s, v):
    """0x13f98: 8-bit HSV -> RGB (6 sectors)."""
    v1 = v + 1
    e = h * 6
    sec, f = e >> 8, e & 255
    if not (sec & 1): f ^= 255
    X = ((255 - ((f * s) >> 8)) * v1) >> 8
    P = ((255 - s) * v1) >> 8
    order = [(0, 1, 2), (1, 0, 2), (1, 2, 0), (2, 1, 0), (2, 0, 1), (0, 2, 1)][sec]
    out = [0, 0, 0]
    out[order[0]], out[order[1]], out[order[2]] = v, X, P
    return out


def pulse_colour(p):
    """0x1201d..0x12043: p = [0x12012] after its += 2."""
    a = p if p < 0x80 else (~p) & 0xff
    a >>= 2
    a = (a * 0xe6) >> 8
    return hsv(a + 0x9a, 0xff, 0xff)


def lut(col, level):
    """0x11f73: 32 levels -> 16-bit 5:6:5 pixels (the masks from the VBE mode: 5/6/5 bits at 11/5/0)."""
    out = []
    for l in range(32):
        b = l + level
        b = 0 if b < 0 else (31 if b > 31 else b)
        px = 0
        for ch, (bits, pos) in zip(col, ((5, 11), (6, 5), (5, 0))):
            a = min(255, ch + b * 8)
            px |= ((a * b) >> (13 - bits)) << pos
        out.append(px)
    return out


def main():
    F = pickle.load(open(sys.argv[1], 'rb'))
    s = c.load_snap(440)
    m = s['mem']
    tb = c.u32(m, 0x6572)
    tex = np.frombuffer(m[tb:tb + 0x30000], np.uint8).reshape(3, 256, 256)
    lb = c.u32(m, 0x654a)
    light = np.frombuffer(m[lb:lb + 0x10000], np.uint8)
    mb = c.u32(m, 0x662b)
    mask = np.frombuffer(m[mb:mb + 0x10000], np.uint8)
    for f in F[1:]:
        T0 = f['timers'][0][1]
        assert f['timers'][0][0] == 0x1198b
        hist0 = np.frombuffer(f['hist0'], np.uint8)
        (lx, ly, R), pre, newh, disp = frame(T0, f['angles0'], hist0, tex, light, mask)
        rec = np.frombuffer(f['buf'], np.uint8)
        bm = np.frombuffer(f['bump'], np.uint8)
        b2 = bump(tex, light, lx, ly).ravel()
        e_b = int((b2 != bm).sum())
        e_pre = int((pre != np.frombuffer(f['preshr'], np.uint8)).sum())
        e_h = int((newh != rec[0x10000:]).sum())
        e_d = int((disp[:62720] != rec[:62720]).sum())
        # angles at the end: from the 40 timer reads at 0x11d85 (spark 40 first)
        ang = bytearray(f['angles0'])
        tl = [v for r, v, t in f['timers'] if r == 0x11d85]
        for k, i in enumerate(range(40, 0, -1)):
            T = tl[k]
            s1, s2 = struct.unpack_from('<hh', m, 0x11662 + 4 * i)   # speeds: the first random dword of spark i
            for j, sp in enumerate((s1, s2)):
                p = struct.unpack('<i', struct.pack('<I', T & 0xffffffff))[0] * (sp << 8)
                struct.pack_into('<H', ang, 4 * i + 2 * j, (p >> 32) & 0xffff)
        e_a = sum(a != b for a, b in zip(ang[4:0xa4], f['angles1'][4:0xa4]))
        col = pulse_colour(f['pulse'])
        L = lut(col, f['fade'] >> 3)
        e_lut = sum(a != b for a, b in zip(L, struct.unpack('<32I', f['lut'])))
        col_ok = list(f['col'][:3]) == col
        fl = {t: tk for n, tk, tm, t, part in c.flips()}
        page = np.array([L[v] for v in range(32)], np.uint16)[disp[:64000].reshape(200, 320)]
        page[194:] = 0
        cap = c.frame565(fl[T0])
        e_cap = int((page != cap).sum())
        print(f"capture diff {e_cap} (tick {fl[T0]})  ", end='')
        print(f"tick {f['tick']}: light ({lx},{ly}) R {R}  bump diff {e_b}  pre-shr diff {e_pre}  history diff {e_h}"
              f"  shown diff {e_d}  angles diff {e_a}  colour {col_ok}  LUT diff {e_lut}")


if __name__ == '__main__':
    main()

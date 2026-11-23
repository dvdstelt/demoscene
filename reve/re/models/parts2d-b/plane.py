"""The interlaced textured planes of script entries 18, 19 (DADA + a TEXTURE tile) and 23 (TOM + BOZ).

Integer-exact model of 0x259de4 (rotation matrix), 0x259ed0 (transform a point), 0x259f70 (row interpolants),
0x25a048 (one row: ray / plane intersection of its two ends) and the row drawers 0x259a08 (64 x 64 texture, even
lines), 0x259b50 (256 x 256, even lines), 0x259c98 (256 x 256, odd lines). See notes-parts2d-b.md.

usage (from reve/re, in the toolbox):
  python3 models/parts2d-b/plane.py check [entries...]   find, for every captured record of entries 18/19/23, the
                                                         retrace count n whose model frame equals it, and report
  python3 models/parts2d-b/plane.py shot A1 B1 A2 B2 TEX out.png   one frame (TEX = 18 or 23)
"""
import sys

import numpy as np

from common import raw, sine, frames, indexed, entry_starts, read_ticks, FULL

S = sine()
M32 = 0xffffffff

# the constant points at 0x259934 (eye) and 0x25994c.. (the four corners of the view quad), read from the image
import rep  # noqa: E402
EYE = tuple(np.frombuffer(rep.rd(0x259934, 12), '<i4').tolist())                       # (0, 0x200, 0)
QUAD = [tuple(np.frombuffer(rep.rd(0x25994c + 12 * k, 12), '<i4').tolist()) for k in range(4)]
ROWS = 0x64                                                                          # 100 rows per layer


def s32(x):
    x &= M32
    return x - (1 << 32) if x & 0x80000000 else x


def mul30(a, b):
    """imul; shld edx, eax, 2: bits 30..61 of the 64-bit product."""
    return s32((a * b) >> 30)


def idiv(n, d):
    q = abs(n) // abs(d)
    return q if (n >= 0) == (d > 0) else -q


def matrix(a, b):
    """0x259de4: the 3 x 3 matrix (2.30 fixed point) from the angles a = [0x2598fc], b = [0x259900] (4096 a turn)."""
    a &= 0xfff
    b &= 0xfff
    sa, ca = int(S[a]) << 15, int(S[(a + 0x400) & 0xfff]) << 15
    sb, cb = int(S[b]) << 15, int(S[(b + 0x400) & 0xfff]) << 15
    # rows: X' = m[0] . p, Y' = m[1] . p, Z' = m[2] . p
    return ((cb, mul30(sb, ca), mul30(sb, -sa)),
            (-sb, mul30(cb, ca), mul30(cb, -sa)),
            (0, sa, ca))


def xform(m, p):
    """0x259ed0 (the translation 0x259928.. is zero)."""
    return tuple(mul30(p[0], r[0]) + mul30(p[1], r[1]) + mul30(p[2], r[2]) for r in m)


def rows(a, b):
    """Per row k (0..99): (du, dv, u0, v0) as 0x25a048 returns them (EDI, ESI, EDX, ECX), or None for a black row."""
    m = matrix(a, b)
    E = xform(m, EYE)
    P0, P1, P2, P3 = (xform(m, q) for q in QUAD)
    dL = [idiv((P1[i] - P0[i]) << 16, ROWS) for i in range(3)]          # 0x259f70
    dR = [idiv((P3[i] - P2[i]) << 16, ROWS) for i in range(3)]
    L = [P0[i] << 16 for i in range(3)]
    R = [P2[i] << 16 for i in range(3)]
    Ez4 = E[2] << 4
    num = s32(Ez4 << 16)
    Ex8, Ey8 = s32(E[0] << 8), s32(E[1] << 8)
    out = []
    for k in range(ROWS):
        Lx8, Ly8, Lz4 = L[0] >> 8, L[1] >> 8, L[2] >> 12
        Rx8, Ry8, Rz4 = R[0] >> 8, R[1] >> 8, R[2] >> 12
        res = None
        den = Ez4 - Lz4
        if den == 0:                                   # degenerate: EDI = ESI = 1, EDX = cdq, ECX = Lz4
            res = (1, 1, -1 if num < 0 else 0, Lz4)
        else:
            t = idiv(num, den)
            if t < 0:
                res = (0, 0, 0, 0)
            else:
                uL = s32(s32(((Lx8 - Ex8) * t) >> 16) + Ex8)
                vL = s32(s32(((Ly8 - Ey8) * t) >> 16) + Ey8)
                den2 = Ez4 - Rz4
                if den2 == 0:
                    res = (1, 1, -1 if num < 0 else 0, Ey8)
                else:
                    t2 = idiv(num, den2)
                    if t2 < 0:
                        res = (0, 0, 0, 0)
                    else:
                        uR = s32(s32(((Rx8 - Ex8) * t2) >> 16) + Ex8)
                        vR = s32(s32(((Ry8 - Ey8) * t2) >> 16) + Ey8)
                        res = (s32(uR - uL), s32(vR - vL), s32(uL * 320), s32(vL * 320))
        out.append(res)
        for i in range(3):
            L[i] = s32(L[i] + dL[i])
            R[i] = s32(R[i] + dR[i])
    return out


I320 = np.arange(320, dtype=np.int64)


def draw(dst, first_line, a, b, tex, size):
    """0x259a08 (size 64, first_line 0), 0x259b50 (256, 0), 0x259c98 (256, 1): 100 rows on every other line."""
    sh, top = (10, 26) if size == 64 else (8, 24)
    flat = tex.reshape(-1)
    for k, (du, dv, u0, v0) in enumerate(rows(a, b)):
        y = first_line + 2 * k
        if du == 0 and dv == 0:
            dst[y] = 0
            continue
        su, sv = s32(du << sh), s32(dv << sh)
        U = ((u0 << sh) + I320 * su) & M32
        V = ((v0 << sh) + I320 * sv) & M32
        dst[y] = flat[((U >> top) << (32 - top)) | (V >> top)]
    return dst


def texture_tile():
    """Entry 18's init: the 64 x 64 tile at the top-left of [0x21b140] (GFX\\TEXTURE.RAW) copied to [0x205e94]."""
    px, _ = raw('TEXTURE')
    return np.ascontiguousarray(px[:64, :64])


def frame18(a1, b1, a2, b2, tile=None, dada=None):
    """0x20510c: even lines the tile at (a1, b1), odd lines DADA at (a2, b2)."""
    tile = texture_tile() if tile is None else tile
    dada = raw('DADA')[0] if dada is None else dada
    scr = np.zeros((200, 320), np.uint8)
    draw(scr, 0, a1, b1, tile, 64)
    draw(scr, 1, a2, b2, dada, 256)
    return scr


def frame23(a, b1, b2, tom=None, boz=None):
    """0x20520c: even lines TOM at (a, b1), odd lines BOZ at (a, b2)."""
    tom = raw('TOM')[0] if tom is None else tom
    boz = raw('BOZ')[0] if boz is None else boz
    scr = np.zeros((200, 320), np.uint8)
    draw(scr, 0, a, b1, tom, 256)
    draw(scr, 1, a, b2, boz, 256)
    return scr


def angles(entry, n, n18=None):
    """The angle words after n calls of the entry's retrace routine (n18: calls of entry 18's, for entry 19)."""
    if entry == 18:
        return 10 * n, 5 * n, 10 * n, 5 * n
    if entry == 19:
        return 10 * (n18 + n), 5 * (n18 + n), 10 * n18 + 5 * n, 5 * n18 + 2 * n
    if entry == 23:
        return max(0x300 - 3 * n, 0x1f4), 8 * n, 12 * n


def model(entry, n, n18=None):
    if entry in (18, 19):
        return frame18(*angles(entry, n, n18))
    a, b1, b2 = angles(entry, n)
    return frame23(a, b1, b2)


def check(entries):
    raw_, nrec = frames()
    init, setf = entry_starts()
    ticks = read_ticks(FULL + '.ticks')
    n18 = None
    for e in entries:
        if e not in setf:
            print('entry %d: capture not there yet' % e)
            continue
        s = setf[e]
        end = min(init.get(e + 1, nrec), nrec)
        if e == 19:
            n18 = s - setf[18]
        cache = {}
        res = []
        for k in range(s, end):
            px = indexed(raw_, k)[0]
            hit = None
            # the record at refresh k shows a frame drawn after about k - s retrace calls
            for n in sorted(range(max(0, k - s - 6), k - s + 3), key=lambda n: abs(n - (k - s))):
                if n not in cache:
                    cache[n] = model(e, n, n18)
                if (cache[n] == px).all():
                    hit = n
                    break
            if hit is None:
                best = min(range(max(0, k - s - 6), k - s + 3), key=lambda n: (cache[n] != px).sum())
                hit = (best, int((cache[best] != px).sum()))
            res.append((k, int(ticks[k]['drawn']) if k in ticks else -1, hit))
        exact = sum(1 for r in res if isinstance(r[2], int))
        offs = {}
        for k, d, h in res:
            if isinstance(h, int):
                offs[(k - s) - h] = offs.get((k - s) - h, 0) + 1
        print('entry %d: records %d..%d, %d exact; (record - start) - n: %s' % (e, s, end - 1, exact, offs))
        bad = [r for r in res if not isinstance(r[2], int)]
        for r in bad[:10]:
            print('   record %d drawn %d: nearest n %d, %d pixels differ' % (r[0], r[1], r[2][0], r[2][1]))


if __name__ == '__main__':
    if sys.argv[1] == 'check':
        check([int(x) for x in sys.argv[2:]] or [18, 19, 23])
    elif sys.argv[1] == 'shot':
        from PIL import Image
        from common import to_rgb, pal6
        a1, b1, a2, b2, tex = (int(x, 0) for x in sys.argv[2:7])
        if tex == 18:
            scr, pal = frame18(a1, b1, a2, b2), raw('DADA')[1]
        else:
            scr, pal = frame23(a1, b1, b2), raw('TOM')[1]
        Image.fromarray(to_rgb(scr, pal6(pal))).resize((640, 480), Image.NEAREST).save(sys.argv[7])


def draw_float(dst, first_line, a, b, tex, size):
    """The same layer in floating point (the form for the remake, notes section 4.4), nearest texel."""
    al, be = 2 * np.pi * a / 4096, 2 * np.pi * b / 4096
    sa, ca, sb, cb = np.sin(al), np.cos(al), np.sin(be), np.cos(be)

    def R(x, y, z):
        y2 = ca * y - sa * z
        return cb * x + sb * y2, -sb * x + cb * y2, sa * y + ca * z
    Ex, Ey, Ez = R(0.0, 512.0, 0.0)
    i = np.arange(320.0)
    for k in range(100):
        x, z = 128 - 0.8 * i, 128 - 2.56 * k
        Qx, Qy, Qz = R(x, 0.0, z)
        den = Ez - Qz
        y = first_line + 2 * k
        with np.errstate(divide='ignore', invalid='ignore'):
            t = Ez / den
        ok = np.isfinite(t) & (t > 0)
        Hx, Hy = Ex + t * (Qx - Ex), Ey + t * (Qy - Ey)
        u = np.floor(np.where(ok, 1.25 * Hx, 0)).astype(np.int64) % size
        v = np.floor(np.where(ok, 1.25 * Hy, 0)).astype(np.int64) % size
        dst[y] = np.where(ok, tex[u, v], 0)
    return dst


def compare_float(entry, n, n18=618):
    """Share of pixels where the float form picks the same index as the integer model."""
    if entry in (18, 19):
        a1, b1, a2, b2 = angles(entry, n, n18)
        A, B, sa, sb = texture_tile(), raw('DADA')[0], 64, 256
    else:
        a1, b1, b2 = angles(entry, n)
        a2 = a1
        A, B, sa, sb = raw('TOM')[0], raw('BOZ')[0], 256, 256
    f = np.zeros((200, 320), np.uint8)
    draw_float(f, 0, a1, b1, A, sa)
    draw_float(f, 1, a2, b2, B, sb)
    return (f == model(entry, n, n18)).mean()

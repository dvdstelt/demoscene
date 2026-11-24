"""Model of the second part 0x2e779 (objects): background rows 0x2e9ca, the cog mesh, 0x2e5f5 (transform and
project), 0x2e304 (stamp line), the callback 0x2eb81 and the palette blend.

All arithmetic is 32-bit like the original (imul truncation, sar, idiv toward zero).
"""
from common import *
from title import T, C

M32 = 0xffffffff

def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v

def idiv(a, b):
    a, b = s32(a), s32(b)
    q = abs(a) // abs(b)
    return s32(-q if (a < 0) != (b < 0) else q)

def sar(a, n):
    return s32(a) >> n

def imul(a, b):
    return s32(s32(a) * s32(b))

def sinv(i):
    return int(T[i])

def cosv(i):
    return int(T[i + 0x401])

# ---- the mesh: 64 vertices built by the set-up 0x2e7c2 (the second set at 0x2cde4 is built but never used)
def build_vertices():
    v = []
    for z in (0x400, -0x400):
        a = 0
        for it in range(8):
            s, c = sinv(a), cosv(a)
            v.append((s, c, z)); v.append((3 * s, 3 * c, z))
            a += 0x100
            s, c = sinv(a), cosv(a)
            v.append((3 * s, 3 * c, z)); v.append((s, c, z))
            a += 0x100
    return v

VERTS = build_vertices()
_e = np.frombuffer(frd(0x2d0e4, 768), np.uint8).reshape(96, 8)
EDGES = [(int(a), int(b)) for a, b in _e[:, [0, 4]]]

# 8x8 stamp of 0x2e304 (bytes added at rows 0..7, columns 0..7)
STAMP = np.array([
    [6, 1, 1, 1, 1, 1, 1, 6],
    [1, 1, 1, 1, 1, 1, 1, 1],
    [1, 1, 1, 2, 2, 1, 1, 1],
    [1, 1, 2, 6, 6, 2, 1, 1],
    [1, 1, 2, 6, 6, 2, 1, 1],
    [1, 1, 1, 2, 2, 1, 1, 1],
    [1, 1, 1, 1, 1, 1, 1, 1],
    [6, 1, 1, 1, 1, 1, 1, 6]], np.int64)

def rot(a, b, ang):
    """(a*cos - b*sin) >> 10, (b*cos + a*sin) >> 10 with 32-bit products"""
    s, c = sinv(ang), cosv(ang)
    na = sar(s32(imul(a, c) - imul(s, b)), 10)
    nb = sar(s32(imul(b, c) + imul(s, a)), 10)
    return na, nb

def project(st):
    """0x2e5f5 first half: the 64 vertices -> screen (x, y) ints"""
    out = []
    for (x, y, z) in VERTS:
        x, y = rot(x, y, st['angA'])
        x, z = rot(x, z, st['angB'])
        x = s32(x + st['xoff'] + st['xoff2'])
        x, z = rot(x, z, st['angC'])
        y, z = rot(y, z, st['angD'])
        z = s32(z + st['zdist'])
        if z > 0:
            X = idiv(s32(x << 7), z)
            Y = idiv(s32(y << 7), z)
        else:
            k = s32(1 - z)
            X = imul(x, k)
            Y = imul(y, k)
        out.append((s32(X + 0xa0), s32(Y + 0x64)))
    return out

def line(stamps, x1, y1, x2, y2, shift):
    """0x2e304: collect stamp origins (x, y) (top left of the 8x8 stamp)"""
    if not (y1 > 0) and y2 < 0:
        return
    if (y1 & M32) >= (y2 & M32):
        x1, y1, x2, y2 = x2, y2, x1, y1
    d = s32(x2 - x1)
    n = (d ^ (d >> 31)) + 1           # cdq; xor eax, edx; inc eax
    d2 = s32(y2 - y1)
    m = (d2 ^ (d2 >> 31)) + 1
    n &= M32; m &= M32
    if not (n > m):
        n = m
    n = (n >> shift) & M32
    if n == 0:
        return
    bx = idiv(s32(s32(x2 - x1) << 8), n)
    bp = idiv(s32(s32(y2 - y1) << 8), n)
    if y1 < 0:                         # never true here (the unsigned sort puts a y >= 0 first), kept for fidelity
        x1 = s32(x1 + imul(-y1, bx))
        y1 = 0
    ex = s32(x1 << 8)
    ey = s32(y1 << 8)
    fast = (0 <= y1 <= 0xbf and 0 <= y2 <= 0xbf and 0 <= x1 <= 0x137 and 0 <= x2 <= 0x137)
    if fast:
        cnt = n
    else:
        cnt = ((n & 0xff) - 1) % 256 + 1   # 'dec cl; jne': the low byte of the count (0 -> 256)
    for _ in range(cnt):
        yy, xx = ey >> 8, ex >> 8
        if fast or (0 <= yy <= 0xbf and 0 <= xx <= 0x137):
            stamps.append((xx, yy))
        ey = s32(ey + bp)
        ex = s32(ex + bx)

def draw_object(stamps, st, shift):
    P = project(st)
    for a, b in EDGES:
        line(stamps, P[a][0], P[a][1], P[b][0], P[b][1], shift)

def background(bg1, bg2, seed):
    """0x2e9ca: rows (200 x 320 uint8) and the new seed"""
    v = np.empty(200, np.int64)
    a, b = bg1, bg2
    for y in range(200):
        v[y] = ((sinv(a) + 0x400) >> 6) + ((sinv(b) + 0x400) >> 8)
        a = (a + 10) & 0xfff
        b = (b + 32) & 0xfff
    img = np.empty((200, 320), np.int64)
    x = seed
    for y in range(200):
        d = x & 0x03030303
        nb = np.array([(d >> (8 * i)) & 0xff for i in range(4)], np.int64)
        img[y] = v[y] + np.tile(nb, 80)
        x = (x + 0x9d) & M32
        x = ((x << 11) | (x >> 21)) & M32
        x ^= 0x9d
    return img, x

def stamp_image(stamps):
    acc = np.zeros((200 + 8, 320 + 8), np.int64)
    if stamps:
        a = np.array(stamps, np.int64)
        xs, ys = a[:, 0], a[:, 1]
        for dy in range(8):
            for dx in range(8):
                np.add.at(acc, (ys + dy, xs + dx), STAMP[dy, dx])
    return acc[:200, :320]

# ---- the callback 0x2eb81 (with a GUS: no fake clock)
INIT = dict(palw0=2048, palw1=0, angB=0, xoff2=0, tubeX=-32764, zdist=-2048, xoff=0, angD=0, ph1=0, ph2=0, ph3=0,
            angC=0, angC0=0, angA=0, bg1=2048, bg2=3328, flag=0, shift=2)

def init_from_snapshot(s):
    a = dict(palw0=0x2c9ac, palw1=0x2c9b0, flag=0x2c9b8, angB=0x2c9bc, xoff2=0x2c9c0, tubeX=0x2c9c4, zdist=0x2ca90,
             xoff=0x2ca94, angD=0x2ca98, ph1=0x2caa8, ph2=0x2caac, ph3=0x2cab0, angC=0x2cab4, angC0=0x2cab8,
             angA=0x2cabc, bg1=0x2cac0, bg2=0x2cac4)
    st = {k: mi32(s, v) for k, v in a.items()}
    st['shift'] = 2       # the set-up writes 2 into the shr at 0x2e374
    return st

def callback(st, order, row):
    if order >= 4 and st['palw0'] != 0:
        st['palw0'] -= 8
        st['palw1'] += 8
    if order < 3:
        st['angD'] = (st['angD'] + 0xf) & 0xfff
        st['angC0'] = (st['angC0'] + 2) & 0xfff
        st['zdist'] = s32(st['zdist'] + 0x40)
        if order >= 2:
            st['xoff'] = s32(st['xoff'] + 0x100)
            st['angA'] = (st['angA'] + 0x40) & 0xfff
            if row >= 0x20:
                st['zdist'] = s32(st['zdist'] + 0x5f4)
    else:
        st['shift'] = 1
        st['xoff'] = 0
        st['angB'] = 0x400
        st['zdist'] = 0x1555
        st['angC0'] = 0x400
        st['ph1'] = (st['ph1'] + 0xd) & 0xfff
        st['ph2'] = (st['ph2'] + 0x17) & 0xfff
        st['ph3'] = (st['ph3'] - 9) & 0xfff
        st['angA'] = (st['angA'] + 0x10) & 0xfff
        st['angD'] = (st['angD'] + 5) & 0xfff
        e = sinv(st['ph1']) >> 1
        e = (e + sinv(st['ph2'])) >> 1
        e = (e + sinv(st['ph3'])) & 0xfff
        st['angC'] = e
        st['tubeX'] = s32(st['tubeX'] + 0x100)
        if not (order >= 6 and row >= 0x20):
            if st['tubeX'] >= 0:
                st['flag'] = 1
                st['tubeX'] &= 0x1fff
    st['bg2'] = (st['bg2'] - 0x10) & 0xfff
    st['bg1'] &= 0xfff

def draw(st, order, seed):
    """the main loop's frame (indices), given the state after the callbacks and the order the loop reads"""
    img, seed = background(st['bg1'], st['bg2'], seed)
    stamps = []
    if order < 3:
        st['angC'] = st['angC0']
        for k in range(8):
            draw_object(stamps, st, st['shift'])
            st['angC'] = (st['angC'] + 0x1ff) & 0xfff
    else:
        st['xoff2'] = s32(st['tubeX'] - 0x5000)
        for k in range(5):
            draw_object(stamps, st, st['shift'])
            st['xoff2'] = s32(st['xoff2'] + 0x2000)
    img = (img + stamp_image(stamps)) & 0xff
    return img.astype(np.uint8), seed

P0 = np.frombuffer(frd(0x2d9e4, 768), np.uint8).astype(np.int64)
P1 = np.frombuffer(frd(0x2dce4, 768), np.uint8).astype(np.int64)

def palette(st):
    return ((P0 * st['palw0'] + P1 * st['palw1']) >> 11).astype(np.uint8)

# exact: the stamps are 16 unaligned dword adds each, so a byte that overflows carries into the next byte of the
# same dword (and the carry out of a dword's top byte is lost); the result depends on the order of the adds
STAMP_DWORDS = [(0x000, 0x1010106), (0x140, 0x1010101), (0x280, 0x2010101), (0x3c0, 0x6020101), (0x500, 0x6020101),
                (0x640, 0x2010101), (0x780, 0x1010101), (0x8c0, 0x1010106), (0x004, 0x6010101), (0x144, 0x1010101),
                (0x284, 0x1010102), (0x3c4, 0x1010206), (0x504, 0x1010206), (0x644, 0x1010102), (0x784, 0x1010101),
                (0x8c4, 0x6010101)]

def stamp_exact(img, stamps):
    """img: 200x320 indices (the background); stamps in drawing order -> uint8 image with the dword carries"""
    import struct
    buf = bytearray(img.astype(np.uint8).tobytes()) + bytearray(8 * 320)
    up, pk = struct.unpack_from, struct.pack_into
    for x, y in stamps:
        base = y * 320 + x
        for off, v in STAMP_DWORDS:
            o = base + off
            pk('<I', buf, o, (up('<I', buf, o)[0] + v) & 0xffffffff)
    return np.frombuffer(bytes(buf[:64000]), np.uint8).reshape(200, 320)

def draw_exact(st, order, seed):
    img, seed = background(st['bg1'], st['bg2'], seed)
    stamps = []
    if order < 3:
        st['angC'] = st['angC0']
        for k in range(8):
            draw_object(stamps, st, st['shift'])
            st['angC'] = (st['angC'] + 0x1ff) & 0xfff
    else:
        st['xoff2'] = s32(st['tubeX'] - 0x5000)
        for k in range(5):
            draw_object(stamps, st, st['shift'])
            st['xoff2'] = s32(st['xoff2'] + 0x2000)
    approx = (img + stamp_image(stamps))
    if approx.max() > 255:
        return stamp_exact(img, stamps), seed, True
    return (approx & 0xff).astype(np.uint8), seed, False

def seed_list(n, seed=135477):
    """the background seed at the start of draws 0..n (200 generator steps per draw)"""
    out = [seed]
    x = seed
    for i in range(n):
        for y in range(200):
            x = (x + 0x9d) & M32; x = ((x << 11) | (x >> 21)) & M32; x ^= 0x9d
        out.append(x)
    return out

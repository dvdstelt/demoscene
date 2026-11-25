"""The water scene's data and tables, built the way WaterInit 0x405bd0 builds them (exact integer arithmetic).

load() -> dict with: ini, env (2, 256, 256) left/right halves of watenv.pcx, back (200, 320), pal (256, 3) 6-bit,
bump (256, 256, 2) int8 (dx, dy) as 0x405fcf builds it, objs (4 parsed .m3d), spl (solid, transparent: 6 channels each,
keys [t, y<<8, y2] after the 16.16 natural spline 0x413870), sin table (1025 dwords, 0x412560), env table (0x825428).
"""
import math
import os
import struct

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
WATER = os.path.join(RE, 'unpacked', 'files', '2', 'water')

M32 = 0xffffffff


def s32(x):
    x &= M32
    return x - (1 << 32) if x & 0x80000000 else x


def s16(x):
    x &= 0xffff
    return x - 0x10000 if x & 0x8000 else x


def idiv(n, d):
    """x86 idiv: quotient truncated toward zero"""
    q = abs(n) // abs(d)
    return q if (n >= 0) == (d >= 0) else -q


def shrd16(p):
    """shrd eax, edx, 16 of a 64-bit product: bits 16..47, as a signed 32-bit value"""
    return s32(p >> 16)


def ini(path=os.path.join(WATER, 'water.ini')):
    d = {}
    for line in open(path, encoding='latin1'):
        line = line.strip()
        if not line or line.startswith('#') or line.startswith('@') or '=' not in line:
            continue
        k, v = line.split('=', 1)
        d[k.strip()] = v.strip()
    return d


def pcx(name):
    im = Image.open(os.path.join(WATER, name))
    data = open(os.path.join(WATER, name), 'rb').read()
    pal = (np.frombuffer(data[-768:], np.uint8) >> 2).reshape(256, 3)
    return np.asarray(im, dtype=np.uint8), pal


def bump_table(b):
    """0x405fcf: per byte pair (dx, dy) = trunc(atan(diff * 0.01) * 81.5286624) (Watcom __CHP truncates);
    rows 0..254, columns 0..254 (column 255 and row 255 stay 0 from the zeroed allocation). The 'right' neighbour of
    column x is the next byte of the picture, the 'lower' neighbour the byte 256 further."""
    flat = b.astype(np.int64).ravel()
    t = np.zeros((256, 256, 2), np.int64)
    k1 = 0.01
    k2 = 81.52866242038216       # double at 0x8206a0 (256 / 3.14)
    for y in range(255):
        for x in range(255):
            p = y * 256 + x
            d1 = flat[p + 1] - flat[p]
            d2 = flat[p + 256] - flat[p]
            t[y, x, 0] = math.trunc(math.atan(d1 * k1) * k2)
            t[y, x, 1] = math.trunc(math.atan(d2 * k1) * k2)
    return (t & 0xff).astype(np.uint8)


def cos_fixed(x):
    """0x422120: cos of a 16.16 angle by its Taylor series, 16.16 result"""
    x &= M32
    edi = ((x * x) >> 16) & M32
    ebp = 0x10000
    bx = 0
    esi = 0x10000
    while True:
        bx = (bx + 2) & 0xffff
        cx = ((bx - 1) * bx) & 0xffff
        eax = edi // cx
        eax = ((eax * esi) >> 16) & M32
        esi = eax
        ebp = (ebp - eax) & M32
        bx = (bx + 2) & 0xffff
        cx = ((bx - 1) * bx) & 0xffff
        eax = edi // cx
        eax = ((eax * esi) >> 16) & M32
        esi = eax
        ebp = (ebp + eax) & M32
        if s32(eax) <= 1:
            return s32(ebp)


def sin_table():
    """0x412560: 1025 dwords, entry k = cos_fixed(floor(k * 0x64880 / 4096)) = cos(k pi / 2048) in 16.16"""
    return [cos_fixed((k * 0x64880) >> 12) for k in range(1025)]


def fsin(tab, a):
    """0x413ce4: sin of a 16-bit angle (65536 = a full turn) from the table; one's complement for the lower half"""
    a &= 0xffff
    idx = (a >> 2) & 0x1ffc
    if idx == 0:
        return 0
    v = tab[abs(idx - 0x1000) >> 2]
    if a & 0x8000:
        v = ~v
    return v


def fcos(tab, a):
    return fsin(tab, a + 0x4000)


def env_table():
    """the static word table at 0x825228..0x825628 (pointer [0x825638] = 0x825428), index -256..256:
    8192 + asin(i / 256) * 16384 / pi rounded, 16383 at i = 256 (10.6 fixed texel coordinates).
    Python model of the values (checked against the image in check_tables)."""
    t = {}
    for i in range(-256, 257):
        v = int(round(8192 + math.asin(i / 256) * 16384 / math.pi))
        t[i] = min(v, 16383)
    return t


def m3d(name):
    """the .m3d as the loader 0x412bf0 reads it with flags 3 (uv + normal indices)"""
    d = open(os.path.join(WATER, name), 'rb').read()
    magic, flags, nv, nn, ng, voff, coff, noff, goff = struct.unpack('<4s8I', d[:36])
    assert magic == b'$M3D' and flags == 7 and ng == 1
    verts = [struct.unpack('<hhhHH', d[voff + 10 * i: voff + 10 * i + 10]) for i in range(nv)]
    centre = struct.unpack('<hhh', d[coff:coff + 6])
    nrms = [struct.unpack('<hhh', d[noff + 6 * i: noff + 6 * i + 6]) for i in range(nn)]
    nf, mat = struct.unpack('<HH', d[goff:goff + 4])
    faces = [struct.unpack('<6H', d[goff + 4 + 12 * i: goff + 16 + 12 * i]) for i in range(nf)]
    # vertex u, v are scaled by 0x4000 / 65536 (0x413330): unsigned word * 0x4000 >> 16
    uv = [((u * 0x4000) >> 16, (v * 0x4000) >> 16) for (_, _, _, u, v) in verts]
    pos = [(x, y, z) for (x, y, z, _, _) in verts] + [centre]
    return {'pos': pos, 'uv': uv, 'nrm': nrms, 'faces': faces, 'mat': mat & 0xff}


def spl_columns(name):
    rows = []
    for line in open(os.path.join(WATER, name), encoding='latin1'):
        line = line.strip()
        if not line or line.startswith('@'):
            continue
        rows.append([int(x) for x in line.split()])
    return [[r[c] for r in rows] for c in range(6)]


def spline_y2(xs, ys):
    """0x413870: Numerical Recipes' spline() in 16.16 with natural ends (y2[1] = u[1] = 0, qn = un = 0)"""
    n = len(xs)
    y2 = [0] * n
    u = [0] * n

    def div16(a, b):          # mov edx, eax; shl eax, 16; sar edx, 16; idiv
        return idiv(s32(a) << 16, b)
    for i in range(1, n - 1):
        sig = div16(xs[i] - xs[i - 1], xs[i + 1] - xs[i - 1])
        p = shrd16(sig * y2[i - 1]) + 0x20000
        y2[i] = div16(sig - 0x10000, p)
        dr = div16(ys[i + 1] - ys[i], xs[i + 1] - xs[i])
        dl = div16(ys[i] - ys[i - 1], xs[i] - xs[i - 1])
        e = s32(dr - dl)
        c = idiv(0x60000 * e, xs[i + 1] - xs[i - 1])
        c = s32(c - shrd16(sig * u[i - 1]))
        u[i] = div16(c, p)
    # natural end: y2[n-1] = (0 - 0) / (0 + 1) = 0
    y2[n - 1] = 0
    for k in range(n - 2, -1, -1):
        y2[k] = s32(shrd16(y2[k] * y2[k + 1]) + u[k])
    return y2, u


def channel(values):
    xs = [i << 16 for i in range(len(values))]
    ys = [s32(v << 8) for v in values]
    y2, u = spline_y2(xs, ys)
    return {'x': xs, 'y': ys, 'y2': y2, 'u': u}


def spline_eval(ch, t):
    """0x413b90: binary search (t <= x[k] -> khi = k), then the cubic spline formula in 16.16"""
    xs, ys, y2 = ch['x'], ch['y'], ch['y2']
    klo, khi = 0, len(xs) - 1
    while khi - klo > 1:
        k = (klo + khi) >> 1
        if t <= xs[k]:
            khi = k
        else:
            klo = k
    h = xs[khi] - xs[klo]
    if h == 0:
        return 0
    a = idiv(s32(xs[khi] - t) << 16, h)
    b = idiv(s32(t - xs[klo]) << 16, h)
    bb = shrd16(shrd16(b * b) * b)
    ebp = shrd16(s32(bb - b) * y2[khi])
    aa = shrd16(shrd16(a * a) * a)
    e = shrd16(s32(aa - a) * y2[klo])
    e = s32(e + ebp)
    e = shrd16(e * h)
    e = idiv(e * h, 0x60000)
    r = s32(e + shrd16(a * ys[klo]))
    r = s32(r + shrd16(b * ys[khi]))
    return r


def load():
    cfg = ini()
    envpic, envpal = pcx('watenv.pcx')
    back, _ = pcx('watback.pcx')
    bumppic, _ = pcx('watbump.pcx')
    env = np.stack([envpic[:, :256], envpic[:, 256:512]])
    solid = [channel(c) for c in spl_columns('wat_s.spl')]
    trans = [channel(c) for c in spl_columns('wat_sphe.spl')]
    return {'ini': cfg, 'env': env, 'back': back, 'pal': envpal, 'bump': bump_table(bumppic),
            'objs': [m3d('wat_sphe.m3d')] + [m3d('wat_s.m3d') for _ in range(3)],
            'solid': solid, 'trans': trans, 'sin': sin_table(), 'envtab': env_table(), 'bumppic': bumppic}

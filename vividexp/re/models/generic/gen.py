"""Python model of VIVID.EXE's generic 3D scenes (GenericInit 0x408cec, GenericEffect 0x409d74).

Integer-exact port of the routines (see ../../notes-generic.md):
  read_ini        the key=value reader (0x4129ec int, 0x412938 string)
  read_spl        the .spl reader (0x413520 @length, 0x4137f8 one column)
  spline_prep     0x413870: natural cubic spline, second derivatives, 16.16 fixed point (Numerical Recipes 'spline')
  spline_eval     0x413b90: 'splint' in 16.16
  read_m3d        the $M3D object (loader 0x412bf0)
  sin16 / cos16   0x413ce4 / 0x413ce0 with the 1025-entry table 0x412560
  frame_state     GenericEffect's per-frame object state (position, angles) from the frame counter
  transform       0x413c93 reset, 0x413d98 rot Y, 0x413e6a rot X, 0x413f37 rot Z; 0x414160 normals -> envmap coords
  project         0x414000; backface 0x4140e0; near test in 0x409d74; face order 0x411980 + 0x4119f0 / 0x411a30
  raster          0x414f13 (types 9, 10) and 0x418313 (type 11): affine 8.8 spans with per-triangle gradients
Everything takes the files from ../../unpacked/files/.
"""
import math
import os
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
FILES = os.path.join(RE, 'unpacked', 'files')


def path(name):
    """archive file name (gen/..., gen2/...) -> extracted file"""
    for k in ('2', '3', '1'):
        p = os.path.join(FILES, k, name)
        if os.path.exists(p):
            return p
    raise FileNotFoundError(name)


def load(name):
    return open(path(name), 'rb').read()


# ---------------------------------------------------------------------------------------------------------------
# fixed-point helpers (x86 semantics)

def idiv(a, b):
    """x86 idiv: truncates toward zero"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def s32(x):
    x &= 0xffffffff
    return x - (1 << 32) if x & 0x80000000 else x


def s16(x):
    x &= 0xffff
    return x - 0x10000 if x & 0x8000 else x


def fmul(a, b):
    """imul; shrd eax, edx, 16 (floor of the 64-bit product / 65536, low 32 bits)"""
    return s32((a * b) >> 16)


def fdiv(a, b):
    """mov edx,eax; shl eax,16; sar edx,16; idiv b  ==  (a << 16) / b, the dividend sign-extended from a's 32 bits"""
    return s32(idiv(s32(a) << 16, b))


# ---------------------------------------------------------------------------------------------------------------
# ini (0x4129ec / 0x412938): key=value lines; '#' comments; '@end'

def read_ini(name):
    d = {}
    for line in load(name).decode('latin1').splitlines():
        line = line.strip()
        if not line or line.startswith('#') or line.startswith('@'):
            continue
        if '=' in line:
            k, v = line.split('=', 1)
            d[k.strip()] = v.strip()
    return d


# ---------------------------------------------------------------------------------------------------------------
# splines

def read_spl(name):
    """columns of integers (@width, @length rows); returns list of columns"""
    rows = []
    for line in load(name).decode('latin1').splitlines():
        s = line.strip()
        if not s or s[0] in '#@':
            continue
        rows.append([int(x) for x in s.split()])
    width = len(rows[0])
    return [[r[c] for r in rows] for c in range(width)]


def spline_prep(values):
    """0x413870 on a column whose values were multiplied by 256 (GenericInit shl 8).
    Records: x = i << 16, y = value << 8, y2, u; returns (xs, ys, y2s)."""
    n = len(values)
    x = [i << 16 for i in range(n)]
    y = [v << 8 for v in values]
    y2 = [0] * n
    u = [0] * n
    # 1-based in the code; here 0-based: y2[0] = u[0] = 0 (natural)
    for i in range(1, n - 1):
        sig = fdiv(x[i] - x[i - 1], x[i + 1] - x[i - 1])
        p = fmul(sig, y2[i - 1]) + 0x20000
        y2[i] = fdiv(sig - 0x10000, p)
        t1 = fdiv(y[i + 1] - y[i], x[i + 1] - x[i])
        t2 = fdiv(y[i] - y[i - 1], x[i] - x[i - 1])
        ui = t1 - t2
        e = idiv(0x60000 * ui, x[i + 1] - x[i - 1])          # imul edx (64-bit), idiv
        e = s32(e) - fmul(sig, u[i - 1])
        u[i] = fdiv(e, p)
    qn = un = 0                                              # natural end
    y2[n - 1] = fdiv(un - fmul(qn, u[n - 2]), fmul(qn, y2[n - 2]) + 0x10000)
    for k in range(n - 2, -1, -1):
        y2[k] = fmul(y2[k], y2[k + 1]) + u[k]
    return x, y, y2


def spline_eval(rec, pos):
    """0x413b90: value at pos (16.16 key index)"""
    x, y, y2 = rec
    n = len(x)
    lo, hi = 0, n - 1
    while hi - lo > 1:
        k = (lo + hi) >> 1
        if pos <= x[k]:
            hi = k
        else:
            lo = k
    h = x[hi] - x[lo]
    if h == 0:
        return 0
    a = fdiv(x[hi] - pos, h)
    b = fdiv(pos - x[lo], h)
    t = fmul(fmul(fmul(b, b), b) - b, y2[hi])
    s = fmul(fmul(fmul(a, a), a) - a, y2[lo]) + t
    s = fmul(s, h)
    s = s32(idiv(s * h, 0x60000))
    s += fmul(a, y[lo])
    s += fmul(b, y[hi])
    return s32(s)


# ---------------------------------------------------------------------------------------------------------------
# sine table 0x412560 (1025 dwords: T[k] = cos16(k * 0x64880 >> 12)) and the fixed cosine 0x422120

def cos_fixed(a):
    """0x422120: Taylor series of cos in 16.16 (a = angle in radians, 16.16, a >= 0)"""
    a2 = ((a * a) >> 16) & 0xffffffff
    r = 0x10000
    term = 0x10000
    k = 0
    while True:
        k += 2
        term = ((idiv(a2, ((k - 1) * k) & 0xffff) * term) >> 16) & 0xffffffff
        r -= term
        k += 2
        term = ((idiv(a2, ((k - 1) * k) & 0xffff) * term) >> 16) & 0xffffffff
        r += term
        if not (s32(term) > 1):
            break
    return s32(r)


SINTAB = [cos_fixed((k * 0x64880) >> 12) for k in range(0x401)]


def sin16(ang):
    """0x413ce4: sine of a 16-bit angle (0x10000 = full turn), 16.16; negative values are one's complements"""
    ang &= 0xffff
    i = (ang >> 2) & 0x1ffc
    if i == 0:
        return 0
    i = abs(i - 0x1000) >> 2
    v = SINTAB[i]
    return ~v if ang & 0x8000 else v


def cos16(ang):
    return sin16(ang + 0x4000)


# ---------------------------------------------------------------------------------------------------------------
# $M3D

class M3D:
    pass


def read_m3d(name):
    d = load(name)
    m = M3D()
    magic, flags, nv, nn, ng, ov, oc, on, of = struct.unpack('<4s8I', d[:36])
    assert magic == b'$M3D'
    m.flags = flags
    vs = 10 if flags & 1 else 6
    m.xyz = np.array([struct.unpack('<3h', d[ov + i * vs:ov + i * vs + 6]) for i in range(nv)], np.int64)
    m.uv = (np.array([struct.unpack('<2H', d[ov + i * vs + 6:ov + i * vs + 10]) for i in range(nv)], np.int64)
            if flags & 1 else None)
    m.centers = np.array([struct.unpack('<3h', d[oc + i * 6:oc + i * 6 + 6]) for i in range(ng if ng > 1 else 1)],
                         np.int64)
    m.normals = (np.array([struct.unpack('<3h', d[on + i * 6:on + i * 6 + 6]) for i in range(nn)], np.int64)
                 if flags & 4 else None)
    m.groups = []
    p = of
    rs = 12 if flags & 2 else 6
    for g in range(ng):
        cnt, col, _ = struct.unpack('<hBB', d[p:p + 4])
        faces = []
        for f in range(cnt):
            r = struct.unpack('<6h' if flags & 2 else '<3h', d[p + 4 + f * rs:p + 4 + (f + 1) * rs])
            faces.append(r)
        m.groups.append((col, np.array(faces, np.int64)))
        p += 4 + cnt * rs
    return m


# ---------------------------------------------------------------------------------------------------------------
# the envmap coordinate table at 0x825428 (words, index -256..256): round(8192 + 16384/pi * asin(i/256)), clamped

def env_table():
    import rep
    return {i: s16(rep.rd16(0x825428 + 2 * i)) for i in range(-256, 257)}


def env_formula(i):
    return min(16383, int(math.floor(8192 + 16384 / math.pi * math.asin(i / 256) + 0.5)))


# ---------------------------------------------------------------------------------------------------------------
# maps made by the init (0x4077d4)

def pcx(name):
    """ZSoft PCX 8 bit RLE -> (pixels HxW uint8, palette 256x3 8-bit)"""
    d = load(name)
    x0, y0, x1, y1 = struct.unpack('<4h', d[4:12])
    w, h = x1 - x0 + 1, y1 - y0 + 1
    bpl = struct.unpack('<h', d[66:68])[0]
    out = bytearray()
    p = 128
    while len(out) < bpl * h:
        c = d[p]; p += 1
        if c >= 0xc0:
            out += bytes([d[p]]) * (c & 0x3f); p += 1
        else:
            out.append(c)
    px = np.frombuffer(bytes(out[:bpl * h]), np.uint8).reshape(h, bpl)[:, :w]
    pal = np.frombuffer(d[-768:], np.uint8).reshape(256, 3)
    return px, pal


def bump_table(name, stride=2):
    """0x4074e4(dst, pcx pixels, 100.0, stride): per texel (dx, dy) = trunc(atan(diff / 100) * 256 / 3.14) as bytes,
    for x, y in 0..254 (row 255 and column 255 keep what the block held: zero)"""
    px, _ = pcx(name)
    b = px[:256, :256].astype(np.int64)
    k = 1.0 / 100.0
    out = np.zeros((256, 256, 2), np.int64)
    for y in range(255):
        for x in range(255):
            for j, d in enumerate((b[y, x + 1] - b[y, x], b[y + 1, x] - b[y, x])):
                v = math.atan(d * k) * 81.528662420382166  # 256 / 3.14
                out[y, x, j] = int(v) & 0xff                # chop
    return out


def isqrt(n):
    """0x4221e0: Newton iteration b = (b + n / b + 1) >> 1"""
    if n < 5:
        return ((n & 0xffff) + 1) >> 1
    cl = 0
    b = n
    if b > 0xffff: b >>= 16; cl += 8
    if b > 0xff: b >>= 8; cl += 4
    if b > 0xf: b >>= 4; cl += 2
    if b > 3: b >>= 2; cl += 1
    if b > 1: b >>= 1; cl += 1
    b = ((n >> cl) + 1 + (1 << cl)) >> 1
    di = (1 << cl) & 0xffff
    while di != (b & 0xffff):
        di = b & 0xffff
        q = (n // di) & 0xffff
        b = (di + 1 + q) >> 1
    return b & 0xffff


def phong_map(shades, amb, diff, spec, exp, shift=8):
    """0x413a60: 256x256 shade map (cdecl: centre = block + 0x8080, shift 8, shades, amb, diff, spec, exp)"""
    size = 1 << shift
    R = size >> 1
    out = np.zeros((size, size), np.int64)
    for y in range(R):
        for x in range(R):
            r = isqrt(x * x + y * y)
            if r > R:
                v = amb
            else:
                ang = (r * 0x19220) // R
                c = cos_fixed(ang) & 0xffffffff
                base = ((c * diff) >> 16) + amb
                if exp == 0:
                    p = 0x80000000
                else:
                    p = (c << 15) & 0xffffffff
                    n = exp - 1
                    if n != 0:
                        m = p
                        while True:
                            p = ((p * m) >> 31) & 0xffffffff
                            n -= 1
                            if n < 0:
                                break
                p = ((p * spec) >> 31) & 0xffffffff
                v = (p + base) & 0xffffffff
            v = ((v * (shades - 1)) >> 16) & 0xff
            out[R + y, R + x] = v
            out[R + y, R - 1 - x] = v
            out[R - 1 - y, R + x] = v
            out[R - 1 - y, R - 1 - x] = v
    return out

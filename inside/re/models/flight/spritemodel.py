"""Model of the engine's sprite drawer 0x415878 as the flight part uses it (the sun, mode 0: opaque copy; the
lens flares, mode 1: flare.shd[texel][background]), checked against run1.sprites (run_inside.py
--sprites-until 7300: the draw buffer before and after every sprite draw): python3 spritemodel.py [file]

Render item (object + 0xc8): +0x10 texture, +0x14 blend table, +0x18 texture stride, +0x1c width, +0x20 height,
+0x24 k (float: position factor), +0x28 size (float), +0x30/+0x34/+0x38 camera-space x, y, z * 128 (floats).
Engine globals (scene 320 x 200): F = [0x4b9014] = 25600, G = [0x4b8fec] = 21333, centre (160, 100), clip
0..320 x 0..200, near [0x4b9020] / 128 = 64, far [0x4b9018] = 0x400000.
"""
import math
import os
import struct
import sys

import numpy as np

F, G, CX, CY, NEAR, FAR = 25600, 21333, 160, 100, 64, 4194304
W_SCR, H_SCR = 320, 200


def ftrunc(x): return math.trunc(x)
def cdiv16(a): return int(a / 16) if a >= 0 else -((-a) // 16)


def sprite(x, y, z, w, h, stride, k, size):
    """returns (x0, x1, y0, y1, us (16.16 start), du16, v0 (float), dv) or None; same float order as 0x415878"""
    zz = z * 0.0078125
    if NEAR > zz or FAR < zz:
        return None
    fp = F * 0.0009765625
    sx = k * x * fp * (1.0 / zz)
    sy = k * y * fp * (1.0 / zz)
    sw = w * size * F * (1.0 / zz)
    sh = h * size * G * (1.0 / zz)
    if 0 >= sw or 0 >= sh:
        return None
    ty = (CY << 4) + sy
    ish = ftrunc(sh)
    y0 = ftrunc(ty - sh * 0.5)
    y1 = y0 + ish
    isw = ftrunc(sw)
    du = 16 * w / isw                      # trunc(SW): 0x40ba78 rounds st(0) in place before the fdivp
    dv = 16 * h / ish
    x0 = ftrunc(((CX << 4) + sx) - sw * 0.5)
    x1 = x0 + isw
    voff = uoff = 0.0
    if y0 < 0:
        voff = 0.0 - y0 * dv * 0.0625
        y0 = 0
    if x0 < 0:
        uoff = uoff - x0 * du * 0.0625
        x0 = 0
    fy, fx = y0 & 15, x0 & 15
    y0, y1, x0, x1 = cdiv16(y0), cdiv16(y1), cdiv16(x0), cdiv16(x1)
    x1 = min(x1, W_SCR)
    y1 = min(y1, H_SCR)
    if x1 <= x0 or y0 >= y1:
        return None
    us = uoff + (15 - fx) * du * 0.0625
    du16 = du * 65536
    v = voff + (15 - fy) * dv * 0.0625
    return x0, x1, y0, y1, ftrunc(us * 65536), ftrunc(du16), v, dv


def draw(buf, tex, blend, mode, geo, stride):
    x0, x1, y0, y1, us, du16, v, dv = geo
    out = buf.copy()
    for yy in range(y0, y1):
        row = ftrunc(v) * stride
        u = us
        for xx in range(x0, x1):
            t = int(tex[row + (u >> 16)])
            out[yy, xx] = t if mode == 0 else blend[t * 256 + int(out[yy, xx])]
            u += du16
        v += dv
    return out


if __name__ == '__main__':
    HERE = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(HERE, '..', '..'))
    import datfile
    dat = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
    files = {k: np.frombuffer(dat.get(k), np.uint8) for k in ('flight/sun.raw', 'flight/lens1.raw', 'flight/lens2.raw',
             'flight/lens3.raw', 'flight/lens4.raw', 'flight/lens5.raw')}
    blend = np.frombuffer(dat.get('flight/flare.shd'), np.uint8)
    sizes = {len(v): k for k, v in files.items()}
    f = open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'run1.sprites'), 'rb')
    n = ok = drawn = 0
    while True:
        h = f.read(8)
        if len(h) < 8:
            break
        kind, ln = struct.unpack('<4sI', h)
        p = f.read(ln)
        if kind == b'SPIN':
            frame, mode, item = struct.unpack_from('<III', p)
            it = p[12:12 + 0x3c]
            before = np.frombuffer(p[12 + 0x3c:], np.uint8).reshape(200, 320)
            w, hh = struct.unpack_from('<ii', it, 0x1c)
            stride = struct.unpack_from('<i', it, 0x18)[0]
            k, size = struct.unpack_from('<ff', it, 0x24)
            x, y, z = struct.unpack_from('<fff', it, 0x30)
            tex = files[sizes[w * hh]]
        else:
            after = np.frombuffer(p, np.uint8).reshape(200, 320)
            geo = sprite(float(np.float32(x)), float(np.float32(y)), float(np.float32(z)), w, hh, stride,
                         float(np.float32(k)), float(np.float32(size)))
            model = before if geo is None else draw(before, tex, blend, mode, geo, stride)
            n += 1
            drawn += geo is not None
            if np.array_equal(model, after):
                ok += 1
            elif n - ok < 5:
                d = np.argwhere(model != after)
                print('mismatch frame', frame, 'mode', mode, 'k', k, len(d), 'pixels, first', d[:3], geo)
    print('%d sprite draws (%d on screen): %d reproduced exactly' % (n, drawn, ok))

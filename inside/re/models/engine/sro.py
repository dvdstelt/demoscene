"""SurRender .sro objects as the engine loads them (0x410bb0) and the normals it computes (0x410918, 0x410a70).

Engine units: vertex positions are ints (file int16 x scale / 65536, truncated toward zero); u, v are 8.4 fixed
texel coordinates (12-bit, the file's 16-bit values >> 4; v negated); face normals are int vectors of length 1024,
vertex normals ints of length 819.2 (0.8 x 1024), both truncated toward zero.
"""
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))


def trunc(x):
    return int(math.trunc(x))


def load(data, scale=0x10000):
    """-> verts [(x, y, z, u, v)], faces [(a, b, c)] as 0x410bb0 builds them (scale = EBX, 16.16)."""
    voff, nv, _foff, nf = struct.unpack_from('<IIII', data, 4)
    s = scale * (1.0 / 65536)
    verts, faces = [], []
    p = voff
    for _ in range(nv):
        x, y, z, u, v = struct.unpack_from('<hhhHH', data, p)
        uu = u >> 4
        vv = -(v >> 4)
        verts.append((trunc(x * s), trunc(y * s), trunc(z * s), uu, vv))
        p += 12
    for _ in range(nf):
        _w, a, b, c = struct.unpack_from('<Hhhh', data, p)
        faces.append((a, b, c))
        p += 8
    return verts, faces


def face_normal(p0, p1, p2):
    """0x410918: n = (p1 - p0) x (p2 - p0), scaled to length 1024 (factor 1024 if |n| <= 1), truncated."""
    ax, ay, az = p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]
    bx, by, bz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
    nx = by * az - bz * ay
    ny = bz * ax - bx * az
    nz = bx * ay - by * ax
    l = math.sqrt(nx * nx + ny * ny + nz * nz)
    k = 1024.0 / l if l > 1.0 else 1024.0
    return trunc(nx * k), trunc(ny * k), trunc(nz * k)


def normals(verts, faces):
    fn = [face_normal(verts[a], verts[b], verts[c]) for a, b, c in faces]
    acc = [[0, 0, 0] for _ in verts]
    for (a, b, c), n in zip(faces, fn):
        for i in (a, b, c):
            for k in range(3):
                acc[i][k] += n[k]
    vn = []
    for s in acc:                                     # 0x410a70: sum of the faces' normals, length 0.8 x 1024
        l = math.sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2])
        k = (1024.0 / l if l > 1.0 else 1024.0) * 0.8
        vn.append((trunc(s[0] * k), trunc(s[1] * k), trunc(s[2] * k)))
    return fn, vn

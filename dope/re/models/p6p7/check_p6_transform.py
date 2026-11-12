"""p6: model of the object transformation (2bdd -> 1b56 matrix, 1a30/1930 vertices, screen and env coordinates) and
the face list (2ac7: culling, sort keys), checked against the engine buffers (segment 667b) in snapshots.
The snapshot is taken at a frame start, i.e. somewhere in the frame loop: the buffers belong to the last transform,
whose angles/positions were those of k retraces earlier (k = 0..3 tried)."""
import struct
import sys
import numpy as np
from snap import Snap, img
import sine
import mesh

b = img('p6')
SIN = sine.build(b, 0x100 + 0x220)
GS = 0x667b


def trunc_div(a, d):
    q = abs(a) // abs(d)
    return q if (a >= 0) == (d > 0) else -q


def sar(v, n): return v >> n


def matrix(a, bb, c):
    """1b56: angles are (object + group) >> 4 (16-bit, sar), table index (x & 0xffe)/2"""
    def sc(x):
        i = (x & 0xffe) >> 1
        return SIN[i], SIN[i + 512]
    s1, c1 = sc(a); s2, c2 = sc(bb); s3, c3 = sc(c)
    m00 = sar(c2 * c3 - sar(s2 * s1, 15) * s3, 15)
    m01 = sar(c1 * s3, 15)
    m02 = sar(s2 * c3 + sar(c2 * s1, 15) * s3, 15)
    m10 = sar(-(c2 * s3 + sar(s2 * s1, 15) * c3), 15)
    m11 = sar(c1 * c3, 15)
    m12 = sar(-(s2 * s3 - sar(c2 * s1, 15) * c3), 15)
    m20 = sar(-s2 * c1, 15)
    m21 = -s1
    m22 = sar(c2 * c1, 15)
    return [[m00, m01, m02], [m10, m11, m12], [m20, m21, m22]]


def s32(x): return ((x + 0x80000000) & 0xffffffff) - 0x80000000


def transform(m, M, pos, F, off=(0, 0, 0)):
    out = []
    for i in range(len(m['V'])):
        n = [int(x) for x in m['N'][i]]
        d0 = s32(sum(M[0][j] * n[j] for j in range(3)))
        d1 = s32(sum(M[1][j] * n[j] for j in range(3)))
        edx = (d0 & 0xffff0000) | ((d1 >> 16) & 0xffff)
        env = (-edx + 0x40004000) & 0xffffffff
        v = [int(m['V'][i][j]) + off[j] for j in range(3)]
        x = s32(sum(M[0][j] * v[j] for j in range(3)) + (pos[0] << 8))
        y = s32(sum(M[1][j] * v[j] for j in range(3)) + (pos[1] << 8))
        z = s32(sum(M[2][j] * v[j] for j in range(3)))
        depth = ((sar(z, 16) * F) >> 14) + 0x4000 & 0xffff
        ebp = s32(z - (pos[2] << 8))
        q = trunc_div(x * F, ebp)
        sx = (sar(q, 3) + ((q >> 2) & 1) + 0xa0) & 0xffff
        q = trunc_div(y * F, ebp)
        q3 = 3 * q
        sy = (sar(q3, 5) + ((q3 >> 4) & 1) + 0x64) & 0xffff
        out.append((sy | (sx << 16), env, depth))
    return out


def objects(s):
    objs = []
    for i in range(1, 3):
        o = 0x1620 + i * 0x3a
        objs.append(dict(seg=s.cs16(o), F=s.s16(0x10, o + 4), ang=[s.cs16(o + 0x1e + 2 * k) for k in range(3)],
                         pos=[s.cs32(o + 6 + 4 * k) for k in range(3)], avel=[s.s16(0x10, o + 0x24 + 2 * k) for k in range(3)]))
    g = dict(ang=[s.cs16(0x163e + 2 * k) for k in range(3)], pos=[s.cs32(0x1626 + 4 * k) for k in range(3)],
             avel=[s.s16(0x10, 0x1644 + 2 * k) for k in range(3)], vel=[s.cs32(0x1632 + 4 * k) for k in range(3)])
    return g, objs


def gsbuf(s, off, n, fmt):
    return list(struct.unpack_from('<%d%s' % (n, fmt), s.rd(GS, off, n * struct.calcsize(fmt))))


if __name__ == '__main__':
    meshes = {0x1cc2: mesh.load('p6', 0x1cc2), 0x202d: mesh.load('p6', 0x202d)}
    for path in sys.argv[1:]:
        s = Snap(path)
        g, objs = objects(s)
        scr = gsbuf(s, 0x1900, 608, 'I')
        env = gsbuf(s, 0xc80, 608, 'I')
        dep = [x & 0xffff for x in gsbuf(s, 0x2580, 608, 'I')]
        base = 0
        for o in objs:
            m = meshes[(o['seg'] - 0xf64) & 0xffff]
            nv = len(m['V'])
            best = None
            for k in range(0, 4):
                ga = [(g['ang'][j] - k * g['avel'][j]) & 0xffff for j in range(3)]
                oa = [(o['ang'][j] - k * o['avel'][j]) & 0xffff for j in range(3)]
                gp = [g['pos'][j] - k * g['vel'][j] for j in range(3)]
                ang = [sar(((oa[j] + ga[j]) & 0xffff) - (0x10000 if (oa[j] + ga[j]) & 0x8000 else 0), 4) for j in range(3)]
                M = matrix(*ang)
                pos = [s32(o['pos'][j] + gp[j]) for j in range(3)]
                r = transform(m, M, pos, o['F'])
                ok_s = sum(r[i][0] == scr[base + i] for i in range(nv))
                ok_e = sum(r[i][1] == env[base + i] for i in range(nv))
                ok_d = sum(r[i][2] == dep[base + i] for i in range(nv))
                if best is None or ok_s > best[1]:
                    best = (k, ok_s, ok_e, ok_d)
            if best[1] < nv:
                k = best[0]
                ga = [(g['ang'][j] - k * g['avel'][j]) & 0xffff for j in range(3)]
                oa = [(o['ang'][j] - k * o['avel'][j]) & 0xffff for j in range(3)]
                gp = [g['pos'][j] - k * g['vel'][j] for j in range(3)]
                ang = [sar(((oa[j] + ga[j]) & 0xffff) - (0x10000 if (oa[j] + ga[j]) & 0x8000 else 0), 4) for j in range(3)]
                r = transform(m, matrix(*ang), [s32(o['pos'][j] + gp[j]) for j in range(3)], o['F'])
                for i in range(nv):
                    if r[i][0] != scr[base + i]:
                        print('   v%d model %08x engine %08x' % (i, r[i][0], scr[base + i]))
            print(path, 'mesh %04x' % ((o['seg'] - 0xf64) & 0xffff), 'k=%d screen %d/%d env %d/%d depth %d/%d' % (
                best[0], best[1], nv, best[2], nv, best[3], nv))
            base += nv

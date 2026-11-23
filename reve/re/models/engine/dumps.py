"""Read the memory dumps that run_reve.py (this folder's copy, --dumps) writes, and turn them into model state."""
import glob
import os
import pickle
import re
import struct
import zlib

import engine

HERE = os.path.dirname(os.path.abspath(__file__))


class Dump:
    def __init__(self, path):
        if path.endswith('.snap'):
            import unicorn  # noqa: F401
            d = pickle.load(open(path, 'rb'))
            self.frame, self.addr, self.k, self.regs, self.drawn = d['st']['frame'], None, 0, None, d['st']['drawn']
            self.m = d['mem']
            self.path = path
            return
        d = pickle.load(open(path, 'rb'))
        self.frame, self.addr, self.k, self.regs, self.drawn = d['frame'], d['addr'], d['k'], d['regs'], d['drawn']
        self.m = zlib.decompress(d['mem'])
        self.path = path

    def rd(self, a, n): return self.m[a:a + n]
    def u8(self, a): return self.m[a]
    def u16(self, a): return struct.unpack_from('<H', self.m, a)[0]
    def s16(self, a): return struct.unpack_from('<h', self.m, a)[0]
    def u32(self, a): return struct.unpack_from('<I', self.m, a)[0]
    def s32(self, a): return struct.unpack_from('<i', self.m, a)[0]

    def buf(self, ptr): return self.m[self.u32(ptr):self.u32(ptr) + 64000]


def pairs(folder, start=0x25f5b5, end=0x25f65b):
    """(render-start dump, render-end dump) pairs: the same drawn frame (run_reve's 'drawn' counter)."""
    def lst(a):
        out = {}
        for p in glob.glob(os.path.join(folder, 'd*_%x_*.dump' % a)):
            d = pickle.load(open(p, 'rb'))
            out[d['drawn']] = p
        return out
    s, e = lst(start), lst(end)
    return [(s[k], e[k]) for k in sorted(s) if k in e]


def state(d, world):
    """Fill the model world from a dump: object positions and angles; returns the camera list."""
    base = d.u32(0x217460)
    for i, o in enumerate(world.objects):
        a = base + 0x12c * i
        o.pos16 = [d.s32(a + 0x1c + 4 * k) for k in range(3)]
        o.ang = [d.s32(a + 0x10 + 4 * k) for k in range(3)]
        o.mesh = d.u32(a)
        o.addr = a
        for k, cn in enumerate(o.corners):
            cn[6] = d.s16(a + 0x7c + 0x16 * k + 0x12)
            cn[7] = d.s16(a + 0x7c + 0x16 * k + 0x14)
    cam = [d.s32(0x217474), d.s32(0x217478), d.s32(0x21747c), d.s32(0x21748c), d.s32(0x217490), d.s32(0x217494)]
    camf = [d.u32(0x217480), d.u32(0x217484), d.u32(0x217488), d.u32(0x217498), d.u32(0x21749c), d.u32(0x2174a0)]
    return cam, camf


def renderer(d, world):
    tex = d.rd(d.u32(0x21b140), 0x10000)
    gour = d.rd(0x21643c, 0x1000)
    r = engine.Renderer(world, tex, gour, part4=d.u8(0x2174c6) == 1, outcodes=d.rd(0x21eea8, 8))
    r.part4_cull = d.u32(0x21d43c) == 0x21dfb8
    return r


def vertices(d, o):
    """The vertex records of object o in dump d: (xc, yc, zc, sx, sy, nx, ny, nz, shade, clip)."""
    mesh = o.mesh
    nv = d.u32(mesh)
    va = d.u32(mesh + 8)
    out = []
    for i in range(nv):
        a = va + 0x1e * i
        out.append(tuple(d.s16(a + k) for k in (0xa, 0xc, 0xe, 0x10, 0x12, 0x14, 0x16, 0x18)) +
                   (d.u16(a + 0x1a), d.u16(a + 0x1c)))
    return out

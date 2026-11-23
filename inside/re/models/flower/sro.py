"""SurRender .sro reader as the engine's loader 0x410bb0 sees the file (scale 0x10000 = 1.0), and helpers to read the
part's objects from a snapshot.

file: +0 dword file size, +4 dword offset of the vertices (0x14), +8 dword vertex count, +0xc dword offset of the
faces, +0x10 dword face count; vertex = 12 bytes: int16 x, y, z, uint16 u, uint16 v, 2 bytes (unused by the loader);
face = 8 bytes: word (unused by the loader), word a, b, c (vertex indices).
object vertex (64 bytes): +0 next, +4 flags, +8 x, +0xc y, +0x10 z (int), +0x28 int16 u >> 4, +0x2a int16 -(v >> 4),
+0x30 word (the flower part writes the tunnel's light level here), +0x34/+0x38/+0x3c normal.
object face (52 bytes): +0 next, +8 draw routine, +0x10 material, +0x14/+0x18/+0x1c vertex pointers,
+0x20/+0x24/+0x28 normal, +0x30 flags (bit 0 = hidden).
"""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
sys.path.insert(0, RE)
import datfile  # noqa: E402

_dat = None


def dat():
    global _dat
    if _dat is None:
        _dat = datfile.Dat(os.path.join(RE, '..', 'original', 'INSIDE.DAT'))
    return _dat


def load(name):
    s = dat().get(name)
    size, voff, nv, foff, nf = struct.unpack('<5I', s[:20])
    v = np.frombuffer(s[voff:voff + 12 * nv], np.dtype([('x', '<i2'), ('y', '<i2'), ('z', '<i2'), ('u', '<u2'),
                                                         ('v', '<u2'), ('w', '<u2')]))
    f = np.frombuffer(s[foff:foff + 8 * nf], np.dtype([('m', '<u2'), ('a', '<u2'), ('b', '<u2'), ('c', '<u2')]))
    return v, f


def obj_from_mem(mem, obj):
    """vertices (n, 16 dwords) and faces (n, 13 dwords) of an engine object in a memory image."""
    rd = lambda a: struct.unpack('<I', mem[a:a + 4])[0]
    verts, faces = [], []
    p = rd(obj + 0xd0)
    while p:
        verts.append(np.frombuffer(mem[p:p + 64], np.int32).copy())
        p = rd(p)
        if len(verts) > 5000:
            break
    p = rd(obj + 0xd8)
    while p:
        faces.append((p, np.frombuffer(mem[p:p + 52], np.int32).copy()))
        p = rd(p)
        if len(faces) > 5000:
            break
    return verts, faces

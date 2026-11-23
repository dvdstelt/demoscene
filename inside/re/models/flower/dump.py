"""Reader for the .dump files of run_flower.py (records: 4-byte tag, refresh, length, payload)."""
import struct

import numpy as np

GLOB = struct.Struct('<IIifiifIIIII')   # u0 v0 count phase k active spin f(float) A B part_ticks irqs


def records(path):
    data = open(path, 'rb').read()
    p = 0
    while p + 12 <= len(data):
        tag, frame, n = struct.unpack_from('<4sII', data, p)
        yield tag.decode(), frame, data[p + 12:p + 12 + n]
        p += 12 + n


def glob(payload):
    u0, v0, count, phase, k, active, spin, f, A, B, pt, irqs = GLOB.unpack_from(payload, 0)
    f = struct.unpack('<f', struct.pack('<I', f))[0]
    return dict(u0=u0, v0=v0, count=count, phase=phase, k=k, active=active, spin=spin, f=f, A=A, B=B, part_ticks=pt,
                irqs=irqs)


def obj(b):
    """engine object (0x110 bytes): position +0x10..+0x18, matrix +0x28 (9 doubles), +0x70.., flags +0xc4."""
    pos = struct.unpack_from('<3i', b, 0x10)
    wpos = struct.unpack_from('<3i', b, 0x1c)
    m = np.array(struct.unpack_from('<9d', b, 0x28)).reshape(3, 3)
    m2 = np.array(struct.unpack_from('<9d', b, 0x70)).reshape(3, 3)
    return dict(pos=pos, wpos=wpos, m=m, m2=m2, b8=struct.unpack_from('<3i', b, 0xb8), flags=struct.unpack_from('<I', b, 0xc4)[0],
                raw=b)


def verts(b, n):
    return np.frombuffer(b[:64 * n], np.int32).reshape(n, 16)


def faces(b, n):
    return np.frombuffer(b[:52 * n], np.int32).reshape(n, 13)

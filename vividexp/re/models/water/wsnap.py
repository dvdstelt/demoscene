"""Snapshot memory access for the water models: snap(path) -> Mem with rd8/rd16/rd32/s16/s32/blk.

Unpickling a snapshot needs unicorn imported; the 64 MB memory is cached as runs/<name>.mem next to this file.
"""
import os
import pickle
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))


class Mem:
    def __init__(self, data):
        self.m = data

    def rd(self, a, n): return bytes(self.m[a:a + n])
    def rd8(self, a): return int(self.m[a])
    def rd16(self, a): return struct.unpack('<H', self.rd(a, 2))[0]
    def s16(self, a): return struct.unpack('<h', self.rd(a, 2))[0]
    def rd32(self, a): return struct.unpack('<I', self.rd(a, 4))[0]
    def s32(self, a): return struct.unpack('<i', self.rd(a, 4))[0]
    def blk(self, a, n): return np.asarray(self.m[a:a + n])


def snap(path):
    if not os.path.isabs(path):
        path = os.path.join(RE, path)
    cache = os.path.join(HERE, 'runs', os.path.basename(path) + '.mem')
    if not os.path.exists(cache):
        import unicorn  # noqa: F401  (the pickle holds unicorn objects)
        s = pickle.load(open(path, 'rb'))
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        open(cache, 'wb').write(s['mem'])
    return Mem(np.memmap(cache, dtype=np.uint8, mode='r'))

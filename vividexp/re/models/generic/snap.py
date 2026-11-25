"""Read run_vivid.py snapshots (pickle; needs unicorn importable) as flat memory."""
import os
import pickle
import struct
import sys

import unicorn  # noqa: F401  (the pickle refers to unicorn's context class)

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, RE)


class Mem:
    def __init__(self, path):
        s = pickle.load(open(path, 'rb'))
        self.m = s['mem']
        self.st = s['st']

    def rd(self, a, n): return self.m[a:a + n]
    def u8(self, a): return self.m[a]
    def u16(self, a): return struct.unpack_from('<H', self.m, a)[0]
    def s16(self, a): return struct.unpack_from('<h', self.m, a)[0]
    def u32(self, a): return struct.unpack_from('<I', self.m, a)[0]
    def s32(self, a): return struct.unpack_from('<i', self.m, a)[0]


def snap(frame, d='snaps2'):
    return Mem(os.path.join(RE, d, 'f%d.snap' % frame))

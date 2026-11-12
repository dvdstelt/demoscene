"""Snapshot helpers for the parts2d-b models: load a snapshot (needs unicorn importable), read memory."""
import os, pickle, struct, sys
import unicorn  # noqa: F401  (the pickles reference unicorn context objects)

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.abspath(os.path.join(HERE, '..', '..'))
IMG = open(os.path.join(RE, 'unpacked', 'stars_img.bin'), 'rb').read()


def load(name):
    p = name if os.path.exists(name) else os.path.join(RE, 'snaps', name)
    return pickle.load(open(p, 'rb'))


class Mem:
    def __init__(self, b): self.b = b
    def u8(self, a): return self.b[a]
    def s8(self, a): return struct.unpack_from('<b', self.b, a)[0]
    def u16(self, a): return struct.unpack_from('<H', self.b, a)[0]
    def s16(self, a): return struct.unpack_from('<h', self.b, a)[0]
    def u32(self, a): return struct.unpack_from('<I', self.b, a)[0]
    def s32(self, a): return struct.unpack_from('<i', self.b, a)[0]
    def blk(self, a, n): return self.b[a:a + n]


def ovl(n):
    return open(os.path.join(RE, 'unpacked', 'files', '%02d.bin' % n), 'rb').read()

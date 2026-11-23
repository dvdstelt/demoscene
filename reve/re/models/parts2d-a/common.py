"""Shared helpers for the parts2d-a models: the image, snapshots, the capture, the archive's files."""
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, RE)
import rep  # noqa: E402
from sheet import open_frames, indexed, rgb, read_ticks  # noqa: E402

FILES = os.path.join(RE, 'unpacked', 'files')


def snap(path):
    import unicorn  # noqa: F401  (unpickling needs it)
    with open(path if os.path.isabs(path) else os.path.join(RE, path), 'rb') as f:
        return pickle.load(f)


class Mem:
    """Read access to a snapshot's 16 MB."""
    def __init__(self, s):
        self.m = s['mem'] if isinstance(s, dict) else s

    def rd(self, a, n): return bytes(self.m[a:a + n])
    def u8(self, a): return self.m[a]
    def s8(self, a): return struct.unpack('<b', self.rd(a, 1))[0]
    def u16(self, a): return struct.unpack('<H', self.rd(a, 2))[0]
    def s16(self, a): return struct.unpack('<h', self.rd(a, 2))[0]
    def u32(self, a): return struct.unpack('<I', self.rd(a, 4))[0]
    def s32(self, a): return struct.unpack('<i', self.rd(a, 4))[0]
    def arr(self, a, n): return np.frombuffer(self.rd(a, n), dtype=np.uint8).copy()


def raw(name):
    """A RAW picture of the archive: (indices HxW, palette 256x3 8-bit)."""
    d = open(os.path.join(FILES, *name.split('\\')), 'rb').read()
    assert d[:6] == b'mhwanh'
    w, h, n = struct.unpack('>HHH', d[8:14])
    pal = np.frombuffer(d[32:32 + 3 * n], dtype=np.uint8).reshape(n, 3)
    px = np.frombuffer(d[32 + 3 * n:32 + 3 * n + w * h], dtype=np.uint8).reshape(h, w)
    return px.copy(), pal.copy()


def file(name):
    return open(os.path.join(FILES, *name.split('\\')), 'rb').read()


def pal6_to_rgb(pal6):
    return ((np.asarray(pal6, dtype=np.uint32) * 255 + 31) // 63).astype(np.uint8)


def frames():
    return open_frames(os.path.join(RE, 'full.frames'))


def ticks():
    return read_ticks(os.path.join(RE, 'full.ticks'))

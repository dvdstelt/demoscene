"""Snapshot and capture access for the parts3d-b models (run inside the toolbox, from stars/re or anywhere)."""
import os
import pickle
import struct
import sys

import numpy as np
import unicorn  # noqa: F401  (the pickles reference unicorn types)

RE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, RE)
import rep  # noqa: E402

IMG = rep.SEG


def load(name):
    """Load snaps/<name> (or a path); returns the dict with 'mem' as bytes."""
    p = name if os.path.exists(name) else os.path.join(RE, 'snaps', name)
    with open(p, 'rb') as f:
        s = pickle.load(f)
    return s


class Mem:
    def __init__(self, mem):
        self.m = mem if isinstance(mem, (bytes, bytearray)) else bytes(mem)

    def u8(self, a): return self.m[a]
    def s8(self, a): return struct.unpack_from('<b', self.m, a)[0]
    def u16(self, a): return struct.unpack_from('<H', self.m, a)[0]
    def s16(self, a): return struct.unpack_from('<h', self.m, a)[0]
    def u32(self, a): return struct.unpack_from('<I', self.m, a)[0]
    def s32(self, a): return struct.unpack_from('<i', self.m, a)[0]
    def blk(self, a, n): return self.m[a:a + n]
    def arr(self, a, n, dt=np.uint8): return np.frombuffer(self.m, dtype=dt, count=n, offset=a).copy()


def frames(first, count=1):
    """Read capture records from full.frames: list of (pixels 200x320 uint8, palette 256x3 6-bit)."""
    rec = 64000 + 768
    out = []
    with open(os.path.join(RE, 'full.frames'), 'rb') as f:
        for k in range(count):
            f.seek(32 + (first + k) * rec)
            b = f.read(rec)
            px = np.frombuffer(b[:64000], np.uint8).reshape(200, 320)
            pal = np.frombuffer(b[64000:], np.uint8).reshape(256, 3)
            out.append((px, pal))
    return out


def ticks():
    """full.ticks as a list of rows (strings split)."""
    rows = []
    with open(os.path.join(RE, 'full.ticks')) as f:
        for line in f:
            if line.startswith('#'): continue
            rows.append(line.split())
    return rows

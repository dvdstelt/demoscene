"""Shared helpers for the landscape models: the program image, snapshots, the capture, 32-bit arithmetic."""
import os, sys, struct, pickle
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, RE)
import unicorn  # noqa: F401  (snapshots need it to unpickle)
from sheet import open_frames

IMG = open(os.path.join(RE, 'unpacked', 'korso2.bin'), 'rb').read()
C32 = 0x8fc0


def fi32(a):
    return struct.unpack_from('<i', IMG, 0xec0 + a)[0]


def fu8(a):
    return IMG[0xec0 + a]


def load_snap(name):
    return pickle.load(open(os.path.join(RE, 'snaps', name + '.snap'), 'rb'))


class Mem:
    """code32 view of a snapshot's memory"""
    def __init__(self, snap):
        self.m = snap['mem']

    def i32(self, a):
        return struct.unpack_from('<i', self.m, C32 + a)[0]

    def u32(self, a):
        return struct.unpack_from('<I', self.m, C32 + a)[0]

    def u8(self, a):
        return self.m[C32 + a]

    def arr(self, a, n, dt):
        return np.frombuffer(bytes(self.m[C32 + a:C32 + a + n * np.dtype(dt).itemsize]), dt).copy()


def s32(x):
    x &= 0xffffffff
    return x - (1 << 32) if x & 0x80000000 else x


def idiv_shl8(a, d):
    """cdq; shl eax, 8; idiv d  (edx = sign of a before the shift)"""
    hi = -1 if s32(a) < 0 else 0
    lo = (a << 8) & 0xffffffff
    num = (hi << 32) | lo
    q = abs(num) // abs(d)
    if (num < 0) != (d < 0):
        q = -q
    assert -(1 << 31) <= q < (1 << 31), 'divide overflow'
    return q


class Capture:
    def __init__(self):
        self.raw, self.W, self.H, self.rec, self.hdr, self.n = open_frames(os.path.join(RE, 'full.frames'))

    def idx(self, k):
        o = self.hdr + k * self.rec
        return np.asarray(self.raw[o + 776:o + 776 + self.W * self.H]).reshape(self.H, self.W)[:200, :320].copy()

    def pal(self, k):
        o = self.hdr + k * self.rec
        return np.frombuffer(bytes(self.raw[o + 8:o + 776]), np.uint8).reshape(256, 3).copy()


def read_ticks_full():
    rows = {}
    for line in open(os.path.join(RE, 'full.ticks')):
        if line.startswith('#'):
            continue
        f = line.split()
        rows[int(f[0])] = dict(t=float(f[1]), order=int(f[2]), row=int(f[3]), irqs=int(f[9]))
    return rows

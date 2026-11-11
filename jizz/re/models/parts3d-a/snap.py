"""Snapshot / capture access for the parts3d-a models (run inside the toolbox)."""
import os, sys, pickle, struct
import numpy as np
RE = '/home/dennis/src/dvdstelt/demoscene/jizz/re'
sys.path.insert(0, RE)
import unicorn  # noqa: needed to unpickle
import rep

_cache = {}
def load(tick):
    if tick not in _cache:
        with open(os.path.join(RE, 'snaps', 't%d.snap' % tick), 'rb') as f:
            _cache[tick] = pickle.load(f)
    return _cache[tick]

class Mem:
    def __init__(self, b): self.b = b
    def u8(self, a): return self.b[a]
    def u16(self, a): return struct.unpack_from('<H', self.b, a)[0]
    def s16(self, a): return struct.unpack_from('<h', self.b, a)[0]
    def u32(self, a): return struct.unpack_from('<I', self.b, a)[0]
    def s32(self, a): return struct.unpack_from('<i', self.b, a)[0]
    def f32(self, a): return struct.unpack_from('<f', self.b, a)[0]
    def arr(self, a, n, dt): return np.frombuffer(self.b, dtype=dt, count=n, offset=a)

def mem(tick):
    return Mem(load(tick)['mem'])

FR = os.path.join(RE, 'full.frames')
def frame(t):
    """16-bit page shown at the end of tick t, as uint16 (200,320)."""
    with open(FR, 'rb') as f:
        f.seek(32 + t * 128000)
        return np.frombuffer(f.read(128000), dtype='<u2').reshape(200, 320)

def rgb565(px):
    px = px.astype(np.uint32)
    r, g, b = (px >> 11) & 31, (px >> 5) & 63, px & 31
    return np.stack([r * 255 // 31, g * 255 // 63, b * 255 // 31], -1).astype(np.uint8)

def ticks():
    rows = []
    for line in open(os.path.join(RE, 'full.ticks')):
        p = line.split()
        if not p or not p[0].isdigit(): continue
        rows.append(p)
    return rows

def flips():
    rows = []
    for line in open(os.path.join(RE, 'full.flips')):
        p = line.split()
        if not p or not p[0].isdigit(): continue
        rows.append(p)
    return rows

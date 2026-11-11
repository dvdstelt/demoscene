"""Snapshot helpers for the parts3d-b models (run inside the toolbox, from any directory)."""
import os, pickle, struct, sys
import numpy as np
RE = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, RE)
import unicorn  # noqa: F401  (needed to unpickle)
import rep

_cache = {}
def load(t):
    if t not in _cache:
        with open(os.path.join(RE, 'snaps', f't{t}.snap'), 'rb') as f:
            _cache[t] = pickle.load(f)
    return _cache[t]

class Mem:
    def __init__(self, t):
        s = load(t); self.s = s; self.m = s['mem']
    def d(self, a): return struct.unpack_from('<I', self.m, a)[0]
    def i(self, a): return struct.unpack_from('<i', self.m, a)[0]
    def f(self, a): return struct.unpack_from('<f', self.m, a)[0]
    def b(self, a): return self.m[a]
    def fa(self, a, n): return np.frombuffer(bytes(self.m[a:a + 4 * n]), dtype='<f4')
    def ia(self, a, n): return np.frombuffer(bytes(self.m[a:a + 4 * n]), dtype='<i4')

FRAMES = os.path.join(RE, 'full.frames')
def frame(t):
    with open(FRAMES, 'rb') as f:
        f.seek(32 + t * 128000)
        a = np.frombuffer(f.read(128000), dtype='<u2').reshape(200, 320)
    r = ((a >> 11) & 31).astype(np.float32) * 255 / 31
    g = ((a >> 5) & 63).astype(np.float32) * 255 / 63
    b = (a & 31).astype(np.float32) * 255 / 31
    return a, np.stack([r, g, b], -1).astype(np.uint8)

def ticks():
    rows = []
    for l in open(os.path.join(RE, 'full.ticks')):
        p = l.split()
        if p and p[0].isdigit(): rows.append(p)
    return rows

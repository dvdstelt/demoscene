"""Read a snapshot's memory (snaps/f<frame>.snap): import snap; m = snap.Mem('f11276'); m.d32(a), m.f64(a), ...
Also helpers for the temple's state: matrices (3x3 doubles, row-major), vectors (3 doubles)."""
import os, pickle, struct, sys
import numpy as np
import unicorn  # noqa: F401  (the pickle needs it)
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
sys.path.insert(0, RE)


class Mem:
    def __init__(self, name='f11276'):
        path = name if os.path.exists(name) else os.path.join(RE, 'snaps', name + '.snap')
        s = pickle.load(open(path, 'rb'))
        self.s = s
        self.mem = s['mem']

    def rd(self, a, n): return bytes(self.mem[a:a + n])
    def d32(self, a): return struct.unpack('<I', self.rd(a, 4))[0]
    def i32(self, a): return struct.unpack('<i', self.rd(a, 4))[0]
    def i16(self, a): return struct.unpack('<h', self.rd(a, 2))[0]
    def u16(self, a): return struct.unpack('<H', self.rd(a, 2))[0]
    def f32(self, a): return struct.unpack('<f', self.rd(a, 4))[0]
    def f64(self, a): return struct.unpack('<d', self.rd(a, 8))[0]
    def ints(self, a, n): return struct.unpack('<%di' % n, self.rd(a, 4 * n))
    def floats(self, a, n): return struct.unpack('<%df' % n, self.rd(a, 4 * n))
    def mat(self, a): return np.array(struct.unpack('<9d', self.rd(a, 72))).reshape(3, 3)
    def vec(self, a): return np.array(struct.unpack('<3d', self.rd(a, 24)))

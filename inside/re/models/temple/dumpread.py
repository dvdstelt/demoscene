"""Helpers to read run_temple.py --dump pickles: d = load(path); m = Blocks(d[i]) gives d32/i32/f64/... on the
recorded memory blocks."""
import pickle, struct
import numpy as np


def load(path): return pickle.load(open(path, 'rb'))


class Blocks:
    def __init__(self, rec):
        self.rec = rec
        self.bl = rec['blocks']
        self.parts = rec['parts']

    def rd(self, a, n):
        if 0x425588 <= a and a + n <= 0x425ed4:
            o = a - 0x425588; return self.parts[o:o + n]
        for b, data in self.bl:
            if b <= a and a + n <= b + len(data):
                return data[a - b:a - b + n]
        raise KeyError(hex(a))

    def d32(self, a): return struct.unpack('<I', self.rd(a, 4))[0]
    def i32(self, a): return struct.unpack('<i', self.rd(a, 4))[0]
    def i16(self, a): return struct.unpack('<h', self.rd(a, 2))[0]
    def f32(self, a): return struct.unpack('<f', self.rd(a, 4))[0]
    def f64(self, a): return struct.unpack('<d', self.rd(a, 8))[0]
    def mat(self, a): return np.array(struct.unpack('<9d', self.rd(a, 72))).reshape(3, 3)
    def vec(self, a): return np.array(struct.unpack('<3d', self.rd(a, 24)))
    def ints(self, a, n): return struct.unpack('<%di' % n, self.rd(a, 4 * n))

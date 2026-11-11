import struct, numpy as np, os
D = os.path.dirname(os.path.abspath(__file__))
IMG = open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()[0xd00:]
class M:
    def __init__(self, t):
        self.b = open(os.path.join(D, 'm%s.bin' % t), 'rb').read() if t != 'img' else IMG
        self.pal = open(os.path.join(D, 'p%s.bin' % t), 'rb').read() if t != 'img' else None
    def d(self, a): return struct.unpack_from('<I', self.b, a)[0]
    def i(self, a): return struct.unpack_from('<i', self.b, a)[0]
    def by(self, a): return self.b[a]
    def f(self, a): return struct.unpack_from('<f', self.b, a)[0]
    def ints(self, a, n): return list(struct.unpack_from('<%di' % n, self.b, a))

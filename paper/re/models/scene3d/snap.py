import pickle, struct, sys, os
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import unicorn  # needed for unpickling ctx
SNAPDIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'snaps')
_cache = {}
def load(t):
    if t not in _cache:
        _cache[t] = pickle.load(open(os.path.join(SNAPDIR, 't%d.snap' % t), 'rb'))
    return _cache[t]
class M:
    def __init__(self, t):
        self.s = load(t); self.m = self.s['mem']
    def u32(self, a): return struct.unpack_from('<I', self.m, a)[0]
    def i32(self, a): return struct.unpack_from('<i', self.m, a)[0]
    def u8(self, a): return self.m[a]
    def i16(self, a): return struct.unpack_from('<h', self.m, a)[0]
    def blk(self, a, n): return self.m[a:a+n]

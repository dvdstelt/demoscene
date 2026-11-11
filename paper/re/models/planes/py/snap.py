import pickle, struct, sys, os
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import unicorn
SN = '/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/snaps/t%d.snap'
_c = {}
def load(t):
    if t not in _c: _c[t] = pickle.load(open(SN % t, 'rb'))
    return _c[t]
class M:
    def __init__(s, t): s.s = load(t); s.m = s.s['mem']
    def d(s, a): return struct.unpack_from('<I', s.m, a)[0]
    def i(s, a): return struct.unpack_from('<i', s.m, a)[0]
    def f(s, a): return struct.unpack_from('<f', s.m, a)[0]
    def b(s, a, n): return s.m[a:a+n]
    def ii(s, a, n): return list(struct.unpack_from('<%di' % n, s.m, a))
    def ff(s, a, n): return list(struct.unpack_from('<%df' % n, s.m, a))

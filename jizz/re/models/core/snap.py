"""snap.py: load a snapshot (needs unicorn importable) and give small accessors.
   s = load(tick); rd32(s, a), rdb(s, a, n), f32(s, a)"""
import sys, pickle, struct, os
sys.path.insert(0, '/home/dennis/src/dvdstelt/demoscene/jizz/re')
import unicorn  # noqa: F401 (the pickle refers to unicorn types)
RE = '/home/dennis/src/dvdstelt/demoscene/jizz/re'
_cache = {}
def load(t):
    if t not in _cache:
        _cache[t] = pickle.load(open(os.path.join(RE, 'snaps', f't{t}.snap'), 'rb'))
    return _cache[t]
def rd32(s, a): return struct.unpack_from('<I', s['mem'], a)[0]
def rds32(s, a): return struct.unpack_from('<i', s['mem'], a)[0]
def rdb(s, a, n): return bytes(s['mem'][a:a + n])
def f32(s, a): return struct.unpack_from('<f', s['mem'], a)[0]

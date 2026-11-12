"""Count which filler path (texture+env / env only, clipped or not) the faces of a frame take.
usage: python3 models/engine/paths.py <part> <dumpdir>/fNNNNN"""
import glob, os, sys
from collections import Counter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dumpview import Dump
from engine_model import Engine, PARTS
part, stem = sys.argv[1], sys.argv[2]
f = lambda n: (glob.glob(stem + '_*_%s.npz' % n) or [None])[0]
pre = Dump(f('pre'), part)
mid = Dump(f('mid'), part) if f('mid') else pre
e = Engine(pre, part, mid)
e.build()
e.sort()
cnt = Counter()
segs = Counter()
for i in e.order:
    key, A, B, C = e.faces[i]
    t = e.segtex[B & 3]
    cnt['texture+env' if (t or not e.c['envonly']) else 'env only'] += 1
    segs['env %04x tex %04x' % (e.segenv[C & 3], t)] += 1
print(part, pre.frame, dict(cnt), dict(segs))

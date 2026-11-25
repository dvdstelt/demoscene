"""Every draw's object state in a --gen-dump log (pos, X Y Z A B C) against the model's spline from t (per object:
each object reads the effect counter itself, so a retrace during the object loop gives later objects t + 1)."""
import sys
import render
from gen import idiv

prefix, ini = sys.argv[1], sys.argv[2]
sc = render.Scene(ini)
n = bad = mixed = 0
for d in render.read_gentxt(prefix + '.gentxt'):
    ts = [sc.t_from_pos(o, d['objs'][i][0]) for i, o in enumerate(sc.objs)]
    if len(set(ts)) > 1:
        mixed += 1
        print('draw %d: per-object t %s' % (d['n'], ts))
    for i, o in enumerate(sc.objs):
        t = ts[i]
        pos, vals = sc.state(o, t)
        n += 1
        if [pos] + vals != d['objs'][i]:
            bad += 1
            if bad < 5:
                print('draw', d['n'], 'object', i, 'model', pos, vals, 'emulator', d['objs'][i])
print('object states checked %d, different %d; draws with more than one t: %d' % (n, bad, mixed))

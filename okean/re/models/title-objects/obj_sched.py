from vlog import *
import pickle
ev = parse('vlog2.txt')
ds = draws(ev)
print(len(ds), 'copies; first interval', ds[0]['frame'], 'last', ds[-1]['frame'])
prev = None
gaps = []; split = []
for r in ds:
    c0 = r['irq0'] - 617
    oi = sorted(set(o['irq'] - 617 for o in r['objs']))
    if prev is not None and r['frame'] != prev['frame'] + 1:
        gaps.append(r['frame'])
    if oi != [c0]:
        split.append((r['frame'], c0, oi))
    prev = r
print('intervals without a copy (the record repeats the previous one):', len(gaps), [g - 1 for g in gaps])
print('draws whose objects saw a callback after the draw start:', len(split), split[:40])
pickle.dump(ds, open('sched2.pkl', 'wb'))

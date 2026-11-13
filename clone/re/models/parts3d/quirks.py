"""How often the cube quirks matter in a --p3d dump of part 9 or 10: cubes, full cubes, hidden faces, faces hidden
by the x+2 test that an x+1 test would show (and the reverse), part 10's aliased cells and reads past the array."""
import os, sys, pickle
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import model
recs = [r for r in pickle.load(open(sys.argv[1], 'rb')) if 'after' in r]
part = recs[0]['part']
st = dict(frames=0, cubes=0, full=0, hidden=0, x2_hides=0, x2_shows=0, past=0, past_full=0, alias_cubes=0)
for r in recs:
    st['frames'] += 1
    if part == 9:
        G = np.frombuffer(r['grid'], '<f4').reshape(8, 8, 8); flat = G.reshape(-1)
        for z in range(1, 7):
            for y in range(1, 7):
                for x in range(1, 7):
                    s = G[z, y, x] / 6
                    if s <= 0.5: continue
                    st['cubes'] += 1
                    if s < 15: continue
                    st['full'] += 1
                    x2 = flat[z * 64 + y * 8 + x + 2] >= 90; x1 = G[z, y, x + 1] >= 90
                    st['x2_hides'] += int(x2 and not x1); st['x2_shows'] += int(x1 and not x2)
    else:
        g = np.frombuffer(r['grid'], '<f4')
        extra = {216 + i: v for i, v in enumerate(np.frombuffer(r['grid'][216 * 4:], '<f4'))}
        for a in range(1, 5):
            for b in range(1, 11):
                for e in range(1, 5):
                    k = 36 * a + 6 * b + e
                    s = g[k] / 6
                    if s <= 0.5: continue
                    st['cubes'] += 1
                    st['alias_cubes'] += int(a >= 2 and b <= 4)
                    if s < 15: continue
                    st['full'] += 1
                    for kk in (k - 36, k + 36, k - 6, k + 6, k - 1, k + 2):
                        if kk >= 209:
                            st['past'] += 1
                            st['past_full'] += int(extra.get(kk, 0) >= 90)
                    x2 = g[k + 2] >= 90; x1 = g[k + 1] >= 90 if e < 4 else False
                    st['x2_hides'] += int(x2 and not x1); st['x2_shows'] += int(x1 and not x2)
print('part', part, st)

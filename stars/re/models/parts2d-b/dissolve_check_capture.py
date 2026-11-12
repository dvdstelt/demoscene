"""Compare the dissolve model's screens and palettes with full.frames."""
import pickle, os
import numpy as np
import snap, dissolve_model as dm

out = dm.run('runs/d.pkl', True)
ev = pickle.load(open('runs/d.pkl', 'rb'))['events']
ends = [e[1] for e in ev if e[0] == 'E'][-len(out):]
raw = np.memmap(os.path.join(snap.RE, 'full.frames'), np.uint8, 'r', 32); REC = 64768
pal = dm.palette().astype(int)
bad_v = bad_p = 0; f0, f1 = ends[0], ends[-1]
for f in range(f0, f1):
    rec = raw[f * REC:(f + 1) * REC]
    k = max(i for i, x in enumerate(ends) if x <= f)
    bad_v += not np.array_equal(rec[:64000], out[k])
    p = rec[64000:].reshape(256, 3).astype(int)
    bad_p += not any(np.array_equal(np.minimum(63, (pal * L) >> 8), p) for L in range(0, 257, 4))
print('records %d..%d: VGA differs in %d, palette not pal*L>>8 in %d' % (f0, f1 - 1, bad_v, bad_p))
levels = [(e[4], e[1]) for e in ev if e[0] == 'P']
print('palette levels (frame, level) first / at the fade-out:', levels[:3], [l for l in levels if l[1] < 256][-20:])
F = [e for e in ev if e[0] == 'F']
print('first frame', F[0], 'last', F[-1])

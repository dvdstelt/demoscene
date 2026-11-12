"""Compare the particle model's frames and palettes with the capture full.frames (VGA and DAC per displayed frame).
usage: python3 particles_check_capture.py runs/p1.pkl 1 f18451_133ad.snap"""
import sys, pickle, os
import numpy as np
import snap, particles_model as pm

path, variant, sn = sys.argv[1], int(sys.argv[2]), sys.argv[3]
out = pm.run(path, variant, sn, True)
h = pickle.load(open(path, 'rb')); ev = h['events']
raw = np.memmap(os.path.join(snap.RE, 'full.frames'), np.uint8, 'r', 32)
REC = 64768
ends = [e[1] for e in ev if e[0] == 'E']
base = np.frombuffer(snap.IMG, np.uint8, 96, 0x13048 if variant == 1 else 0x130a8).reshape(32, 3).astype(int)
levels = {}
lv = None
for e in ev:
    if e[0] == 'P': levels[e[4]] = e[1]
f0, f1 = ends[0], ends[-1]
bad_v = bad_p = 0; n = 0
for f in range(f0, f1 + 1):
    rec = raw[f * REC:(f + 1) * REC]
    k = max(i for i, x in enumerate(ends) if x <= f)
    if not np.array_equal(rec[:64000], out[k]):
        bad_v += 1; print("record", f, "differs from model frame", k, "(drawn at", ends[k], ")", (rec[:64000] != out[k]).sum(), "pixels; next model frame matches:", k + 1 < len(out) and np.array_equal(rec[:64000], out[k + 1]))
    pal = rec[64000:64000 + 96].reshape(32, 3).astype(int)
    # the palette shown in record f was set by the 0x4a7b0 call of the frame drawn in record f-? : find a level
    ok = any(np.array_equal(np.minimum(63, (base * L) >> 8), pal) for L in range(0, 257, 4))
    bad_p += not ok
    n += 1
print('records %d..%d: %d with VGA != model, %d with a palette that is not base*L>>8 (L = 0, 4, .., 256)' % (f0, f1, bad_v, bad_p))

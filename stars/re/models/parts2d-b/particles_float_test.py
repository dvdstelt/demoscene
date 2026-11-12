"""How much does the zoom's truncation (>> 2 of the 4-neighbour sum) matter? Runs variant 1 with the same random
blobs and words three ways: the original integer step, a float step without truncation, and a float step minus a
constant 3/8 (the mean truncation loss). Prints the mean level and the share of pixels >= 16 every 100 frames and
writes a comparison image (rows: integer, float, float - 3/8)."""
import pickle
import numpy as np
from PIL import Image
import snap, particles_model as pm

ev = pickle.load(open('runs/p1.pkl', 'rb'))['events']
sm = snap.Mem(snap.load('f18451_133ad.snap')['mem'])
I = pm.SRC_IDX


def step_float(s, bias):
    v = (s[I - 1] + s[I + 1] + s[I - 320] + s[I + 320]) / 4 - bias
    return np.maximum(v, 0)


bufs = [np.zeros(64000, np.uint8), np.zeros(64000), np.zeros(64000)]
rng = pm.Rng(sm.u32(0x4af35)); idx = None; shots = []
k = 0
for e in ev:
    if e[0] == 'F':
        for _ in range(10):
            y = rng(100) + 0x32; x = rng(0xa0) + 0x50
            col = 0 if rng(0x100) & 1 else 0x1f
            for b in bufs: pm.blob(b, y * 320 + x, col, pm.SMALL)
        rng(0xfa0); txt = False
    elif e[0] == 't170': txt = e[1] != 0
    elif e[0] == 'idx' and txt:
        for b in bufs:
            t = np.zeros(64000, np.uint8); pm.text_entry(t, 1, e[1]); b[t != 0] = 31
    elif e[0] == 'E':
        bufs = [pm.zoom(bufs[0]), step_float(bufs[1], 0), step_float(bufs[2], 0.375)]
        k += 1
        if k % 100 == 0 or k in (60, 150):
            print('frame %3d  mean: int %.2f float %.2f float-3/8 %.2f   >=16: %.3f %.3f %.3f' % (
                (k,) + tuple(b.mean() for b in bufs) + tuple((b >= 16).mean() for b in bufs)))
        if k in (60, 150, 350):
            shots.append(np.vstack([np.clip(b, 0, 31).reshape(200, 320) * 8 for b in bufs]))
Image.fromarray(np.hstack(shots).astype(np.uint8)).save('particles_float_test.png')

"""Check the model against run_bump.py's per-draw dumps.
usage: python3 check_draws.py runs/suomi bump/bump1.ini
1. every dumped screen against fx.frame() with the logged words and light offsets (the drawers);
2. the logged state against State.step() driven by the frame counter (RunBUMPEFEK's loop)."""
import struct
import sys

import numpy as np

import bump

pre = sys.argv[1]
fx = bump.Bump(sys.argv[2])
recs = [l.split() for l in open(pre + '.draws.txt') if not l.startswith('#')]
done = [r for r in recs if r[0] == 'done']
raw = open(pre + '.draws', 'rb').read()
n = len(raw) // 64012
print('draws', n, len(done))
bad = 0
prev = None
for k in range(n):
    fr, cnt, t = struct.unpack_from('<IIf', raw, k * 64012)
    scr = np.frombuffer(raw, np.uint8, 64000, k * 64012 + 12).reshape(200, 320)
    r = done[k]
    st = bump.State()
    st.b_word, st.a_word = int(r[6]), int(r[7])
    st.ang_x, st.ang_y = float(r[8]), float(r[9])
    lx, ly = st.light(fx)
    if (lx, ly) != (int(r[10]), int(r[11])):
        print('light differs at', k, (lx, ly), r[10:12])
    m = fx.frame(st, prev=scr)          # border pixels are not drawn: take them from the screen
    d = m != scr
    if d.any():
        bad += 1
        if bad < 5:
            ys, xs = np.nonzero(d)
            print('draw %d frame %d: %d pixels differ, e.g. (%d,%d) model %d screen %d' % (
                k, fr, d.sum(), xs[0], ys[0], m[ys[0], xs[0]], scr[ys[0], xs[0]]))
print('screens differing:', bad, 'of', n)
# border
print('border values', np.unique(np.concatenate([scr[:2].ravel(), scr[198:].ravel(), scr[:, :2].ravel(), scr[:, 318:].ravel()])))

# state stepping: start from the first 'draw' record (b, a, angles as stored) and step by the counter differences
draws = [r for r in recs if r[0] == 'draw']
r0 = draws[0]
st = bump.State(float(r0[4]), float(r0[5]))
st.ang_x, st.ang_y = float(r0[8]), float(r0[9])
last = int(r0[3])
mism = 0
for r in draws[1:]:
    c = int(r[3])
    st.step(c - last, fx)
    last = c
    got = (st.b_word, st.a_word, st.ang_x, st.ang_y, bump.f32(st.b), bump.f32(st.a))
    want = (int(r[6]), int(r[7]), float(r[8]), float(r[9]), float(r[4]), float(r[5]))
    if got != want:
        mism += 1
        if mism < 5:
            print('state differs at counter', c, got, want)
print('state mismatches', mism, 'of', len(draws) - 1)
steps = np.diff([int(r[3]) for r in draws])
print('refreshes per drawn frame:', dict(zip(*np.unique(steps, return_counts=True))), 'mean %.3f' % steps.mean())

"""Script-step timing from a hookrun npz: retrace count at each "run N retraces" call vs the sum of N (overshoot),
and a check that the camera state at every drawn frame equals the closed form (initial + per-retrace velocity
summed over the steps) — p3 and p5."""
import sys
import numpy as np
d = np.load(sys.argv[1])
steps = d['steps']
print('step  frame  retr_at_call  N   cumN  overshoot')
cum = 0
for i, (fr, r, n) in enumerate(steps):
    print('%3d %7d %7d %6d %6d %4d' % (i + 1, fr, r, n, cum, r - cum - steps[0][1]))
    cum += n
o0 = d['o0'].view(np.uint8)
ang = o0[:, 0x1e:0x24].copy().view('<i2'); vel_a = o0[:, 0x24:0x2a].copy().view('<i2')
pos = o0[:, 6:0x12].copy().view('<i4'); vel = o0[:, 0x12:0x1e].copy().view('<i4')
retr = d['retr']
# angles advance by the angular velocity every retrace: check consecutive rows
bad = 0
for k in range(1, len(retr)):
    dr = retr[k] - retr[k - 1]
    if dr <= 0:
        continue
    same = (vel_a[k] == vel_a[k - 1]).all() and (vel[k] == vel[k - 1]).all()
    if same:
        ea = (ang[k - 1].astype(np.int64) + dr * vel_a[k].astype(np.int64)) & 0xffff
        ep = pos[k - 1].astype(np.int64) + dr * vel[k].astype(np.int64)
        if not ((ea == ang[k].astype(np.int64) & 0xffff).all() and (ep == pos[k]).all()):
            bad += 1
print('rows', len(retr), 'state mismatches vs per-retrace integration', bad)
print('drawn frames per retrace: retr deltas', np.bincount(np.diff(retr))[:5])
print('first row: frame', d['frame'][0], 'retr', retr[0], 'ang', ang[0], 'pos', pos[0])
print('last row: frame', d['frame'][-1], 'retr', retr[-1], 'ang', ang[-1], 'pos', pos[-1])
print('frame (transform) -> frame (copy) lag', np.bincount(d['frame_copy'] - d['frame'])[:4], 'retr lag', np.bincount(d['retr_copy'] - d['retr'])[:4])

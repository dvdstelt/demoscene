"""Where the camera is relative to the tunnel's rings (inside or outside the tube), and which part of the tube the
part's face window (0x40940c) leaves visible, per tick."""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lws  # noqa: E402
import sro  # noqa: E402

v, f = sro.load('flower/tunneli1.sro')
P = np.stack([v['x'], v['y'], v['z']], 1).astype(float).reshape(61, 12, 3)
C = P.mean(1)
Rr = np.linalg.norm(P - C[:, None], axis=2)
print('rings 61 x 12 vertices; ring radius min %.0f max %.0f mean %.0f; ring spacing %.0f..%.0f' % (
    Rr.min(), Rr.max(), Rr.mean(), np.linalg.norm(np.diff(C, axis=0), axis=1).min(),
    np.linalg.norm(np.diff(C, axis=0), axis=1).max()))
print('ring 0 centre', np.round(C[0]), 'ring 60 centre', np.round(C[60]))
tab, n = lws.tables()[0]
f32 = np.float32
fr = f32(0)
cnt = 0
for tick in range(1, 2101):
    cnt += 1
    fr = f32(float(fr) + 0.15)
    if fr > 260:
        fr, cnt = f32(0), 0
    if tick % 60 and tick not in (1733, 1734, 1735):
        continue
    pos, ang = lws.camera_at(tab, n, fr)
    p = np.array(pos, float)
    d = np.linalg.norm(C - p, axis=1)
    r = int(np.argmin(d))
    # distance from the local axis (segment between ring centres)
    axis = C[min(r + 1, 60)] - C[max(r - 1, 0)]
    axis /= np.linalg.norm(axis)
    off = p - C[r]
    radial = np.linalg.norm(off - axis * (off @ axis))
    nwin = int(cnt * 0.75)
    print('tick %4d refresh %4d f %6.2f camera %s nearest ring %2d, %4.0f from its axis (ring radius %4.0f) %s; '
          'visible faces %d..%d (segments %.1f..%.1f)' % (
              tick, 4962 + tick, fr, pos, r, radial, Rr[r].mean(), 'INSIDE' if radial < Rr[r].min() else 'outside',
              min(nwin + 1, 1092) if cnt else 0, min(nwin + 348, 1439) if cnt else 349,
              (min(nwin + 1, 1092) if cnt else 0) / 24, (min(nwin + 348, 1439) if cnt else 349) / 24))

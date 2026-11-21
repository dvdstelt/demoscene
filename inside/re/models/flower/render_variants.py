"""Try variants of render.py (texture orientation, affine or perspective-correct interpolation) on the frames
where the flower is hidden and report, per frame, the share of equal pixel indices among the pixels that are not
black in the capture.

usage: python3 models/flower/render_variants.py models/flower/run/part.dump
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import render as R  # noqa: E402
import dump  # noqa: E402
from sheet import open_frames, indexed  # noqa: E402

raw, n = open_frames(os.path.join(HERE, '..', '..', 'full.frames'))
T2o = R.T2.copy()
recs = []
uv0 = None
for tag, frame, pl in dump.records(sys.argv[1]):
    if tag == 'MRPH':
        g = dump.glob(pl); uv0 = (g['u0'], g['v0'])
    if tag == 'DRWN' and uv0 is not None and (5590 <= frame <= 5660 or 6790 <= frame <= 6900):
        recs.append((frame, pl, uv0))
recs = recs[::3]
variants = [('affine, as read', T2o, False), ('affine, transposed', T2o.T.copy(), False),
            ('perspective, as read', T2o, True)]
for name, tex, persp in variants:
    R.T2, R.PERSPECTIVE = tex, persp
    out = []
    for frame, pl, uv in recs:
        img = R.render(pl, uv)
        best = 0
        for k in (frame, frame + 1):
            cap = indexed(raw, k)[0]
            m = cap != 0
            best = max(best, np.count_nonzero((cap == img) & m) / max(1, np.count_nonzero(m)))
        out.append(best * 100)
    print('%-22s %s' % (name, ' '.join('%d:%.0f' % (f, e) for (f, _, _), e in zip(recs, out))))

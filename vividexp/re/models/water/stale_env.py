"""How visible is the stale environment map? For drawn frames in which no letter is behind the transparent heart,
render with the env map left by the last frame that had one (what the original shows) and with the clean right half
of watenv.pcx, and count the pixels that differ; writes runs/stale_<i>.png (original | clean | difference)."""
import os
import sys

import numpy as np
from PIL import Image

import wdata
import wlog
import wmodel
from check_frames import env_after

D = wdata.load()
F = wlog.frames()
pal = D['pal']
lut = (pal.astype(np.uint32) * 255 + 31) // 63
for i in [int(a) for a in sys.argv[1:]]:
    f = F[i]
    st = env_after(i - 1)
    a, _, _ = wmodel.render(D, f['counts'], env_state=st)
    b, _, _ = wmodel.render(D, f['counts'])
    d = a != b
    print('frame', i, 'record', f['shown'], 'behind', f['behind'], 'pixels differing', int(d.sum()))
    img = np.hstack([lut[a], lut[b], np.repeat(d[:, :, None] * 255, 3, 2)]).astype(np.uint8)
    Image.fromarray(img).save(os.path.join(wlog.HERE, 'runs', 'stale_%d.png' % i))

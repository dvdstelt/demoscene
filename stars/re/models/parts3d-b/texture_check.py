"""Part 0xa0ca: the texture the engine reads ([0xa04e], 256x256) must be morph frame k of 0xa5c5 tiled 4 x 2
(0xa57d), with k = [0xa076] >> 3 as seen by 0xa530. usage: texture_check.py DUMP..."""
import pickle
import sys

import numpy as np

import snap
import textures_a0ca as T

if __name__ == '__main__':
    m23 = T.F(23).reshape(256, 256); m21 = T.F(21).reshape(256, 256)
    frames = T.morph_frames(m23, m21)
    for path in sys.argv[1:]:
        m = snap.Mem(pickle.load(open(path, 'rb'))['mem'])
        tex = m.arr(m.u32(0xa04e), 65536).reshape(256, 256)
        hits = [k for k in range(8) if (np.tile(frames[k], (2, 4)) == tex).all()]
        pre = m.arr(m.u32(0xa062), 8 * 0x2000).reshape(8, 128, 64)
        same = all((pre[k] == frames[k]).all() for k in range(8))
        print('%s: [0xa076] = %d (>> 3 = %d); texture = tiled frame %s; precomputed frames at [0xa062] match the model: %s'
              % (path, m.s32(0xa076), m.s32(0xa076) >> 3, hits, same))

"""Sun position, flare visibility (0x40276c, 0x4024d0 / 0x411e80) and the camera-space coordinates the engine
hands to the sprite drawer (0x40c158), checked against every REND / AFTR pair of run1.dump: python3 sunmodel.py
"""
import math
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fdump  # noqa: E402


def sun_world(q):
    """0x40276c: control object = trunc(q); sun = (ctl.x - 0x23000, -0x5000, ctl.z)"""
    return np.array([math.trunc(q[0]) - 0x23000, -0x5000, math.trunc(q[2])], float)


def cam_space(p, cam, C):
    """0x40c158: trunc(128 * C^T (p - cam)) per axis (ints)"""
    rel = p - cam
    v = [(rel[1] * C[1, k] + rel[0] * C[0, k]) + rel[2] * C[2, k] for k in range(3)]   # 0x4144ec order
    return [math.trunc(t * 128) for t in v]


def flares_visible(p, cam, C):
    """0x4024d0 with 0x411e80: the sun's 3D projection must lie in [-12, 332) x [-12, 212) and z >= 32"""
    rel = p - cam
    v = [C[0, k] * rel[0] + C[1, k] * rel[1] + C[2, k] * rel[2] for k in range(3)]
    if 8192 * 0.00390625 > v[2]:
        return False, None
    sx = 25600 * 6.103515625e-05 * (128 * v[0]) / v[2] + 160
    sy = 21333 * 6.103515625e-05 * (128 * v[1]) / v[2] + 100
    return (-12 <= sx < 332) and (-12 <= sy < 212), (sx, sy)


if __name__ == '__main__':
    recs = [r for r in fdump.read(os.path.join(HERE, 'run1.dump')) if r.kind in (b'REND', b'AFTR')]
    pairs, last = [], None
    for r in recs:
        if r.kind == b'REND':
            last = r
        elif last is not None and r.frame < 8469:
            pairs.append((last, r))
            last = None
    n = bad_c = bad_v = vis = 0
    first_vis = []
    prev = None
    for a, b in pairs:
        camobj = a.data[fdump.SCENE + 0x50: fdump.SCENE + 0x50 + 0xc8]
        cam = np.array(struct.unpack_from('<3i', camobj, 0x10), float)
        C = np.array(struct.unpack_from('<9d', camobj, 0x28)).reshape(3, 3)
        so = b.data[64000: 64000 + 0x104]
        sun = np.array(struct.unpack_from('<3i', so, 0x10), float)      # set by 0x40276c from the control object
        if a.st(0x424fe0) == a.st(0x424fe0):                            # (same refresh: the relation holds)
            pass
        got = struct.unpack_from('<3i', so, 0xb8)
        if list(got) != cam_space(sun, cam, C):
            bad_c += 1
            if bad_c < 4:
                print('frame', a.frame, 'engine', got, 'model', cam_space(sun, cam, C))
        v, s = flares_visible(sun, cam, C)
        fl = b.data[64000 + 0x104: 64000 + 0x104 + 0x104]
        hidden = fl[0xc4] & 1
        if v == bool(hidden):
            bad_v += 1
        vis += v
        if v and not prev:
            first_vis.append(a.frame)
        prev = v
        n += 1
    print('%d frames: sun camera coordinates wrong in %d, flare visibility wrong in %d; flares shown in %d frames'
          % (n, bad_c, bad_v, vis))
    print('flares appear at refreshes', first_vis)

"""Model of the landscape grid update (0x401ab8) and the per-vertex fog (0x401c64), checked against every REND
record of run1.dump: python3 landmodel.py

Grid object: 19 x 19 vertices (18 x 18 cells, two triangles each), vertex (row r, column c) in list order
r * 19 + c, object-local x = (c - 9) * 512, z = (r - 9) * 512; the object sits at (trunc(cam.x / 512) * 512, 0,
trunc(cam.z / 512) * 512).
"""
import math
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import heightmap  # noqa: E402

UTAB = [int(k * 4096.0 * (1.0 / 18.0)) for k in range(38)]      # 0x424d80, filled in 0x401450


def pmod(a, m):
    while a < 0:
        a += m
    return a % m


def cdiv(a, b): return int(a / b) if a >= 0 else -((-a) // b)    # C division toward zero
def cmod(a, b): return a - b * cdiv(a, b)


def grid(cam, hmap, light):
    """cam = camera object position (ints). Returns object position and per-vertex x, y, z, u, v, light, fog."""
    cx, cz = cdiv(cam[0], 512), cdiv(cam[2], 512)
    hx, hz = pmod(cx, 128), pmod(cz, 128)
    ux, uz = pmod(cx, 18), pmod(cz, 18)
    out = []
    for r in range(19):
        for c in range(19):
            y = int(hmap[(hz + r) & 127, (hx + c) & 127])
            out.append([(c - 9) * 512, y, (r - 9) * 512, UTAB[ux + c], UTAB[uz + r],
                        int(light[(hz + r) & 127, (hx + c) & 127])])
    # fog 0x401c64: distance from (cam.x % 512, cam.y, cam.z % 512) in object space
    fx, fy, fz = float(np.float32(cmod(cam[0], 512))), float(np.float32(cam[1])), float(np.float32(cmod(cam[2], 512)))
    fog0 = int(np.float32(0.8) * 3328.0)                          # 2662
    for v in out:
        dist = math.sqrt(((fx - v[0]) ** 2 + (fy - v[1]) ** 2) + (fz - v[2]) ** 2)
        e = dist - fog0
        f = int(e * 0.00078125 * 63.0 * 16.0) if e >= 0 else 0
        f = (f + 0x8000 & 0xffff) - 0x8000                          # stored as a 16-bit word
        f = max(f, 0)
        v.append(min(f, 0x3f0))
    return (cx * 512, 0, cz * 512), np.array(out)


if __name__ == '__main__':
    import fdump
    hmap, _ = heightmap.heights()
    light = heightmap.light(hmap)
    recs = [r for r in fdump.read(os.path.join(HERE, 'run1.dump')) if r.kind == b'REND' and r.frame < 8468]
    bad = 0
    fogged_faces = 0
    for r in recs:
        camobj = r.data[fdump.SCENE + 0x50: fdump.SCENE + 0x50 + 0xc8]
        cam = struct.unpack_from('<3i', camobj, 0x10)
        pos, v = grid(cam, hmap, light)
        land = r.data[fdump.LAND: fdump.LAND + 0xdeec]
        got_pos = struct.unpack_from('<3i', land, 0x10)
        xyz, uv, lf = fdump.land_vertices(r)
        got = np.concatenate([xyz, uv, lf], axis=1).astype(np.int64)
        if got_pos != pos or not np.array_equal(got, v):
            bad += 1
            if bad < 3:
                print('mismatch at', r.frame, got_pos, pos, np.argwhere(got != v)[:5])
        # face rasterizers: 0x414e5c when all three vertices have fog 0, else 0x412264
        faces = np.frombuffer(land, dtype=np.uint8, count=648 * 0x34, offset=0x10c + 361 * 0x40).reshape(648, 0x34)
        fn = faces[:, 8:12].copy().view('<u4').ravel()
        base = struct.unpack_from('<I', land, 0xd0)[0]
        idx = (faces[:, 0x14:0x20].copy().view('<u4') - base) // 0x40
        want = np.where((v[idx, 6] == 0).all(axis=1), 0x414e5c, 0x412264)
        if not np.array_equal(fn, want):
            bad += 1
        fogged_faces += (fn == 0x412264).sum()
    print('%d frames: %d mismatches; on average %.1f of 648 faces fogged' % (len(recs), bad, fogged_faces / len(recs)))
    print('face 0 vertices', idx[0], 'face 1', idx[1], 'face 2', idx[2])

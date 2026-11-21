"""Model of the temple's transforms (world, camera, projection) checked against a dumped frame's vertices."""
import sys
import numpy as np
import dumpread as D
np.set_printoptions(precision=4, suppress=True, linewidth=160)

CAM, HOLD = 0x425e74, 0x425ecc


def world(m, o):
    """object world matrix and position from the dumped object (+0x70 matrix, +0x1c position as the engine left them)"""
    return m.mat(o + 0x70), np.array(m.ints(o + 0x1c, 3), float)


def check(rec, verbose=False):
    m = D.Blocks(rec)
    cam = m.d32(CAM)
    Cw, cpos = world(m, cam)
    out = []
    for oi in range(6):
        o = m.d32(0x425de0 + 4 * oi)
        Mw, pos = world(m, o)
        v0 = m.d32(o + 0xd0)
        for k in range(m.i32(o + 0xd4)):
            a = v0 + 0x40 * k
            flags = m.d32(a + 4)
            v = np.array(m.ints(a + 8, 3), float)
            pc = Cw.T @ (Mw @ v + pos - cpos)
            if pc[2] <= 128 or not flags:
                continue
            xs = 16 * (160 + 170 * pc[0] / pc[2]); ys = 16 * (100 + 170 * (200 / 240) * pc[1] / pc[2])
            sx, sy, sz = m.i32(a + 0x14), m.i32(a + 0x18), m.i32(a + 0x1c)
            out.append((oi, k, xs - sx, ys - sy, pc[2] * 128 - sz))
            if verbose and len(out) < 12:
                print(oi, k, hex(flags), 'model', np.round([xs, ys, pc[2] * 128], 2), 'engine', (sx, sy, sz),
                      'cam', m.ints(a + 0x20, 2))
    return np.array(out)


if __name__ == '__main__':
    d = D.load(sys.argv[1])
    for i, rec in enumerate(d):
        r = check(rec, verbose=(i == 0))
        print('frame', rec['frame'], 'vertices', len(r), 'max |dx| |dy| |dz|', np.abs(r[:, 2:]).max(0))

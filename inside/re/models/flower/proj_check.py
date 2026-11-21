"""Relate the tunnel's transformed vertices (fields the engine writes while drawing) to the camera model:
camera-space coordinates and the projection. Uses run_flower.py's DRWN records.

usage: python3 models/flower/proj_check.py models/flower/run/part.dump
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dump  # noqa: E402
import sro  # noqa: E402

v, f = sro.load('flower/tunneli1.sro')
P = np.stack([v['x'], v['y'], v['z']], 1).astype(float)
NV, NF = len(v), len(f)
fits = []
allres = []
shown = 0
for tag, frame, pl in dump.records(sys.argv[1]):
    if tag != 'DRWN':
        continue
    o = 0
    tun = dump.obj(pl[o:o + 0x110]); o += 0x110
    V = dump.verts(pl[o:], NV); o += 64 * NV
    F = dump.faces(pl[o:], NF); o += 52 * NF
    fl = dump.obj(pl[o:o + 0x110]); o += 0x110
    FV = dump.verts(pl[o:], 111); o += 64 * 111
    FF = dump.faces(pl[o:], 125); o += 52 * 125
    cam = dump.obj(pl[o:o + 0x110]); o += 0x110
    rot = np.frombuffer(pl[o:o + 0x30], np.float64)
    vis = (V[:, 1] & 2) != 0                   # vertex +4 bit 1: used by a visible face this frame
    M = cam['m']
    C = np.array(cam['pos'], float)
    Q = (P - C) @ M                            # camera space = M^T (p - c)
    sx, sy, z = V[:, 5], V[:, 6], V[:, 7]
    x20, y24 = V[:, 8], V[:, 9]
    if shown < 2:
        print('refresh', frame, 'camera', cam['pos'], 'visible vertices', vis.sum())
        print('  tunnel obj +0x70 matrix\n', tun['m2'], '\n  +0xb8', tun['b8'])
        print('  camera +0x70 matrix\n', cam['m2'], '\n  +0xb8', cam['b8'])
        print('  rot at 0x4b8f78', rot)
        for i in np.flatnonzero(vis)[:6]:
            print('  v%d model cam-space %s  fields +0x14..+0x24 %s  +0x28 %08x +0x2c %08x +0x30 %08x' % (
                i, np.round(Q[i], 1), V[i, 5:10], int(V[i, 10]) & 0xffffffff, int(V[i, 11]) & 0xffffffff, int(V[i, 12]) & 0xffffffff))
        shown += 1
    w = vis & (Q[:, 2] > 50) & (np.abs(z - 128 * Q[:, 2]) < 4)
    if w.sum() > 10:
        # fit: z field = a * Qz ; sx = cx16 + k * Qx / Qz
        a = np.linalg.lstsq(Q[w, 2:3], z[w].astype(float), rcond=None)[0][0]
        kx, ky = 4096.0, 4096.0 * 27306 / 32768
        ex = np.abs(sx[w] - (160 * 16 + kx * Q[w, 0] / Q[w, 2]))
        ey = np.abs(sy[w] - (100 * 16 + ky * Q[w, 1] / Q[w, 2]))
        good = (ex < 2) & (ey < 2)
        allres.append((good.sum(), w.sum()))
        if (~good).any() and shown < 4:
            i = np.flatnonzero(w)[np.flatnonzero(~good)[0]]
            print('  outlier refresh %d v%d Q %s fields %s flags %x' % (frame, i, np.round(Q[i], 1), V[i, 1:10], V[i, 1]))
            shown += 1
        rx, ry = np.median(ex), np.median(ey)
        fits.append((frame, a, kx, ky, rx, ry))
fits = np.array(fits)
ar = np.array(allres)
print('vertices within 2/16 px of the model: %d of %d' % (ar[:, 0].sum(), ar[:, 1].sum()))
print('frames fitted', len(fits))
print('z field / camera z: %.4f..%.4f' % (fits[:, 1].min(), fits[:, 1].max()))
print('sx = 2560 + kx * X/Z: kx %.3f..%.3f (px*16), max residual %.2f' % (fits[:, 2].min(), fits[:, 2].max(),
                                                                        fits[:, 4].max()))
print('sy = 1600 + ky * Y/Z: ky %.3f..%.3f (px*16), max residual %.2f' % (fits[:, 3].min(), fits[:, 3].max(),
                                                                        fits[:, 5].max()))

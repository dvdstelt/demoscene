"""The flower in camera space: its vertices' camera-space fields (+0x1c = 128 z, +0x20 = 128 x, +0x24 = 128 y * 5/6)
against R_flower * p + (0, 0, 1500), and its environment-mapping coordinates (+0x28 u, +0x2a v) against the formula of
0x41acc4.

usage: python3 models/flower/flower_cam_check.py models/flower/run/part.dump
"""
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dump  # noqa: E402
import sro  # noqa: E402
from camera_check import Rx, Ry  # noqa: E402

ANGLE = [-100.0, -70.0, -130.0, -80.0, -120.0, -90.0, -100.0, -110.0, -80.0]
NV, NF = 732, 1440
ok_pos = tot = 0
ok_uv = tot_uv = 0
shown = 0
last_m = None
frames_all = frames_n = frames_next = 0
for tag, frame, pl in dump.records(sys.argv[1]):
    if tag == 'MRPH':
        last_m = dump.glob(pl)
        continue
    if tag != 'DRWN' or last_m is None:
        continue
    o = 0x110 + 64 * NV + 52 * NF
    fl = dump.obj(pl[o:o + 0x110]); o += 0x110
    FV = dump.verts(pl[o:], 111)
    g = last_m
    R = Rx(ANGLE[g['k']] * 0.0174532925) @ Ry(float(np.float32(g['spin'])))
    p = FV[:, 2:5].astype(float)
    vis = (FV[:, 1] & 2) != 0
    for variant, Q in (('R p', p @ R.T), ('R^T p', p @ R)):
        Q = Q + np.array([0, 0, 1500.0])
        ez = np.abs(FV[:, 7] - 128 * Q[:, 2])
        ex = np.abs(FV[:, 8] - 128 * Q[:, 0])
        ey = np.abs(FV[:, 9] - 128 * Q[:, 1] * 27306 / 32768)
        good = vis & (ez < 2) & (ex < 2) & (ey < 2)
        if shown < 2:
            print('refresh %d variant %s: %d of %d visible vertices match; world pos +0x1c %s' % (
                frame, variant, good.sum(), vis.sum(), fl['wpos']))
        if variant == 'R p':
            ok_pos += good.sum(); tot += vis.sum()
    # environment mapping (0x41acc4): n2 = 2 * normal; U = trunc(n2 . r0 + 2048); V = trunc(n2 . r1 + 2048)
    # u = -(U & 0xfff) - (x20 >> 6 toward zero); v = (V & 0xfff) + (y24 >> 6 toward zero)
    n2 = 2.0 * FV[:, 13:16].astype(float)
    for variant, (r0, r1) in (('rows', (R[0], R[1])), ('cols', (R[:, 0], R[:, 1]))):
        U = np.trunc(n2 @ r0 + 2048).astype(np.int64)
        Vv = np.trunc(n2 @ r1 + 2048).astype(np.int64)
        xs = np.trunc(FV[:, 8] / 64.0).astype(np.int64)
        ys = np.trunc(FV[:, 9] / 64.0).astype(np.int64)
        u = (-(U & 0xfff) - xs) & 0xffff
        v = ((Vv & 0xfff) + ys) & 0xffff
        gu = FV[:, 10] & 0xffff
        gv = (FV[:, 10] >> 16) & 0xffff
        good = vis & (u == gu) & (v == gv)
        if shown < 2:
            print('   env map with %s of R: %d of %d match' % (variant, good.sum(), vis.sum()))
        if variant == 'rows':
            ok_uv += good.sum(); tot_uv += vis.sum()
            frames_all += int(good.sum() == vis.sum()); frames_n += 1
            if good.sum() != vis.sum():
                # the rotation may have been set by a tick between the hook and the drawing: try spin + 0.02
                R2 = Rx(ANGLE[g['k']] * 0.0174532925) @ Ry(float(np.float32(g['spin'] + 0.02)))
                U2 = np.trunc(n2 @ R2[0] + 2048).astype(np.int64)
                V2 = np.trunc(n2 @ R2[1] + 2048).astype(np.int64)
                u2 = (-(U2 & 0xfff) - xs) & 0xffff
                v2 = ((V2 & 0xfff) + ys) & 0xffff
                frames_next += int((vis & (u2 == gu) & (v2 == gv)).sum() == vis.sum())
    shown += 1
print('camera-space positions (R p + (0,0,1500)): %d of %d visible vertices' % (ok_pos, tot))
print('environment map u, v: %d of %d visible vertices; %d of %d frames all equal, %d more all equal with the next '
      'tick\'s spin' % (ok_uv, tot_uv, frames_all, frames_n, frames_next))

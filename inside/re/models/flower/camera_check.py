"""Check the camera model (lws.py) against the emulator: for every drawn frame of run_flower.py's dump, the camera
object's position (+0x10) and matrix (+0x28) after 0x4080d8, from the frame value [0x4b685c] the emulator used.
Also checks the tick-by-tick recurrence of [0x4b685c], the tunnel window counter and the texture scroll.

usage: python3 models/flower/camera_check.py models/flower/run/part.dump
"""
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dump  # noqa: E402
import lws  # noqa: E402

f32 = np.float32


def Rx(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def Ry(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def Rz(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def camera_matrix(pitch, heading, bank):
    """0x4080d8: set_rotation(0,0,0); rotY(heading) (0x4145ac); rotX(-pitch) (0x4145c4); rotZ(-bank) (0x4145dc);
    each step M = M * R."""
    return Ry(float(heading)) @ Rx(-float(pitch)) @ Rz(-float(bank))


def main():
    tabs = lws.tables()
    tab, n = tabs[0]
    path = sys.argv[1]
    worst_pos, worst_m, cnt = 0, 0.0, 0
    ticks = []
    for tag, frame, pl in dump.records(path):
        if tag == 'TICK':
            ticks.append((frame, pl))
        if tag != 'CAM ':
            continue
        g = dump.glob(pl)
        cam = dump.obj(pl[48:48 + 0x110])
        pos, (p, h, b) = lws.camera_at(tab, n, g['f'])
        dp = max(abs(a - c) for a, c in zip(pos, cam['pos']))
        M = camera_matrix(p, h, b)
        dm = np.abs(M - cam['m']).max()
        worst_pos, worst_m = max(worst_pos, dp), max(worst_m, dm)
        cnt += 1
        if cnt <= 3 or dp > 0 or dm > 1e-5:
            if cnt <= 3 or cnt < 50:
                print('refresh %d f %.4f pos emu %s model %s |dM| %.2g' % (frame, g['f'], cam['pos'], pos, dm))
    print('camera: %d drawn frames, max |position diff| %d, max |matrix diff| %.3g' % (cnt, worst_pos, worst_m))
    # tick recurrence: record = state at entry of the tick routine
    f = f32(0.0)
    count = 0
    u = 0
    bad = 0
    first = ticks[0][0] if ticks else 0
    for i, (frame, pl) in enumerate(ticks):
        import struct
        u0, v0, c, phase, k, active, spin = struct.unpack_from('<IIififf', pl, 0)
        ff = struct.unpack_from('<f', pl, 0x1c)[0]
        if i == 0:
            f, count, u = f32(ff), c, u0
            print('first tick hook at refresh %d: f %.4f count %d u0 %d' % (frame, ff, c, u0))
            continue
        # step model of the previous tick
        count += 1
        f = f32(float(f) + 0.15)
        u = (u + 0x3b) & 0xfff
        if f > 260:
            f, count = f32(0.0), 0
        if f != f32(ff) or count != c or u != u0 or u0 != v0:
            bad += 1
            if bad < 5:
                print('tick at refresh %d: emu f %r count %d u0 %d v0 %d, model %r %d %d' % (frame, ff, c, u0, v0, f, count, u))
    print('ticks: %d checked, %d mismatches (f, window counter, texture scroll)' % (len(ticks), bad))


if __name__ == '__main__':
    main()

"""Check the flower's morph (0x4114c8 with |sin(phase)|), its rotation (0x4108dc(angle[k], spin, 0)) and the event
state against run_flower.py's dump. Target objects (vertex positions and normals as the engine computed them at load)
come from the snapshot.

usage: python3 models/flower/flower_check.py models/flower/run/part.dump [snaps/f4963.snap]
"""
import math
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dump  # noqa: E402
import sro  # noqa: E402
from camera_check import Rx, Ry  # noqa: E402

ANGLE = [-100.0, -70.0, -130.0, -80.0, -120.0, -90.0, -100.0, -110.0, -80.0]   # 0x422918 (degrees)
PAIRS = [(0, k + 1) for k in range(9)]                                           # 0x4228ac
STEP = [0.02] * 9                                                                # 0x4228f4


def main():
    import unicorn  # noqa: F401
    snap = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', '..', 'snaps', 'f4963.snap')
    mem = pickle.load(open(snap, 'rb'))['mem']
    rd = lambda a: struct.unpack('<I', mem[a:a + 4])[0]
    targets = [rd(0x4b6624 + 4 * j) for j in range(10)]
    tv = {}
    for j, t in enumerate(targets):
        vs, fs = sro.obj_from_mem(mem, t)
        tv[t] = (np.array(vs), np.array([f for _, f in fs]))
    nv0 = len(tv[targets[0]][0])
    prev_extra = None
    bad = cnt = 0
    worst_rot = 0.0
    for tag, frame, pl in dump.records(sys.argv[1]):
        if tag == 'EVNT':
            addr, pt, irqs = struct.unpack('<III', pl)
            print('event %x at refresh %d (part tick %d)' % (addr, frame, pt))
            continue
        if tag != 'MRPH':
            continue
        g = dump.glob(pl)
        ob = dump.obj(pl[48:48 + 0x110])
        V = dump.verts(pl[48 + 0x110:], nv0)
        A, B = g['A'], g['B']
        ka, kb = targets.index(A), targets.index(B)
        if (ka, kb) != PAIRS[g["k"]]:
            print("refresh %d: k %d but targets %s (updated by the next tick), phase %.3f" % (frame, g["k"], (ka, kb), g["phase"]))
        w = float(np.float32(abs(math.sin(float(np.float32(g['phase']))))))
        va, vb = tv[A][0], tv[B][0]
        n = min(len(va), len(vb))
        model = np.trunc(va[:n, 2:5] + (vb[:n, 2:5] - va[:n, 2:5]).astype(float) * w).astype(np.int64)
        nmodel = np.trunc(va[:n, 13:16] + (vb[:n, 13:16] - va[:n, 13:16]).astype(float) * w).astype(np.int64)
        ok = np.array_equal(model, V[:n, 2:5]) and np.array_equal(nmodel, V[:n, 13:16])
        # rotation set by the tick: rotX(angle[k]) rotY(spin)
        M = Rx(math.radians(ANGLE[g['k']]) if False else ANGLE[g['k']] * 0.0174532925) @ Ry(float(np.float32(g['spin'])))
        dr = np.abs(M - ob['m']).max()
        worst_rot = max(worst_rot, dr)
        cnt += 1
        if not ok:
            bad += 1
            if bad < 5:
                d = np.abs(model - V[:n, 2:5]).max()
                print('refresh %d k %d w %.4f: morph mismatch max %d' % (frame, g['k'], w, d))
        if n < len(V) and (prev_extra is None or not np.array_equal(prev_extra, V[n:, 2:5])):
            print('refresh %d: vertices %d.. not morphed, hold %s' % (frame, n, V[n:, 2:5].tolist()))
        prev_extra = V[n:, 2:5].copy() if n < len(V) else None
    print('morph: %d frames, %d mismatches; flower matrix max |diff| %.3g' % (cnt, bad, worst_rot))


if __name__ == '__main__':
    main()

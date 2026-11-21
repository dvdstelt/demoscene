"""Check the LWS evaluator (lws.evaluate + the rotation order) against the camera and tunnel objects of the flower
dumps (out/flower_dump.pkl from hooks_scene.py with EXTRA=4b685c, the frame variable).
usage: python3 lws_check.py out/flower_dump.pkl ../../snaps/f4963.snap"""
import os, pickle, struct, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile, engine as E, lws
import unicorn  # noqa: F401
snap = pickle.load(open(sys.argv[2], 'rb'))['mem']
tab = [struct.unpack_from('<I', snap, 0x4b6618 + 4 * k)[0] for k in range(2)]
d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
motions, N = lws.read_scene(d.get('flower/tunnel1.lws'), 3000.0)
D = pickle.load(open(sys.argv[1], 'rb'))
bad = tot = 0
for e in D:
    if e['when'] != 'before':
        continue
    f = struct.unpack('<f', e['extra'][0x4b685c])[0]
    objs = {o['addr']: o['hdr'] for o in e['objs']}
    for m, addr in enumerate(tab):
        if addr not in objs:
            continue
        h = objs[addr]
        pos, ang = lws.evaluate(motions[m], N, f)
        R = [[E.L(1), E.L(0), E.L(0)], [E.L(0), E.L(1), E.L(0)], [E.L(0), E.L(0), E.L(1)]]
        R = E.rot_y(R, float(ang['H']))
        R = E.rot_x(R, -float(ang['P']))
        R = E.rot_z(R, -float(ang['B']))
        opos = [struct.unpack_from('<i', h, 0x10 + 4 * k)[0] for k in range(3)]
        orot = [struct.unpack_from('<d', h, 0x28 + 8 * k)[0] for k in range(9)]
        mrot = [float(R[r][c]) for r in range(3) for c in range(3)]
        tot += 1
        ok = opos == pos and max(abs(a - b) for a, b in zip(orot, mrot)) < 1e-12
        exact = orot == mrot
        if not ok:
            bad += 1
            print('frame %.3f motion %d: pos %s / %s, rot max diff %.3g' % (f, m, opos, pos,
                  max(abs(a - b) for a, b in zip(orot, mrot))))
        elif not exact:
            print('frame %.3f motion %d: positions equal, rotation within %.3g' % (f, m, max(abs(a - b) for a, b in zip(orot, mrot))))
print('%d of %d object states differ' % (bad, tot))

"""Check the credits overlays (0x406794 after the 3D render) on dumped buffers: the water mask (bitmap type 2,
0x416174: dst = wtrmask.int[mask << 8 | dst] over the whole screen, before 9:48) and the name picture (type 4,
0x416324 at x = 0, y = 117: dst = tunnel.int[src << 8 | dst] where src != 0).
usage: python3 blit.py dump.pkl"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile
from dumpread import records, I
d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
raw = lambda n: np.frombuffer(d.get(n), np.uint8)
MASK = raw('credits/wtrmask.raw').reshape(200, 320)
MINT = raw('credits/wtrmask.int').reshape(256, 256)
TINT = raw('credits/tunnel.int').reshape(256, 256)
NAMES = [('kredu1', 173, 75), ('kredu7', 129, 75), ('kredu2', 146, 75), ('kredu8', 154, 72), ('kredu3', 318, 82),
         ('kredu6', 294, 75)]     # [0x4263c0 + 4 i]: destop, wili, jouni, jussi (JULIET), yolk+legend, santtu
PICS = [raw('credits/%s.raw' % n).reshape(h, w) for n, w, h in NAMES]
DRAWN_H = [75, 75, 75, 70, 82, 75]   # heights given to 0x415e50: JULIET's bitmap is 70 rows of the file's 72
PICS = [p[:h] for p, h in zip(PICS, DRAWN_H)]


def overlay(buf3d, event, name):
    out = buf3d.copy()
    if not event:
        out = MINT[MASK.astype(np.int32), out]
    if 0 <= name < 5:
        p = PICS[name]
        h, w = p.shape
        reg = out[117:117 + h, 0:w]
        m = p != 0
        reg[m] = TINT[p[m].astype(np.int32), reg[m]]
    return out


if __name__ == '__main__':
    pend = {}
    ok = bad = 0
    for r in records(sys.argv[1]):
        if r['tag'] == 'buf3d':
            pend[r['n']] = r
        elif r['tag'] == 'buf' and r['n'] in pend:
            a = np.frombuffer(pend[r['n']]['buf'], np.uint8).reshape(200, 320)
            b = np.frombuffer(r['buf'], np.uint8).reshape(200, 320)
            v = r['state']['vars']
            event, name = I(v, 0x110), I(v, 0x118)
            diff = (overlay(a, event, name) != b).sum()
            print('frame %d (refresh %d): event %d name %d: %d pixels differ' % (r['n'], r['refresh'], event, name, diff))
            ok += diff == 0; bad += diff != 0
    print('%d frames exact, %d differ' % (ok, bad))

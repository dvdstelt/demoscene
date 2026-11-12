"""How often the 16-bit back-face test (3377: two 16-bit imuls, the products truncated) disagrees with the exact
cross product, and how many faces have a vertex behind the eye. usage: cull_check.py <part> <pre dump>..."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dumpview import Dump
from engine_model import Engine, PARTS, s16
part = sys.argv[1]
for path in sys.argv[2:]:
    d = Dump(path, part)
    e = Engine(d, part)
    e.build()
    c = PARTS[part]
    wrong = total = 0
    o = c['objs'] + 0x3a
    vbase = 0
    while True:
        r = d.b(0x10, o, 0x3a)
        s = struct.unpack_from('<H', r, 0)[0]
        if s == 0:
            break
        if s != 0xffff and o <= c['objlast'] and struct.unpack_from('<H', r, 2)[0] == 0:
            seg = d.seg_of(s)
            nv, nf, opos, ofac = (d.u16(seg, 2 * i) for i in range(4))
            cull = struct.unpack_from('<h', r, 0x2c)[0]
            for j in range(nf):
                A, B, C = (d.u16(seg, ofac + 8 * j + 2 + 2 * k) + 4 * vbase for k in range(3))
                (xa, ya), (xb, yb), (xc, yc) = e.vscr[A], e.vscr[B], e.vscr[C]
                exact = (xb - xa) * (yc - ya) - (xc - xa) * (yb - ya)
                trunc = s16(s16((xb - xa) * (yc - ya)) - s16((xc - xa) * (yb - ya)))
                total += 1
                if (exact < 0) != (trunc < 0):
                    wrong += 1
            vbase += nv
        o += 0x3a
    print('%s frame %d: %d faces, 16-bit facing test differs from the exact one in %d' % (part, d.frame, total, wrong))

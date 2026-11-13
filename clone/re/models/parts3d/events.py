"""The sync table entries the 3D parts react to, and the frames at which parts 7..10 start / events fire in
full.flips (timer values in t70 units = 1/70 s)."""
import os, sys, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep
from sheet import read_flips
PIT = 1193182
ev = []
for i in range(98):
    pos, e = struct.unpack('<ii', rep.rd(0x420499 + 8 * i, 8))
    ev.append((pos, e))
mine = {7, 8, 9, 10, 701, 801, 802, 803, 901, 902, 1011, 1012}
print('events:', ' '.join('%02x:%02x=%d' % (p >> 6, p & 63, e) for p, e in ev if e in mine))
flips, _ = read_flips(os.path.join(os.path.dirname(__file__), '..', '..', 'full.flips'))
fl = sorted(flips)
# per part: first frame, its t70, previous frame's t70 (time origin), music time of the part's event (row start)
prev = None
for f in fl:
    tick, time, timer, t70, part, order, row = flips[f]
    if prev is not None and part != prev[1][4] and part in (7, 8, 9, 10):
        pos = order * 64 + row
        tick0 = 12 + (order * 64 + row) * 3 if False else None
        print('part %2d: first flip %d tick %d drawn with t70 %.4f; previous frame t70 %.4f (dt %.3f); position %02x:%02x'
              % (part, f, tick, t70, prev[1][3], t70 - prev[1][3], order, row))
    if prev is not None and prev[1][4] in (7, 8, 9, 10) and part != prev[1][4]:
        print('   part %d last flip %d t70 %.4f' % (prev[1][4], prev[0], prev[1][3]))
    prev = (f, flips[f])

"""Check the precalc's object placement (0x16ba1) against the emulator (dump at 0x16a5b, after 0x16ba1 ran)."""
import sys
from common import COS1023
from hooklib import parse, i32, i16

recs = parse(sys.argv[1])
bad = 0
for r in recs:
    f = i32(r['163ae'], 0)
    d = r['16160']
    for i in range(16):
        o = 32 * i
        ang = 16 * (f & 31) + 256 * i
        x = (int(COS1023[ang & 0xfff]) << 8) >> 10
        z = ((int(COS1023[(ang - 0x400) & 0xfff]) << 8) >> 10) - 0xbe
        ay = ((ang - 0x500) & 0xfff) if i else None
        got = (i32(d, o), i32(d, o + 4), i32(d, o + 8), i16(d, o + 0xc), i16(d, o + 0xe) & 0xffff)
        exp = (x, 100, z, 0x400, ay if i else got[4])
        if got != exp:
            bad += 1
            print('frame', f, 'obj', i, 'got', got, 'expected', exp)
    print('frame', f, 'object 0 angle y =', i16(d, 0xe) & 0xffff, 'projection', i32(r['57ab6'], 0), r['5594c'][0], i32(r['55948'], 0)) if f < 3 or f > 61 else None
print(len(recs), 'frames,', bad, 'mismatches')

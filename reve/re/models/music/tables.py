"""Compare the player's jump tables of V3.02 (STARS) and V3.03 (Reve) through the code alignment (vs302.py)."""
import difflib
import struct
import sys

import vs302
import rep

simg = open(sys.argv[1] if len(sys.argv) > 1 else vs302.STARS_IMG, 'rb').read()
cmap = {}
for s0, s1, r0, r1, what in vs302.RANGES:
    S = vs302.listing(simg, 0, s0, s1, 0x10000, 0x80000)
    R = vs302.listing(rep.IMG, rep.BASE, r0, r1, 0x200000, 0x280000)
    sm = difflib.SequenceMatcher(None, [x[2] for x in S], [x[2] for x in R], autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            for k in range(i2 - i1):
                cmap[S[i1 + k][0]] = R[j1 + k][0]
for name, sa, ra, n in (('tick effects', 0x1ac04, 0x26ef34, 16), ('tick E', 0x1aef4, 0x26f224, 16),
                        ('row effects', 0x1b298, 0x26f5cc, 16), ('row E', 0x1b45c, 0x26f790, 16)):
    print(name)
    for i in range(n):
        s = struct.unpack_from('<I', simg, sa + 4 * i)[0]
        r = rep.rd32(ra + 4 * i)
        print('  %x: STARS %x -> %s   Reve %x %s' % (i, s, '%x' % cmap[s] if s in cmap else '?', r,
                                                    '' if cmap.get(s) == r else '<-- DIFFERENT'))

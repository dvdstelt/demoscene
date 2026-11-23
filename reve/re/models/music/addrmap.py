"""Data addresses of V3.02 (STARS) and V3.03 (Reve) that the aligned code uses, with their offset (see vs302.py)."""
import sys
import vs302

simg = open(sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith('--') else vs302.STARS_IMG, 'rb').read()
pairs = vs302.addr_pairs(simg)
for x in sorted(pairs):
    ys = sorted({p[0] for p in pairs[x]})
    jmp = all(p[3].startswith('j') or p[3] == 'call' for p in pairs[x])
    if jmp and '--code' not in sys.argv:
        continue
    print('%6x -> %s' % (x, ', '.join('%x (%+x)' % (y, y - x) for y in ys)),
          '' if len(ys) == 1 else '   <-- several', ' '.join(sorted({p[3] for p in pairs[x]})))

"""xrange.py LO HI: instructions whose 32-bit immediate / displacement lies in [LO, HI) (aligned decode from ctx.py)."""
import sys, struct
sys.path.insert(0, '/home/dennis/src/dvdstelt/demoscene/jizz/re/models/core')
sys.path.insert(0, '/home/dennis/src/dvdstelt/demoscene/jizz/re')
import rep
from ctx import all_insns
from xr import owner
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
ins = all_insns()
for a in sorted(ins):
    size, m, o = ins[a]
    for k in range(a, a + size - 3):
        v = struct.unpack_from('<I', rep.SEG, k)[0]
        if lo <= v < hi:
            print(f'{a:06x} (in {owner(a):06x}): {m} {o}'); break

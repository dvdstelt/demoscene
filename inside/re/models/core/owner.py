"""For each code address that references a global, the function it belongs to (nearest preceding call target or
known routine) — to see which routines read the clocks."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep
targets = set(rep.call_targets())
known = {0x403214, 0x403748, 0x402e30, 0x402f1c, 0x405f30, 0x406794, 0x404738, 0x4047a0, 0x40940c, 0x4095f8,
         0x4021f8, 0x4028e4, 0x407c78, 0x407d78, 0x40a20c, 0x40a264, 0x40407c, 0x404424, 0x4031a0, 0x403164,
         0x4038c8, 0x4038dc}
starts = sorted(targets | known)
import bisect
for g in sys.argv[1:]:
    g = int(g, 16)
    refs = [a for a in rep.xref(g) if a < 0x41e8be]
    own = {}
    for a in refs:
        i = bisect.bisect_right(starts, a) - 1
        own.setdefault(starts[i], []).append(a)
    print('%x:' % g, ', '.join('%x (%d)' % (f, len(v)) for f, v in sorted(own.items())))

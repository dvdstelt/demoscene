"""Frame latency in a hooked run (run_inside.py --events): for every retrace callback that copied a frame, the
clock M = [0x4248c4] at the start of that frame's frame routine and at its flip. Prints the distribution of
(retrace M - start M) in timer callbacks and the frames drawn per refresh."""
import sys
from collections import Counter
ev = [l.split() for l in open(sys.argv[1]) if not l.startswith('#')]
start = None
flipped = None            # (start M, flip M) of the frame waiting to be copied
lat, drawn, shown, dropped = Counter(), 0, 0, 0
for e in ev:
    kind, M = e[0], int(e[4])
    if kind == 'frame':
        start = M
        drawn += 1
    elif kind == 'flip' and start is not None:
        if flipped is not None:
            dropped += 1
        flipped = (start, M)
    elif kind == 'cb' and e[10] == '0' and flipped is not None and e[11] == '0' and e[13] == '0':
        # retrace callback ([0x4b8f10] = 0, not blocked): this callback's M is M + 1 after its increment
        lat[M + 1 - flipped[0]] += 1
        shown += 1
        flipped = None
print('frames drawn %d, shown %d, replaced before shown %d' % (drawn, shown, dropped))
print('latency (retrace callback M - M at frame start), callbacks:', sorted(lat.items()))

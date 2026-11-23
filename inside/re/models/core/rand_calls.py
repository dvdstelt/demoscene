"""rand() / srand() calls in a hooked run (events file): count per calling site, in order of first call, with the
seed before the first call of each run of calls. Also checks that the Watcom LCG reproduces the seeds."""
import bisect
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep
STARTS = sorted(set(rep.call_targets()) | {0x403214, 0x4031a0, 0x4038c8, 0x40940c, 0x4021f8, 0x4098c4})


def owner(a):
    return STARTS[bisect.bisect_right(STARTS, a) - 1]
ev = [l.split() for l in open(sys.argv[1]) if l.startswith(('rand', 'srand'))]
runs = []
seed = 1
for e in ev:
    kind, rec, caller = e[0], int(e[1]), owner(int(e[10], 16))
    val = int(e[11], 16)
    if kind == 'srand':
        runs.append(['srand', rec, caller, 1, val])
        seed = val
        continue
    assert val == seed, (e, hex(seed))
    seed = (seed * 0x41c64e6d + 0x3039) & 0xffffffff
    if runs and runs[-1][0] == 'rand' and runs[-1][2] == caller:
        runs[-1][3] += 1
    else:
        runs.append(['rand', rec, caller, 1, val])
for r in runs:
    print('%-5s record %5d in %x  x%d  seed before %#x' % tuple(r))
print('seed after: %#x' % seed)

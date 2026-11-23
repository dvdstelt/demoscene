"""From a run_temple.py --dump --lite pickle: how long a temple frame takes (T1 - T0 callbacks, T = [0x4248c4]),
the callback at which it is shown (the first retrace callback after its flip: M = 4k - 151 at refresh k) and the
tick count n against T0 (n = floor((T0 + 151) / 4) - 11275, one less from the lost tick at refresh 11967 on).
usage: latency.py out/full_lite.pkl"""
import sys, collections
import dumpread as D, state

d = D.load(sys.argv[1])
L = collections.Counter(); dur = collections.Counter(); off = collections.Counter()
for rec in d:
    T0, T1 = rec['T0'], rec['T1']
    Ms = T1 + 1
    while (Ms + 151) % 4: Ms += 1
    L[Ms - T0] += 1; dur[T1 - T0] += 1
    n, T, e = state.check(rec)
    off[('before 11967' if rec['frame'] < 11967 else 'from 11967'), n - ((T0 + 151) // 4 - 11275)] += 1
print('frames', len(d))
print('frame time T1 - T0 (callbacks):', sorted(dur.items()))
print('shown at M - T0 (callbacks):', sorted(L.items()))
print('n - (floor((T0 + 151) / 4) - 11275):', dict(off))

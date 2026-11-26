"""Music ticks from full.gus: the time of every timer-2 IRQ served ('irq t' lines), gaps and late ticks.

prints: the nominal tick period, every interval that is not one period (late or lost ticks), with the frame
usage: python3 models/core/ticks_gus.py [full.gus]
"""
import sys
import numpy as np
path = sys.argv[1] if len(sys.argv) > 1 else 'full.gus'
T = 808 * 625 / 25.175e6
ts = []
for line in open(path):
    f = line.split()
    if len(f) == 4 and f[1] == 'irq' and 't' in f[2]:
        ts.append((int(f[0]), float(f[3])))
n = np.array([a for a, b in ts]); t = np.array([b for a, b in ts])
print('timer ticks served:', len(ts), 'first', ts[0], 'last', ts[-1])
d = np.diff(t)
med = np.median(d)
print('median interval %.7f s' % med)
P = 62 * 320e-6
print('nominal 62 x 320 us = %.7f' % P)
# phase: t - (t0 + i*P)
ph = t - (t[0] + np.arange(len(t)) * P)
odd = np.nonzero(np.abs(d - P) > 2e-5)[0]
print('intervals differing from 19.84 ms by > 20 us:', len(odd))
for i in odd:
    print('  tick %d -> %d: %.6f -> %.6f  interval %.3f ms  (frame %.2f)  phase after %.3f ms' % (
        n[i], n[i + 1], t[i], t[i + 1], d[i] * 1e3, t[i + 1] / T, ph[i + 1] * 1e3))

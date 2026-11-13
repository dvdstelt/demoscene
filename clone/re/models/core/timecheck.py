"""Check main's time variables in the snapshots against the capture's timer values (full.flips).

t70 [0xe9572e] (double), previous t70 [0xe95736] (double), dt [0x420489] (float), [0xe9573e] (float, += 0.03 dt).
The constant at 0x41a128 is 1/17043 (70/1193010), not 70/1193182.
usage (toolbox, from clone/re): python3 models/core/timecheck.py
"""
import glob
import os
import pickle
import struct
import sys

import unicorn  # noqa: F401

RE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
C = struct.unpack('<d', open(os.path.join(RE, 'unpacked', 'clone_img.bin'), 'rb').read()[0x1a128:0x1a130])[0]
print('constant 0x41a128 = %r, 1/C = %r' % (C, 1 / C))

flips = {}
for line in open(os.path.join(RE, 'full.flips')):
    if line.startswith('#'): continue
    f = line.split()
    flips[int(f[0])] = (int(f[1]), int(f[3]))
ticks = {}
for line in open(os.path.join(RE, 'full.ticks')):
    if line.startswith('#'): continue
    f = line.split()
    ticks[int(f[0])] = f

for path in sorted(glob.glob(os.path.join(RE, 'snaps', 't*.snap')), key=lambda p: int(os.path.basename(p)[1:-5])):
    s = pickle.load(open(path, 'rb'))
    mem = s['mem']
    t70, prev = struct.unpack('<dd', mem[0xe9572e:0xe9573e])
    acc, = struct.unpack('<f', mem[0xe9573e:0xe95742])
    dt, = struct.unpack('<f', mem[0x420489:0x42048d])
    tick = int(os.path.basename(path)[1:-5])
    nflips = s['st'].get('flips', s['st'].get('frames'))
    # the timer read after the last shown frame = the timer of the next frame (flip n+1)
    cand = [(n, tm) for n, (tk, tm) in flips.items() if tk <= tick]
    n, tm = max(cand)
    print(f'{os.path.basename(path):12s} t70 {t70:12.6f} prev {prev:12.6f} dt {dt:9.6f} acc {acc:10.4f}  '
          f'last flip {n} timer {tm} -> timer/17043 {tm * C:12.6f}  timer*70/1193182 {tm * 70 / 1193182:12.6f}')

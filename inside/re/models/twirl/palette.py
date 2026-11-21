"""The DAC during the twirl (full.frames) against the pictures' palettes: per record, how many of the 768 DAC values
equal the palette in force (entry 0 zeroed after an event), and a sample entry, to show the flashes and fades."""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import frame
from sheet import open_frames, indexed
raw, _ = open_frames(os.path.join(frame.RE, 'full.frames'))
pal = {}
for name in ('flower/jeesus.pal', 'nigga/nigga2.pal', 'nigga/nigga4.pal', 'flight/map.pal'):
    pal[name] = np.minimum(np.frombuffer(bytes(frame.dat.get(name)), np.uint8).reshape(256, 3), 63)
j0 = pal['flower/jeesus.pal']
n2 = pal['nigga/nigga2.pal'].copy(); n2[0] = 0
jz = j0.copy(); jz[0] = 0
mp = pal['flight/map.pal'].copy(); mp[0] = 0
E = 100
ranges = [(10572, 10595, j0), (10709, 10740, n2), (10841, 10862, jz), (10884, 10906, jz), (11126, 11146, mp),
          (11270, 11277, mp)]
for a, b, P in ranges:
    for k in range(a, b + 1):
        _, dac = indexed(raw, k)
        eq = int((dac == P).sum())
        print(k, f'equal {eq:3d}/768', 'entry', E, dac[E], 'target', P[E], 'inverse', (63 - P[E]) & 63)
    print()

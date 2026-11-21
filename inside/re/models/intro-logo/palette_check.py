"""The intro's and logo's palettes in the capture: the DAC per refresh against intro.pal / logo.pal x level."""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile
from sheet import open_frames, indexed
D = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
IP = np.frombuffer(D.get('intro/intro.pal'), np.uint8).reshape(256, 3).astype(float)
LP = np.frombuffer(D.get('logo/logo.pal'), np.uint8).reshape(256, 3).astype(float)
raw, n = open_frames(os.path.join(HERE, '..', '..', 'full.frames'))
def dac(k): return indexed(raw, k)[1].astype(float)
print('refresh: fitted level of the DAC against intro.pal (least squares), max |DAC - trunc(level*pal)|')
for k in list(range(30, 45)) + [60, 80, 100, 120, 140, 150, 155, 156, 157, 158, 160, 200, 1100, 1445]:
    d = dac(k)
    lv = (d * IP).sum() / max((IP * IP).sum(), 1)
    print(k, '%.4f' % lv, 'max err vs trunc %.0f' % np.abs(d - np.trunc(IP * lv + 1e-9)).max(), 'equal to intro.pal' if (d == IP).all() else '')
print('logo:')
for k in [1445, 1446, 1447, 1500, 1626, 1627, 1628, 1630, 1640, 1641, 1642, 1643, 1700, 1802, 1803, 1978, 1979, 1990, 1997, 1998, 2147]:
    d = dac(k)
    print(k, 'equal logo.pal' if (d == LP).all() else 'mean %.2f (logo.pal mean %.2f) min %.0f' % (d.mean(), LP.mean(), d.min()))

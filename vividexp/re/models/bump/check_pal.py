"""Which palette is active: the DAC in a snapshot against the light pictures' own palettes and the .pal files.
usage: python3 check_pal.py SNAP"""
import pickle
import sys

import numpy as np
import unicorn  # noqa: F401

import bump

snap = pickle.load(open(sys.argv[1], 'rb'))
dac = np.frombuffer(bytes(snap['st']['dac']), np.uint8).reshape(256, 3)
cands = {}
for n in ['bump/phong.pcx', 'bump/phong2.pcx', 'bump/phong3.pcx', 'bump/bumppic1.pcx', 'bump/bumppic2.pcx',
          'bump/bumppic3.pcx']:
    cands[n] = bump.pcx(n)[1]
for n in ['bump/phong.pal', 'pal/phong.pal', 'pal/phong2.pal', 'pal/white.pal', 'pal/watback.pal']:
    try:
        cands[n] = np.frombuffer(bump.load(n), np.uint8).reshape(256, 3)
    except FileNotFoundError:
        print('missing', n)
for n, p in cands.items():
    print('%-20s equal %s maxdiff %d' % (n, np.array_equal(p, dac), np.abs(p.astype(int) - dac).max()))
print('dac 0..8', dac[:8].tolist())
print('dac 120..136', dac[120:136].tolist())
print('dac 248..256', dac[248:].tolist())

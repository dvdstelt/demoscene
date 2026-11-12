"""How much the integer feedback of 1aab (B[r][c] = min((24c + 232r + 63) >> 8, 112)) differs from float versions,
over the whole part (3715 steps with the real offsets): mean fire value F and mean displayed fire level.
Candidates: exact float  F' = min((24c + 232F) / 256, 112)
            biased float F' = min((24c + 232F + 63) / 256 - k, 112) for a few k
usage (from dope/re): python3 models/p4/fire_float.py
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fire_model as FM  # noqa: E402

rows = [l.split() for l in open(os.path.join(HERE, 'out', 'frames.log'))]
offs = []
prev = 0
for r in rows:
    o = int(r[11])
    if o != prev:
        offs.append(o)
        prev = o
y, x = np.mgrid[0:200, 0:320]
D = FM.DITHER[y & 3, x & 3]


def level(F):
    f = np.repeat(F.reshape(200, 160), 2, axis=1)
    return (2 * f + D) >> 4


Fi = np.zeros(32000, np.uint8)
cands = {'float': 0.0, 'bias0.5': 0.5, 'bias1': 1.0, 'bias1.5': 1.5}
Ff = {k: np.zeros(32000) for k in cands}
stats = {k: [] for k in ['int'] + list(cands)}
for n, off in enumerate(offs):
    Fi = FM.fire_step(Fi, off)
    c = FM.CLOUD[(FM.TMAP[FM.WORD_OF_PIXEL] + off) & 0xffff]
    for k, b in cands.items():
        if k == 'float':
            Ff[k] = np.minimum((24 * c + 232 * Ff[k]) / 256, 112)
        else:
            Ff[k] = np.maximum(np.minimum((24 * c + 232 * Ff[k] + 63) / 256 - b, 112), 0)
    if n % 50 == 49:
        stats['int'].append((Fi.mean(), level(Fi).mean()))
        for k in cands:
            stats[k].append((Ff[k].mean(), (Ff[k] / 8).mean(), np.abs(Ff[k] - Fi).mean()))
print('mean over the part (every 50th step):')
print('  integer: F %.2f, displayed level %.3f' % tuple(np.mean(stats['int'], 0)))
for k in cands:
    a = np.mean(stats[k], 0)
    print('  %-8s F %.2f, F/8 %.3f, mean |F - Fint| %.2f' % ((k,) + tuple(a)))

"""What do nigga1.shd / nigga2.shd (256 x 256) compute?  table[a * 256 + b] for colour indices a, b.

Compares the palette colour of the table's entry with blends of the two inputs' colours (6-bit DAC values) and
reports the mean / max error and how often the table's entry is the nearest palette colour to the blend.
usage: python3 models/sprites/blend.py
"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile
d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))

for k in (1, 2):
    pal = np.frombuffer(d.get(f'nigga/nigga{k}.pal'), np.uint8).reshape(256, 3).astype(float)
    if k != 1:
        pal[0] = 0
    t = np.frombuffer(d.get(f'nigga/nigga{k}.shd'), np.uint8).reshape(256, 256)
    tex = np.frombuffer(d.get(f'nigga/nigga{k}.raw'), np.uint8)
    used = np.unique(tex)
    A = pal[:, None, :]       # row index a (high byte: second texture)
    B = pal[None, :, :]       # column index b (low byte: first texture)
    got = pal[t]
    print(f'nigga{k}.shd: symmetric entries {np.mean(t == t.T):.3f}; diagonal == index {np.mean(np.diag(t) == np.arange(256)):.3f}')
    m = np.zeros((256, 256), bool); m[np.ix_(used, used)] = True
    for name, blend in (('average', (A + B) / 2), ('a', A + 0 * B), ('b', B + 0 * A), ('add', np.minimum(A + B, 63)),
                        ('multiply', A * B / 63), ('max', np.maximum(A, B)), ('min', np.minimum(A, B)),
                        ('screen', 63 - (63 - A) * (63 - B) / 63), ('0.25a+0.75b', 0.25 * A + 0.75 * B),
                        ('0.75a+0.25b', 0.75 * A + 0.25 * B)):
        err = np.abs(got - blend).sum(-1)
        # nearest palette entry to the blend
        dd = ((blend[..., None, :] - pal[None, None, :, :]) ** 2).sum(-1) if name == 'average' else None
        s = f'  {name:12s} mean |err| {err[m].mean():6.2f}  median {np.median(err[m]):5.1f}'
        if dd is not None:
            near = dd.argmin(-1)
            s += f'  table == nearest {np.mean((near == t)[m]):.3f}, nearest-colour err {np.abs(pal[near] - blend).sum(-1)[m].mean():.2f}'
        print(s)

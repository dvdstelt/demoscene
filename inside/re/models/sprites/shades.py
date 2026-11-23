"""What do nigga9.qnt / nigga4.qnt (64 x 256) and shadow9.sdw / shadow4.sdw (16 x 256) compute?

For each level L (row) fit the palette colour of table[L * 256 + c] as a function of pal[c]: a scale factor s
(colour * s) and, for levels above the identity row, a blend towards white. Prints per level: the identity row,
the best scale (least squares over the colours the picture uses) and the mean error of colour * s and of the
blend towards white.
usage: python3 models/sprites/shades.py
"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile
d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))

for k, ext, rows in ((9, 'qnt', 64), (4, 'qnt', 64), (9, 'sdw', 16), (4, 'sdw', 16)):
    pal = np.frombuffer(d.get(f'nigga/nigga{k}.pal'), np.uint8).reshape(256, 3).astype(float)
    pal[0] = 0
    name = f'nigga/nigga{k}.{ext}' if ext == 'qnt' else f'nigga/shadow{k}.{ext}'
    t = np.frombuffer(d.get(name), np.uint8).reshape(rows, 256)
    tex = np.frombuffer(d.get(f'nigga/nigga{k}.raw'), np.uint8)
    used = np.unique(tex)
    ident = [L for L in range(rows) if np.mean(t[L][used] == used) > 0.9]
    print(f'{name}: rows equal to the identity (>90% of the used colours): {ident}')
    lum = pal[used].sum(-1)
    for L in range(rows):
        got = pal[t[L][used]]
        src = pal[used]
        s = (got * src).sum() / max((src * src).sum(), 1e-9)
        e_scale = np.abs(got - src * s).sum(-1).mean()
        # blend towards white: got = src + w (63 - src)
        w = ((got - src) * (63 - src)).sum() / max(((63 - src) ** 2).sum(), 1e-9)
        e_white = np.abs(got - (src + w * (63 - src))).sum(-1).mean()
        if L % (4 if rows == 64 else 1) == 0 or L == rows - 1:
            print(f'  level {L:2d}: scale {s:5.3f} (err {e_scale:5.2f})  towards white {w:6.3f} (err {e_white:5.2f})'
                  f'  mean grey in {src.mean():5.1f} out {got.mean():5.1f}')

print('\nsecond pass: exact models')
for k in (9, 4):
    pal = np.frombuffer(d.get(f'nigga/nigga{k}.pal'), np.uint8).reshape(256, 3).astype(float)
    pal[0] = 0
    q = np.frombuffer(d.get(f'nigga/nigga{k}.qnt'), np.uint8).reshape(64, 256)
    s = np.frombuffer(d.get(f'nigga/shadow{k}.sdw'), np.uint8).reshape(16, 256)
    tex = np.frombuffer(d.get(f'nigga/nigga{k}.raw'), np.uint8)
    used = np.unique(tex)
    # sdw rows that equal a qnt row
    for n in range(16):
        eq = [(L, np.mean(q[L][used] == s[n][used])) for L in range(64)]
        L, f = max(eq, key=lambda x: x[1])
        print(f'  shadow{k}.sdw level {n:2d}: best qnt row {L} ({f:.2f} equal)', end=';')
    print()
    for L in (36, 40, 48, 56, 63):
        src = pal[used]
        got = pal[q[L][used]]
        for name, model in (('min(c*L/32,63)', np.minimum(src * L / 32, 63)),
                            ('c*L/32 then excess spread', None)):
            if model is None:
                continue
            print(f'  nigga{k}.qnt level {L}: {name} err {np.abs(got - model).sum(-1).mean():.2f}')

print('\nthird pass: bright rows of the .qnt (L > 32), colours that stay below 63 vs those that saturate')
for k in (9, 4):
    pal = np.frombuffer(d.get(f'nigga/nigga{k}.pal'), np.uint8).reshape(256, 3).astype(float)
    pal[0] = 0
    q = np.frombuffer(d.get(f'nigga/nigga{k}.qnt'), np.uint8).reshape(64, 256)
    tex = np.frombuffer(d.get(f'nigga/nigga{k}.raw'), np.uint8)
    used = np.unique(tex)
    src = pal[used]
    for L in (34, 40, 48, 56, 63):
        got = pal[q[L][used]]
        lin = src * L / 32
        sat = lin.max(-1) > 63
        e1 = np.abs(got - np.minimum(lin, 63)).sum(-1)
        # hue-preserving: scale so the largest channel is 63 when it would overflow
        hp = np.where(sat[:, None], src * 63 / np.maximum(src.max(-1, keepdims=True), 1), lin)
        e2 = np.abs(got - hp).sum(-1)
        print(f'  nigga{k}.qnt L {L}: unsaturated {(~sat).sum():3d} colours err {e1[~sat].mean():5.2f}; '
              f'saturated {sat.sum():3d}: clamp per channel err {e1[sat].mean() if sat.any() else 0:5.2f}, '
              f'hue-preserving err {e2[sat].mean() if sat.any() else 0:5.2f}')

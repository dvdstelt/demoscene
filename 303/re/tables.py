import os, sys, struct
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '../remake/tools'))
from lzw import read_pak, unlzw
d, ents = read_pak(os.path.join(HERE, '../original/MUSTARD.PAK'))
def get(name):
    for n, off, csz, usz in ents:
        if n.lower() == name: r = unlzw(d, off, usz); r = r[0] if isinstance(r, tuple) else r; return bytes(r)[:usz]


import numpy as np
neu = get('neuron.raw'); pal = np.frombuffer(neu[:768], np.uint8).reshape(256, 3).astype(float) * 4
lum = pal @ [0.3, 0.59, 0.11]
G = np.frombuffer(get('intro.gst'), np.uint8).reshape(256, 256)   # G[new][prev]
F = np.frombuffer(get('intro.fad'), np.uint8).reshape(64, 256)    # F[k][dst]
print('neuron pal lum range', lum.min(), lum.max(), 'pal sorted by index?', np.corrcoef(np.arange(256), lum)[0,1])
a = np.repeat(np.arange(256), 256); b = np.tile(np.arange(256), 256)
y = lum[G.ravel()]; X = np.stack([lum[a], lum[b], np.ones_like(y)], 1)
co, *_ = np.linalg.lstsq(X, y, rcond=None); print('gst lum fit new,prev,const', co, 'rms', np.sqrt(np.mean((X@co - y)**2)))
ymax = np.maximum(lum[a], lum[b]); print('gst vs max rms', np.sqrt(np.mean((ymax - y)**2)))
for k in (0, 1, 2, 4, 8, 16, 32, 63):
    dd = lum[F[k]] - lum; d_ = dd; print('fad k=%d mean dlum %.1f  dlum at dark %.1f at mid %.1f' % (k, dd.mean(), dd[np.argsort(lum)[:20]].mean(), dd[np.argsort(lum)[120:140]].mean()))
print('first palette entries', pal[:4], pal[250:])
from PIL import Image
b = np.frombuffer(get('303b.raw'), np.uint8)
Image.fromarray(b.reshape(38, 256)).resize((1024, 152), Image.NEAREST).save('303b.png')
o = np.frombuffer(get('opening1.raw'), np.uint8)
print('opening1 stats', o.min(), o.max(), np.bincount(o)[:4], 'rows>=200 nonzero', (o.reshape(256,256)[200:] > 0).sum())
Image.fromarray(o.reshape(256, 256)).save('opening1.png')
print('303b max', b.max())

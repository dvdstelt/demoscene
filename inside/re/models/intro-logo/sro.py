"""Read logo/obuke.sro (SurRender object) as the loader 0x410bb0 does: header, vertices, faces."""
import os, sys, struct
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile

def load(name='logo/obuke.sro'):
    d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
    s = d.get(name)
    magic, voff, nv, foff, nf = struct.unpack_from('<5I', s, 0)
    V = np.array([struct.unpack_from('<3h5H', s, voff + 12 * i)[:6] for i in range(nv)], np.int64)
    F = np.array([struct.unpack_from('<4H', s, voff + 12 * nv + 8 * i) for i in range(nf)], np.int64)
    return dict(magic=magic, voff=voff, nv=nv, foff=foff, nf=nf, V=V, F=F, size=len(s))

if __name__ == '__main__':
    o = load()
    print({k: o[k] for k in ('magic', 'voff', 'nv', 'foff', 'nf', 'size')})
    V, F = o['V'], o['F']
    print('xyz min', V[:, :3].min(0), 'max', V[:, :3].max(0))
    r = np.sqrt((V[:, :3].astype(float) ** 2).sum(1))
    print('radius min %.1f max %.1f' % (r.min(), r.max()))
    print('u raw min/max', V[:, 3].min(), V[:, 3].max(), 'v raw', V[:, 4].min(), V[:, 4].max(), 'last word', np.unique(V[:, 5])[:10])
    print('u>>4', np.unique(V[:, 3] >> 4)[:20])
    print('face word0 values', np.unique(F[:, 0], return_counts=True))
    print('face idx max', F[:, 1:].max())
    for i in range(12): print('v', i, V[i], 'r=%.1f' % r[i])
    for i in range(12): print('f', i, F[i])
    # radius histogram
    print(np.histogram(r, bins=10))

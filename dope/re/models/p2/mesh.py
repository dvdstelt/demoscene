"""Parse p2's two meshes (segments 1e71 and 2121) in the engine's format, print summaries.

Header (words): [0] vertex count, [2] face count, [4] vertex list offset, [6] face list offset,
[8] vertex normal list offset, [0a] per-vertex map coordinate list offset (dword per vertex).
"""
import os, struct
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
img = open(os.path.join(HERE, '../../unpacked/p2.bin'), 'rb').read()

def mesh(seg):
    o = seg * 16
    nv, nf, ov, of, on, ot = struct.unpack_from('<6H', img, o)
    V = np.frombuffer(img[o+ov:o+ov+6*nv], '<i2').reshape(nv, 3).astype(int)
    F = np.frombuffer(img[o+of:o+of+8*nf], '<u2').reshape(nf, 4).astype(int)
    N = np.frombuffer(img[o+on:o+on+6*nv], '<i2').reshape(nv, 3).astype(int)
    T = np.frombuffer(img[o+ot:o+ot+4*nv], '<u2').reshape(nv, 2).astype(int)
    return dict(nv=nv, nf=nf, offs=(ov, of, on, ot), V=V, F=F, N=N, T=T, end=o+ot+4*nv)

if __name__ == '__main__':
    for seg in (0x1e71, 0x2121):
        m = mesh(seg)
        print('mesh %04x: %d vertices, %d faces, offsets %s, data ends at file 0x%x' % (seg, m['nv'], m['nf'], [hex(x) for x in m['offs']], m['end']))
        V = m['V']; print('  bbox', V.min(0), V.max(0))
        F = m['F']
        fl, cnt = np.unique(F[:, 0], return_counts=True)
        print('  face flag words', [(hex(a), b) for a, b in zip(fl, cnt)])
        idx = F[:, 1:]
        print('  index range', idx.min(), idx.max(), 'mod 4', np.unique(idx % 4))
        nl = np.sqrt((m['N']**2).sum(1)); print('  normal length', nl.min(), nl.max())
        print('  map coords range u', m['T'][:, 0].min(), m['T'][:, 0].max(), 'v', m['T'][:, 1].min(), m['T'][:, 1].max())
        # clusters along y
        ys = np.unique(V[:, 1]); print('  distinct y', len(ys), ys[:20], ys[-10:])

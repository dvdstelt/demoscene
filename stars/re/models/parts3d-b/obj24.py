"""Object file 24 (the morphing surface of part 0xa0ca): header, vertices, faces and the 10 vertex key sets."""
import struct
import sys

import numpy as np
from PIL import Image

import snap


def load_b(n):
    d = open(f'{snap.RE}/unpacked/files/{n:02d}.bin', 'rb').read()
    assert d[:3] == b'B!\x1a'
    nv, nf = struct.unpack_from('<HH', d, 4)
    v = np.frombuffer(d, np.int16, nv * 8, 8).reshape(nv, 8)
    f = np.frombuffer(d, np.uint16, nf * 8, 8 + nv * 16).reshape(nf, 8)
    o = 8 + nv * 16 + nf * 16
    nk = (len(d) - o) // (nv * 16)
    keys = [np.frombuffer(d, np.int16, nv * 8, o + k * nv * 16).reshape(nv, 8) for k in range(nk)]
    return d, nv, nf, v, f, keys


if __name__ == '__main__':
    d, nv, nf, v, f, keys = load_b(24)
    print('byte3', d[3], 'nv', nv, 'nf', nf, 'keys', len(keys))
    print('vertex words 0..7 of the first 3 vertices:\n', v[:3])
    print('uv bytes (+0xc, +0xd) first row:', [(int(x) & 0xff, int(x) >> 8 & 0xff) for x in v[:23, 6]])
    print('word +0xe values:', np.unique(v[:, 7]))
    print('faces first 4:\n', f[:4])
    print('face word0 unique', np.unique(f[:, 0]), 'word 4..7 unique', [np.unique(f[:, k]) for k in range(4, 8)])
    for k, K in enumerate(keys):
        x, y, z = K[:, 0], K[:, 1], K[:, 2]
        n = np.sqrt((K[:, 3:6].astype(float) ** 2).sum(1))
        same_uv = (K[:, 6] == v[:, 6]).all()
        print(f'key {k}: x {x.min()}..{x.max()} y {y.min()}..{y.max()} z {z.min()}..{z.max()} |n| {n.min():.0f}..{n.max():.0f} uv same {same_uv}')
    # height maps: z of each key on the 23x23 grid
    tiles = []
    for K in keys:
        z = K[:, 2].astype(float).reshape(23, 23)
        im = ((z - z.min()) / max(1, z.max() - z.min()) * 255).astype(np.uint8)
        tiles.append(np.kron(im, np.ones((8, 8), np.uint8)))
    Image.fromarray(np.hstack(tiles)).save('out/obj24_keys_z.png')

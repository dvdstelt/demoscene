"""Which dump/*.tga is in which texture-table slot ([0x18d54] = 0x654a) at a snapshot: python3 texmap.py TICK..
Buffers are planar R, G, B (256x256 each); TGAs are BGR bottom-up or top-down. Prints the closest TGA per slot."""
import sys, os, glob; sys.path.insert(0, '.')
import numpy as np, snap
tgas = {}
for p in sorted(glob.glob(os.path.join(snap.RE, 'dump', '*.tga'))):
    d = open(p, 'rb').read(); img = np.frombuffer(d[18:18 + 256 * 256 * 3], np.uint8).reshape(256, 256, 3)
    tgas[os.path.basename(p)[:-4]] = np.stack([img[:, :, 2], img[:, :, 1], img[:, :, 0]]).astype(int)
for t in map(int, sys.argv[1:]):
    M = snap.Mem(t); print('tick', t)
    for k in range(18):
        a = M.d(0x654a + 4 * k)
        raw = np.frombuffer(bytes(M.m[a:a + 0x30000]), np.uint8)
        planar = raw.reshape(3, 256, 256).astype(int)
        inter = raw.reshape(256, 256, 3).transpose(2, 0, 1).astype(int)
        res = []
        for fmt, buf in (('planar', planar), ('interleaved', inter)):
            for n, im in tgas.items():
                res.append((np.abs(buf - im).mean(), n, fmt))
        best = sorted(res)[:2]
        print(f'  slot {k:2d} {a:#x}', ', '.join(f'{n} ({fmt}) {d:.2f}' for d, n, fmt in best))

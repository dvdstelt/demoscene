import snap, math
import numpy as np
from PIL import Image
img = snap.Mem(snap.IMG)
print('pal1 same', snap.IMG[0x13048:0x13048+96] == snap.load('f18451_133ad.snap')['mem'][0x13048:0x13048+96])
t = np.array([img.s32(0xa740 + 4*i) for i in range(4096 + 1024)])
for A in (65528, 65535, 65536, 65520):
    e = t[:4096] - np.round(A*np.sin(2*np.pi*np.arange(4096)/4096))
    print(A, abs(e).max())
print('cos part = sin shifted', np.array_equal(t[1024:1024+4096], np.concatenate([t[1024:4096], t[4096:4096+1024]])))
print('b740 vs a740+1024', all(img.s32(0xb740+4*i) == img.s32(0xa740 + 4*((i+1024) % 4096)) for i in range(3072)))
print([img.s32(0xb740 + 4*i) for i in range(3070, 3076)], [img.s32(0xa740+4*i) for i in range(0,4)])
f = np.frombuffer(snap.ovl(58), np.uint8).reshape(200, 320)
Image.fromarray((f * 8).astype(np.uint8)).save('/tmp/claude-1000/f58.png' if False else 'f58.png')
rows = np.where(f.any(1))[0]; print('rows used', rows.min(), rows.max())
for r0 in (0, 24, 48, 71, 93, 120):
    blk = f[r0:r0+24]
    cols = np.where(blk.any(0))[0]
    if len(cols): print(r0, 'cols', cols.min(), cols.max(), 'rows', r0 + np.where(blk.any(1))[0].min(), r0 + np.where(blk.any(1))[0].max())

"""p6 exploration: prints what the swirl, env map, bump map and shadow masks contain, the palettes, and the DAC
in a snapshot (export_pictures.py writes the pictures)."""
import numpy as np
from PIL import Image
from snap import img, Snap
b = img('p6')
def seg(s, n): return np.frombuffer(b, np.uint8, n, s * 16)
def pal6(off, n=768):
    p = np.frombuffer(b, np.uint8, n, off)
    return p
sw = seg(0x0c97, 64000).reshape(200, 320)
print('swirl colours', np.unique(sw)[:10], np.unique(sw)[-10:], len(np.unique(sw)))
env = seg(0x0449, 256 * 128).reshape(128, 256)
print('env L colours', np.unique(env[:, :128]), '\nenv R', np.unique(env[:, 128:]))
bump = seg(0x2398, 256 * 128).reshape(128, 256)
print('bump vals', len(np.unique(bump)), np.unique(bump)[:20])
pat = seg(0x368a, 32 * 4096).reshape(32, 64, 64)
print('pattern vals', np.unique(pat))
print('pattern ones per frame', [(int((p & 1).sum())) for p in pat])
p1 = np.frombuffer(b, np.uint8, 768, 0x4190).reshape(256, 3)
p2 = np.frombuffer(b, np.uint8, 768, 0x100 + 0x3565).reshape(256, 3)
p0 = np.frombuffer(b, np.uint8, 48, 0xc660 + 0x10).reshape(16, 3)
print('p0', p0.tolist())
print('p1 max', p1.max(), 'p2 max', p2.max())
for i in range(0, 256, 16):
    print(i, p1[i].tolist(), p2[i].tolist())
def save(a, pal, name, scale=1):
    pal = np.asarray(pal, np.uint16)
    rgb = ((pal * 255 + 31) // 63).astype(np.uint8)[a]
    Image.fromarray(rgb).resize((a.shape[1] * scale, a.shape[0] * scale), Image.NEAREST).save(name)
full = p1.copy(); full[:16] = p0
s = Snap('snaps/f25700.snap')
dac = np.frombuffer(bytes(s.vga['dac']), np.uint8).reshape(256, 3)
print('dac==full?', [(i, dac[i].tolist(), full[i].tolist()) for i in range(256) if (dac[i] != full[i]).any()][:20])
pp = np.concatenate([np.concatenate(list(pat[r * 8:(r + 1) * 8] & 1), 1) for r in range(4)], 0)

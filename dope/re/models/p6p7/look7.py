"""p7 exploration: prints the palette, the horizon / dope / light-map / spot / bump data and the label colours."""
import numpy as np
from PIL import Image
from snap import img, Snap
b = img('p7')
def seg(s, n): return np.frombuffer(b, np.uint8, n, s * 16)
pal = np.zeros((256, 3), np.uint8)
pal[:240] = np.frombuffer(b, np.uint8, 720, 0x5a84 * 16).reshape(240, 3)
pal[240:] = np.frombuffer(b, np.uint8, 48, 0x3beb * 16 + 0x2da).reshape(16, 3)
print('pal max', pal.max())
for i in range(0, 256, 8): print(i, pal[i].tolist(), end='; ')
print()
hz = seg(0x5ab4, 64320).reshape(201, 320)
rows = [r for r in range(201) if hz[r].any()]
print('horizon nonzero rows', rows[:3], rows[-3:], 'colours', np.unique(hz))
dope = seg(0x6a86, 20480).reshape(64, 320)
print('dope colours', np.unique(dope, return_counts=True))
lm = seg(0x03b3, 65536).reshape(256, 256)
print('lightmap base: min', lm.min(), 'max', lm.max(), 'colours', len(np.unique(lm)))
sp = seg(0x37ea, 128 * 128).reshape(128, 128)
print('spot min', sp.min(), 'max', sp.max(), 'centre', np.unravel_index(sp.argmax(), sp.shape))
bump = seg(0x2fe9, 256 * 128).reshape(128, 256)
print('bump L uniq', len(np.unique(bump[:, :128])), 'R uniq', len(np.unique(bump[:, 128:])), np.unique(bump[:, :128])[:5])
def save(a, p, name, scale=1):
    p = np.asarray(p, np.uint16)
    rgb = ((p * 255 + 31) // 63).astype(np.uint8)[a]
    Image.fromarray(rgb).resize((a.shape[1] * scale, a.shape[0] * scale), Image.NEAREST).save(name)
grey = np.stack([np.arange(256) // 4] * 3, 1)
for s in (0x3beb, 0x3ce6):
    print(hex(s), b[s * 16:s * 16 + 10])
    lab = seg(s, 0x30a + 70 * 44)[0x30a:].reshape(44, 70)
    print(' label colours', np.unique(lab))

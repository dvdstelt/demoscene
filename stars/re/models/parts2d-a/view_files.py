"""Write the intro's files 5, 6, 7 as PNG (with their palettes) for inspection."""
import numpy as np
from common import *

f5, f6, f7 = ofile(5), ofile(6), ofile(7)
pal = np.zeros((256, 3), np.uint8)
pal[1:16] = np.frombuffer(f5[:45], np.uint8).reshape(15, 3)
pal[0x60:0x70] = np.frombuffer(f6[:48], np.uint8).reshape(16, 3)
pal[0x10:0x50] = np.frombuffer(f7[:0xc0], np.uint8).reshape(64, 3)
print('f5 pal', pal[1:16].tolist())
print('f6 pal', pal[0x60:0x70].tolist())
print('f7 pal', pal[0x10:0x50].tolist())
for i in range(4):
    a = np.frombuffer(f5[0x2d + i * 0x1440:0x2d + (i + 1) * 0x1440], np.uint8).reshape(36, 144)
    print('f5', i, np.unique(a))
    save('f5_%d.png' % i, rgb(a, pal), 3)
for i in range(4):
    a = np.frombuffer(f6[0x30 + i * 0x2500:0x30 + (i + 1) * 0x2500], np.uint8).reshape(74, 128)
    print('f6', i, np.unique(a))
    save('f6_%d.png' % i, rgb(a, pal), 3)
t = np.frombuffer(f7[0xc0:0xc0 + 0x10000], np.uint8).reshape(256, 256)
print('f7 tex values', np.unique(t), 'extra', f7[0x100c0:].hex())
save('f7_tex.png', rgb(t, pal), 2)
save('f7_tex_idx.png', ((t.astype(int) - 16) * 4).clip(0, 255))

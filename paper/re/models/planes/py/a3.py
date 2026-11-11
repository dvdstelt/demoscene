import numpy as np
from snap import M
m7 = M(7000); m9 = M(9500); m87 = M(8700); m61 = M(6100)
base = np.frombuffer(m7.b(0x2b0000, 65536), np.uint8)
t7 = np.frombuffer(m7.b(0x2c0000, 65536), np.uint8)
t9 = np.frombuffer(m9.b(0x2c0000, 65536), np.uint8)
t87 = np.frombuffer(m87.b(0x2c0000, 65536), np.uint8)
print('teal tex == flock tex:', (t7 == t9).all(), 'same at 8700:', (t7 == t87).all())
r = t7.astype(int) - 0x8b - ((base >> 5) & 7)
print('residual values', np.unique(r, return_counts=True))
print('teal tex range', t7.min(), t7.max(), np.bincount(t7)[0x8b:0x96])
print('base tex hist of top3 bits', np.bincount(base >> 5, minlength=8))
# base texture same at 6100 and 9500?
print('base same 6100/9500', (np.frombuffer(m61.b(0x2b0000, 65536), np.uint8) == np.frombuffer(m9.b(0x2b0000, 65536), np.uint8)).all())
sh = np.frombuffer(m7.b(0x2d0000, 65 * 256), np.uint8).reshape(65, 256).astype(int)
def f(k, c):
    kk = k if k <= 32 else ((k - 32) >> 1) + ((k - 32) >> 3)
    if c < 64: return max(0, c + 2 * kk - 64)
    return (c & 0xe0) + max(0, (c & 31) + kk - 32)
mod = np.array([[f(k, c) for c in range(256)] for k in range(65)])
print('shade formula mismatches', (mod != sh).sum())
print('shade same at 9500', (np.frombuffer(m9.b(0x2d0000, 65 * 256), np.uint8).reshape(65, 256) == sh).all())
print('palette at 7000 (entries 128..160)', [tuple(m7.s['pal'][3*i:3*i+3]) for i in range(128, 160, 4)])
print('palette at 9500 (entries 128..160)', [tuple(m9.s['pal'][3*i:3*i+3]) for i in range(128, 160, 4)])
print('rng state 9500', m9.b(0xaa0, 2).hex())
# sine table
st = np.frombuffer(m7.b(0x22479, 4096), np.int32)
import math
ref = np.array([round(65536 * math.sin(2 * math.pi * i / 1024)) for i in range(1024)])
print('sine max diff vs round(65536 sin)', np.abs(st - ref).max(), st[256], st[0], st[768])
# stroke and jitter tables
s1 = np.frombuffer(m7.b(0x2124c0, 1024), np.uint8); s2 = np.frombuffer(m7.b(0x2128c0, 1024), np.int8)
print('stroke hist', np.bincount(s1)[24:34], 'jitter range', s2.min(), s2.max(), np.bincount(s2.astype(int) + 10))

"""Checks the formula of the three shading tables 0x1a479 / 0x1b079 / 0x1bc79 (built by 0x1a336, core scope) that
the filler indexes with env + texel, and regenerates the env map 0x220000 (built by 0x6592) from its formula.
usage: python3 check_tables.py DUMPDIR N"""
import os, sys, pickle, struct, math
import numpy as np
d, n = sys.argv[1], int(sys.argv[2])
pre = pickle.load(open(os.path.join(d, 'r%04d.pre' % n), 'rb'))
low = pre['low']
u32 = lambda a: struct.unpack_from('<I', low, a)[0]
k = u32(0x1a225); off = struct.unpack_from('<i', low, 0x1a474)[0] >> 8; f = low[0x1a478]
size = [u32(0x18d70), u32(0x18d74), u32(0x18d78)]; pos = [u32(0x18d7c), u32(0x18d80), u32(0x18d84)]
def count(k):
  bad = 0
  for ch, base in enumerate((0x1a479, 0x1b079, 0x1bc79)):
    for i in range(768):
        c = min(max((i * k + off * 256) >> 8, 0), 255)
        o = (c + (255 - c) * f + (255 - f) * c) >> 8
        want = ((o << size[ch]) >> 8) << pos[ch]
        bad += want != u32(base + 4 * i)
  return bad
# the parts set the gain [0x1a225] only around their call of 0x1f5c5 (0x17003: 0x1c8, 0x10fda: 0x132)
k = min((0x100, 0x132, 0x1c8), key=count); bad = count(k)
print('gain (when the tables were built) = %#x, offset = %d, flash [0x1a478] = %d, fields size %s pos %s: %d table entries differ from the formula'
      % (k, off, f, size, pos, bad))
# env map (core precalc 0x6592), float32 like the x87 at 24-bit precision
f32 = np.float32
env = np.frombuffer(pre['tex'][:0x10000], np.uint8).reshape(256, 256)
mine = np.zeros((256, 256), np.uint8)
y = f32(-128)
for r in range(256):
    x = f32(-128)
    yy = f32(y * y)
    for c in range(256):
        x = f32(x + f32(1))
        q = f32(f32(255) - f32(math.sqrt(f32(f32(x * x) + yy))))
        q = f32(f32(q * q) * f32(1 / 256))
        q = f32(f32(q * q) * f32(1 / 256))
        mine[r, c] = int(np.rint(q)) & 255
    y = f32(y + f32(1))
print('env map 0x220000: %d of 65536 bytes differ from the formula' % int((mine != env).sum()))

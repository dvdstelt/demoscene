"""Dump the sheet part's variables and tables from the snapshot taken where main calls 0x5334f, and compare with
the file (korso2.bin): which are static, which were changed before the part starts."""
import os, pickle, sys, struct
import numpy as np
import unicorn  # noqa: needed to unpickle
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
snap = pickle.load(open(os.path.join(RE, 'snaps', 'part_5334f.snap'), 'rb'))
mem = snap['mem']
binf = open(os.path.join(RE, 'unpacked', 'korso2.bin'), 'rb').read()
C32 = 0x8fc0
def m32(a): return struct.unpack('<i', mem[C32 + a:C32 + a + 4])[0]
def f32(a): return struct.unpack('<i', binf[0xec0 + a:0xec0 + a + 4])[0]
for name, a in [('pal fade 4db54', 0x4db54), ('amp 4db58', 0x4db58), ('seed 4db5c', 0x4db5c), ('phase 4db60', 0x4db60),
                ('4dbec', 0x4dbec), ('a 4dbf4', 0x4dbf4), ('4dbf8', 0x4dbf8), ('4dbfc', 0x4dbfc), ('4dc04', 0x4dc04),
                ('b 4de6c', 0x4de6c), ('camz 4de14', 0x4de14), ('ydrop 4d84c', 0x4d84c), ('yvel 4d850', 0x4d850),
                ('cb 4d710', 0x4d710), ('4de40', 0x4de40), ('4de44', 0x4de44), ('4de68', 0x4de68), ('buf 4dec1', 0x4dec1),
                ('txtbuf 4cac4', 0x4cac4), ('cnt 5356d', 0x5356d)]:
    print('%-14s snap %10d %08x   file %10d %08x' % (name, m32(a), m32(a) & 0xffffffff, f32(a), f32(a) & 0xffffffff))
print('byte 5356d', mem[C32 + 0x5356d], binf[0xec0 + 0x5356d])
# grid
g = np.frombuffer(bytes(mem[C32 + 0x4decd:C32 + 0x4decd + 256 * 12]), '<i4').reshape(256, 3)
gf = np.frombuffer(binf[0xec0 + 0x4decd:0xec0 + 0x4decd + 256 * 12], '<i4').reshape(256, 3)
print('grid snap first', g[:20].tolist())
print('grid file==snap', (g == gf).all())
e = np.frombuffer(bytes(mem[C32 + 0x4eacd:C32 + 0x4eacd + 480 * 8]), '<i4').reshape(480, 2)
ef = np.frombuffer(binf[0xec0 + 0x4eacd:0xec0 + 0x4eacd + 480 * 8], '<i4').reshape(480, 2)
print('edges first', e[:20].tolist(), e[230:250].tolist(), 'file==snap', (e == ef).all())
print('edges last', e[-5:].tolist())
rt = np.frombuffer(bytes(mem[C32 + 0x52ccd:C32 + 0x52ccd + 200 * 4]), '<i4')
rtf = np.frombuffer(binf[0xec0 + 0x52ccd:0xec0 + 0x52ccd + 200 * 4], '<i4')
print('rowtab', rt[:5], rt[-3:], 'file==snap', (rt == rtf).all())
p1 = np.frombuffer(bytes(mem[C32 + 0x526cd:C32 + 0x526cd + 768]), np.uint8).reshape(256, 3)
p1f = np.frombuffer(binf[0xec0 + 0x526cd:0xec0 + 0x526cd + 768], np.uint8).reshape(256, 3)
p2 = np.frombuffer(bytes(mem[C32 + 0x529cd:C32 + 0x529cd + 768]), np.uint8).reshape(256, 3)
p2f = np.frombuffer(binf[0xec0 + 0x529cd:0xec0 + 0x529cd + 768], np.uint8).reshape(256, 3)
print('pal1 file==snap', (p1 == p1f).all(), 'pal2 file==snap', (p2 == p2f).all())
np.set_printoptions(linewidth=200)
print('pal1', p1.tolist())
print('pal2', p2.tolist())
s = np.frombuffer(bytes(mem[C32 + 0x24a3c:C32 + 0x24a3c + 5200 * 4]), '<i4')
sf = np.frombuffer(binf[0xec0 + 0x24a3c:0xec0 + 0x24a3c + 5200 * 4], '<i4')
print('sine file==snap', (s == sf).all(), s[:4], s[1020:1030], s[4090:4100], s[5115:5125], 'max', s[:4096].max(), s[:4096].min())

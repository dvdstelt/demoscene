import snap, math
import numpy as np
img = snap.Mem(snap.IMG)
s = snap.load('f18997_1528c.snap'); m = snap.Mem(s['mem'])
t_img = np.array([img.s16(0x14260 + 2*i) for i in range(2048)])
t_snp = np.array([m.s16(0x14260 + 2*i) for i in range(2048)])
print('table same', np.array_equal(t_img, t_snp), t_img[:8], t_img.max(), t_img.min(), t_img[512])
for A in (t_img.max(), 2047, 2048, 4095, 4096, 16384, 32767):
    e = t_img - np.round(A*np.sin(2*np.pi*np.arange(2048)/2048))
    print(A, abs(e).max())
print('15278 img', img.u32(0x15278), 'snap', m.u32(0x15278))
print('1527c', m.s32(0x1527c), m.s32(0x15280))
print('79fac', m.u8(0x79fac), img.u8(0x79fac))
for n in (36, 37, 38, 39, 40, 41):
    d = snap.ovl(n); print(n, len(d), d[:24].hex())

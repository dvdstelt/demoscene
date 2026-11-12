import snap, math, struct
import numpy as np
img = snap.Mem(snap.IMG)
s = snap.load('f23436_1089c.snap'); m = snap.Mem(s['mem'])
s0 = snap.load('f23433_10b26.snap'); m0 = snap.Mem(s0['mem'])
t = np.array([img.s32(0xffb8 + 4*i) for i in range(512)])
print('ffb8 same in snap', snap.IMG[0xffb8:0xffb8+2048] == s['mem'][0xffb8:0xffb8+2048], t[:6], t.max(), t.min(), t[128])
for A in (t.max(), 65536, 65535):
    print(A, abs(t - np.round(A*np.sin(2*np.pi*np.arange(512)/512))).max())
names = ['?f740', 'spd1', 'step1', 'spd2', 'step2', 'spd3', 'step3', 'spd4', 'step4', 'z', 'tab', 'face', 'ph1', 'ph2', 'ph3', 'ph4']
for base in (0xf740, 0x107dc, 0x1081c, 0x1085c):
    print(hex(base), 'img ', [img.s32(base + 4*i) for i in range(16)])
print('f740 snap23433', [m0.s32(0xf740 + 4*i) for i in range(16)])
print('f740 snap23436', [m.s32(0xf740 + 4*i) for i in range(16)])
for a in (0x107b8, 0x107bc, 0x107be, 0x107c2, 0x107c6, 0x107ca, 0x107ce, 0x10a7e, 0x10a82, 0x11a8d, 0x11a91, 0x11a95, 0x11a99, 0x11a9d, 0x11aa1, 0xffa0, 0xffa4, 0xffa8, 0xffb0, 0xffb4):
    print(hex(a), 'img', hex(img.u32(a)), 'snap', hex(m.u32(a)))
print('10a7e words img', img.u16(0x10a7e), img.u16(0x10a80))
print('patch table faa0', [hex(m.u32(0xfaa0 + 4*i)) for i in range(10)], hex(m.u32(0xfaa0 + 4*319)))
f34 = snap.ovl(34); f35 = snap.ovl(35)
print('f34', len(f34), f34[:64].hex())
print('f35 pal', list(f35[:48]))

import sys
from snap import Snap
s = Snap(sys.argv[1])
print('vga keys', list(s.vga.keys())[:40])
for i in range(12):
    b = 0x1620 + i * 0x3a
    w = [s.u16(0x10, b + k) for k in range(0, 0x3a, 2)]
    if w[0] == 0:
        print('end at', hex(b)); break
    print('%04x seg=%04x flag=%04x w4=%d pos=(%d,%d,%d) vel=(%d,%d,%d) ang=(%04x,%04x,%04x) avel=(%d,%d,%d) 2a=%04x 2c=%04x 2e=(%d,%d,%d)' % (
        b, w[0], w[1], s.s16(0x10, b + 4), s.cs32(b + 6), s.cs32(b + 0xa), s.cs32(b + 0xe), s.cs32(b + 0x12), s.cs32(b + 0x16), s.cs32(b + 0x1a),
        w[15], w[16], w[17], s.s16(0x10, b + 0x24), s.s16(0x10, b + 0x26), s.s16(0x10, b + 0x28), w[21], w[22], s.cs32(b + 0x2e), s.cs32(b + 0x32), s.cs32(b + 0x36)))
for a in (0x1758, 0x1760, 0x1762, 0x1768, 0x176a, 0x176c, 0x176e, 0x1770, 0x177a, 0x3259, 0x325b, 0x1736, 0x173a, 0x1742, 0x1704, 0x1706):
    print(hex(a), hex(s.cs16(a)))

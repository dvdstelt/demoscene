from common import *
s = snap('2e779')
names = {0x2c9ac: 'palw0', 0x2c9b0: 'palw1', 0x2c9b4: 'seed', 0x2c9b8: 'flag', 0x2c9bc: 'angB', 0x2c9c0: 'xoff2', 0x2c9c4: 'tubeX',
 0x2ca90: 'zdist', 0x2ca94: 'xoff', 0x2ca98: 'angD', 0x2caa8: 'ph1', 0x2caac: 'ph2', 0x2cab0: 'ph3', 0x2cab4: 'angC', 0x2cab8: 'angC0',
 0x2cabc: 'angA', 0x2cac0: 'bg1', 0x2cac4: 'bg2', 0x2cac8: 'bgd1', 0x2cacc: 'bgd2', 0x2eb94: 'fakeclk(byte)'}
for a, n in names.items():
    print('%-6s [%x] file %d snap %d' % (n, a, fi32(a), mi32(s, a)))
print('byte 2e376', IMG[0xec0 + 0x2e376])
e = np.frombuffer(frd(0x2d0e4, 768), np.uint8).reshape(96, 8)
print('edge bytes other than 0,4 nonzero:', np.count_nonzero(e[:, [1, 2, 3, 5, 6, 7]]))
print('edges', [(int(a), int(b)) for a, b in e[:, [0, 4]]])
P0 = np.frombuffer(frd(0x2d9e4, 768), np.uint8).reshape(256, 3)
P1 = np.frombuffer(frd(0x2dce4, 768), np.uint8).reshape(256, 3)
for i in range(0, 256, 8):
    print(i, P0[i:i+8].tolist(), '|', P1[i:i+8].tolist())

"""Compare the model's init buffers with a snapshot taken at RunBUMPEFEK's start.
usage: python3 check_init.py ../../snaps2/f2724.snap bump/bump1.ini 1"""
import pickle
import sys

import numpy as np
import unicorn  # noqa: F401  (the snapshot pickles unicorn contexts)

import bump

snap = pickle.load(open(sys.argv[1], 'rb'))
mem = snap['mem']
fx = bump.Bump(sys.argv[2], int(sys.argv[3]))


def rd32(a): return int.from_bytes(mem[a:a + 4], 'little')


def buf(a, n): return np.frombuffer(mem[rd32(a):rd32(a) + n], np.uint8)


for name, a in [('c0', 0x82d4c0), ('c4', 0x82d4c4), ('c8', 0x82d4c8), ('cc', 0x82d4cc), ('d0', 0x82d4d0),
                ('d4', 0x82d4d4), ('d8', 0x82d4d8)]:
    print(name, hex(rd32(a)))
print('type', rd32(0x82d46c), 'add', mem[0x82d4a4])
import struct
print('floats 470..4bc', [round(struct.unpack('<f', mem[a:a+4])[0], 4) for a in range(0x82d470, 0x82d4c0, 4)])
print('ints 478..48c', [rd32(a) for a in range(0x82d478, 0x82d490, 4)], 'words', mem[0x82d490:0x82d494].hex())
h = buf(0x82d4c8, 64000).reshape(200, 320)
print('height ok', np.array_equal(h, fx.height))
ph = buf(0x82d4d8, 65536).reshape(256, 256)
print('light ok', np.array_equal(ph, fx.light))
t = buf(0x82d4d0, 128000).reshape(200, 320, 2)
print('tab ok', np.array_equal(t[:, :, 0], np.broadcast_to(fx.tab_x[None, :], (200, 320))),
      np.array_equal(t[:, :, 1], np.broadcast_to(fx.tab_y[:, None], (200, 320))))
if fx.type != 1:
    w = np.frombuffer(mem[rd32(0x82d4cc):rd32(0x82d4cc) + 256000], '<u2').reshape(200, 320, 2)
    print('wave0 ok', np.array_equal(w[:, :, 0], fx.wave0), 'wave1 ok', np.array_equal(w[:, :, 1], fx.wave1))
else:
    n = buf(0x82d4d4, 128000).reshape(200, 320, 2)
    u, v = fx.normals
    print('normals ok', np.array_equal(n[2:198, 2:318, 0], u[2:198, 2:318]),
          np.array_equal(n[2:198, 2:318, 1], v[2:198, 2:318]))
    c4 = buf(0x82d4c4, 128000).reshape(200, 320, 2)
    print('c4 = (h, 0)', np.array_equal(c4[:, :, 0], fx.height), (c4[:, :, 1] == 0).all())
dac = np.frombuffer(bytes(snap['st']['dac']), np.uint8).reshape(256, 3)
pal = np.array([[struct.unpack('<i', mem[0x826abc + 12 * i + 4 * c:0x826abc + 12 * i + 4 * c + 4])[0] / 65536
                 for c in range(3)] for i in range(256)])
print('dac[0..4]', dac[:4].tolist(), 'pal16.16[0..2]', pal[:2].tolist())

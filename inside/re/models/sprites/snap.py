"""Snapshot reader for the sprite part: the part's pointers, objects, materials, the light, and the counters.

usage: python3 models/sprites/snap.py snaps/f4255.snap
"""
import os, pickle, struct, sys
import unicorn  # noqa: F401  (needed to unpickle)

HERE = os.path.dirname(os.path.abspath(__file__))


class Snap:
    def __init__(self, path):
        s = pickle.load(open(path, 'rb'))
        self.mem = s['mem']
        self.s = s

    def d32(self, a): return struct.unpack_from('<I', self.mem, a)[0]
    def s32(self, a): return struct.unpack_from('<i', self.mem, a)[0]
    def s16(self, a): return struct.unpack_from('<h', self.mem, a)[0]
    def f32(self, a): return struct.unpack_from('<f', self.mem, a)[0]
    def f64(self, a): return struct.unpack_from('<d', self.mem, a)[0]
    def rd(self, a, n): return self.mem[a:a + n]


NAMES = {0x425fc4: 'scene', 0x425fc0: 'camera', 0x425fd4: 'material', 0x425fdc: 'material2',
         0x425fbc: 'shadowmap', 0x425fd0: 'light', 0x425fb4: 'grid-sro', 0x425fb8: 'shadowsquare-sro',
         0x425fd8: 'warpgrid', 0x425fc8: 'shadepoly', 0x425fcc: 'kuubio', 0x425fb0: 'poksi.sro'}

if __name__ == '__main__':
    sn = Snap(sys.argv[1])
    print('interrupts [0x4248c4] = %d, frame clock [0x424880] = %.1f, part ticks [0x4248c8] = %d' % (
        sn.d32(0x4248c4), sn.f32(0x424880), sn.d32(0x4248c8)))
    print('picture [0x4227fc] = %d, camera step [0x422800] = %d' % (sn.d32(0x4227fc), sn.d32(0x422800)))
    print('warp phases', [round(sn.f32(0x422810 + 20 * k), 4) for k in range(3)])
    for a, n in NAMES.items():
        print('%-18s [%x] = %x' % (n, a, sn.d32(a)))
    for a in range(0x425f20, 0x425fb0, 4):
        print('  [%x] = %x' % (a, sn.d32(a)), end='')
    print()
    for n in ('material', 'material2', 'shadowmap'):
        p = sn.d32([k for k, v in NAMES.items() if v == n][0])
        print(n, ' '.join('%x' % sn.d32(p + 4 * i) for i in range(16)), sn.rd(p + 0x40, 4).hex())
    for n in ('camera', 'warpgrid', 'shadepoly', 'kuubio', 'light'):
        p = sn.d32([k for k, v in NAMES.items() if v == n][0])
        print(n, 'pos', [sn.s32(p + o) for o in (0x10, 0x14, 0x18)], 'flags c4=%x c8=%d cc=%d d0=%x d4=%x d8=%x dc=%d' % (
            sn.d32(p + 0xc4), sn.s32(p + 0xc8), sn.s32(p + 0xcc), sn.d32(p + 0xd0), sn.d32(p + 0xd4), sn.d32(p + 0xd8),
            sn.s32(p + 0xdc)))
        print('   head', ' '.join('%x' % sn.d32(p + 4 * i) for i in range(10)))
        print('   matrix', [round(sn.f64(p + 0x28 + 8 * i), 5) for i in range(9)])
        print('   +70..c4', ' '.join('%x' % sn.d32(p + i) for i in range(0x70, 0xc4, 4)))
    sc = sn.d32(0x425fc4)
    print('scene', ' '.join('%x' % sn.d32(sc + 4 * i) for i in range(20)))

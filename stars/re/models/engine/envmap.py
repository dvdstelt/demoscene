"""0x7873a (with 0x7871c): the engine's 256x256 environment ("phong") map, regenerated and compared with the map
in a snapshot. Also checks the static cosine table 0x5599c = trunc(1023 cos(2 pi i / 4096)).

usage: python3 envmap.py   (inside the toolbox: the snapshots need unicorn to unpickle)
"""
import math
import os
import pickle
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')


def isqrt_table():
    """0x7871c: 65536 bytes, value v repeated 2v+1 times = floor(sqrt(n))."""
    t = bytearray()
    for v in range(256):
        t += bytes([v]) * (2 * v + 1)
    return t


def envmap(colours, dl, dither=False):
    """0x7873a: colours = the part's 256-byte table (only entries 0..127 are read), dl = the part's mode."""
    sq = isqrt_table()
    out = bytearray(0x10000)
    acc = 0                                      # [0x787fb], only with [0x582ce] != 0
    cx = 0x8000
    while True:
        cl, ch = cx & 0xff, cx >> 8
        x, y = cl >> 1, ch
        r = min(sq[x * x + y * y], 0x7f)
        al = colours[0x7f - r]
        if dl == 0:
            ah = ((al & 0x11) << 6) & 0xff
            al >>= 2
            if dither:
                acc += ah
                if acc > 0xff:
                    acc &= 0xff
                    al += 1
            al = (al << 4) & 0xff
        elif dl == 2:
            al >>= 1
        elif dl == 3:
            al = ((al >> 3) << 5) & 0xff
        elif dl == 4:
            al = ((al >> 1) << 3) & 0xff
        bl, bh = (0x80 + x) & 0xff, (0x80 + y) & 0xff
        for u, v in ((bl, bh), (bl ^ 0xff, bh), (bl ^ 0xff, bh ^ 0xff), (bl, bh ^ 0xff)):
            out[v << 8 | u] = al
        cx -= 2
        if cx < 0:
            break
    return out


def check(snap, table, dl, post=None):
    s = pickle.load(open(os.path.join(RE, 'snaps', snap), 'rb'))
    mem = s['mem']
    base = struct.unpack_from('<I', mem, 0x55914)[0]
    have = mem[base:base + 0x10000]
    want = envmap(mem[table:table + 256], dl)
    if post:
        want = bytearray(post(b) for b in want)
    diff = sum(1 for a, b in zip(have, want) if a != b)
    print('%s: env map at %#x vs 0x7873a(dl=%d, table %#x): %d of 65536 bytes differ' % (snap, base, dl, table, diff))


if __name__ == '__main__':
    img = open(os.path.join(RE, 'unpacked', 'stars_img.bin'), 'rb').read()
    cos = [struct.unpack_from('<h', img, 0x5599c + 2 * i)[0] for i in range(4096)]
    bad = sum(1 for i in range(4096) if cos[i] != int(1023 * math.cos(2 * math.pi * i / 4096)))
    print('cos table 0x5599c vs trunc(1023 cos): %d of 4096 differ' % bad)
    import unicorn  # noqa: F401  (the snapshots pickle unicorn contexts)
    check('f7170_6534.snap', 0x6310, 0, lambda b: b or 8)     # spike ball (0x66d7, then 0 -> 8 at 0x671c)
    check('f13810_a0ca.snap', 0x9df0, 4)                      # textured surfaces (0xa302)
    check('f19504_16f7c.snap', 0x16c20, 2, lambda b: (b + 0x20) & 0xff)   # torso (0x16f15, then +0x20 per byte)

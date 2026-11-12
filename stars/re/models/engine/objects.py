"""Parse BARTi's object files (B! = files 18, 24, 45, 50, 51; WLDD = file 28) from unpacked/files/NN.bin and
print statistics: counts, coordinate ranges, normal lengths, face fields, animation frames.

usage: python3 objects.py [N ...]
"""
import os
import struct
import sys
import math

HERE = os.path.dirname(os.path.abspath(__file__))
FILES = os.path.join(HERE, '..', '..', 'unpacked', 'files')


def mesh(b, off):
    """Mesh at off: u16 nv, u16 nf, nv vertices of 16 bytes, nf faces of 16 bytes."""
    nv, nf = struct.unpack_from('<HH', b, off)
    vs = [struct.unpack_from('<hhhhhhBBH', b, off + 4 + 16 * i) for i in range(nv)]
    fo = off + 4 + 16 * nv
    fs = [struct.unpack_from('<HHHHhhhH', b, fo + 16 * i) for i in range(nf)]
    return nv, nf, vs, fs, fo + 16 * nf


def stats(name, nv, nf, vs, fs):
    xs = [v[0] for v in vs]; ys = [v[1] for v in vs]; zs = [v[2] for v in vs]
    nl = [math.sqrt(v[3] ** 2 + v[4] ** 2 + v[5] ** 2) for v in vs]
    fl = [math.sqrt(f[4] ** 2 + f[5] ** 2 + f[6] ** 2) for f in fs]
    print(f'{name}: {nv} vertices, {nf} faces; x {min(xs)}..{max(xs)} y {min(ys)}..{max(ys)} z {min(zs)}..{max(zs)}')
    print(f'   vertex normal length {min(nl):.1f}..{max(nl):.1f}; face normal length '
          f'{min(fl) if fl else 0:.1f}..{max(fl) if fl else 0:.1f}')
    print(f'   vertex +0xc u {min(v[6] for v in vs)}..{max(v[6] for v in vs)}, +0xd v {min(v[7] for v in vs)}..'
          f'{max(v[7] for v in vs)}, +0xe {sorted(set(v[8] for v in vs))[:8]}')
    print(f'   face +0 {sorted(set(f[0] for f in fs))[:12]}, +0xe {sorted(set(f[7] for f in fs))[:12]}, '
          f'max index {max(max(f[1:4]) for f in fs)}')


def bfile(n):
    b = open(os.path.join(FILES, '%02d.bin' % n), 'rb').read()
    assert b[:3] == b'B!\x1a'
    nv, nf, vs, fs, end = mesh(b, 4)
    stats('file %d (B!, byte 3 = %#x, %d bytes)' % (n, b[3], len(b)), nv, nf, vs, fs)
    rest = len(b) - end
    if rest:
        print(f'   {rest} bytes after the mesh = {rest / (16 * nv):.3f} vertex frames of {16 * nv} bytes')


def wldd(n):
    b = open(os.path.join(FILES, '%02d.bin' % n), 'rb').read()
    cnt = struct.unpack_from('<H', b, 3)[0]
    print(f'file {n}: {b[:8]!r}, {cnt} objects, {len(b)} bytes')
    offs = []
    for i in range(cnt):
        x, y, z, a, bb, c, off, anim, r = struct.unpack_from('<iiihhhIIi', b, 8 + 0x20 * i)
        pad = struct.unpack_from('<H', b, 8 + 0x20 * i + 0x1e)[0]
        offs.append(off)
        if i < 6 or i == cnt - 1:
            nv, nf = struct.unpack_from('<HH', b, 4 + off)
            print(f'   obj {i}: pos ({x}, {y}, {z}) angles ({a}, {bb}, {c}) mesh at +{off:#x} (file +{off + 4:#x}): '
                  f'{nv} v, {nf} f; anim {anim:#x} radius {r} +0x1e {pad:#x}')
    tv = tf = 0
    for i, off in enumerate(offs):
        nv, nf, vs, fs, end = mesh(b, 4 + off)
        tv += nv; tf += nf
        nxt = sorted(o for o in offs if o > off)
        gap = (nxt[0] + 4 if nxt else len(b)) - end
        if gap:
            print(f'   obj {i}: {gap} bytes between mesh end and next')
    print(f'   total {tv} vertices, {tf} faces; first mesh at +{min(offs) + 4:#x}, header ends at +{8 + 0x20 * cnt:#x}')
    nv, nf, vs, fs, end = mesh(b, 4 + offs[0])
    stats('   obj 0', nv, nf, vs, fs)


if __name__ == '__main__':
    for a in sys.argv[1:] or ['18', '24', '45', '50', '51', '28']:
        n = int(a)
        (wldd if n == 28 else bfile)(n)

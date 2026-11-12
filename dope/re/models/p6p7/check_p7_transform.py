"""p7: model of the transformation of the tentacle object (2e0b -> 1dd6 matrix, 1cb0/1b90 vertices) checked against
the engine buffers (segment 3de1) in a memory dump taken where the drawing loop 2c90 starts (all buffers of the
frame complete): screen coordinates (+0x6d60), env coordinates (+0x55f0: n.m + 0x40004000, no negation),
bump coordinates (+0x3e80: object-space normal x | y << 16, each wrapped to 15 bits), depth (+0x84d0)."""
import struct
import sys
from snap import img
import sine
import mesh
from check_p6_transform import matrix as _m6, s32, trunc_div, sar

b7 = img('p7')
SIN = sine.build(b7, 0x100 + 0x410)
import check_p6_transform as c6
c6.SIN = SIN                       # same matrix code (p7 1dd6 = p6 1b56, checked by reading), p7's own table
L = 0xF64


def wrap15(x):
    x = (x << 1) & 0xffff
    x = x - 0x10000 if x & 0x8000 else x
    return (x >> 1) & 0xffff


def main(path):
    a = open(path, 'rb').read()
    cs = (0x10 + L) * 16
    def w(off): return struct.unpack_from('<H', a, cs + off)[0]
    def sw(off): return struct.unpack_from('<h', a, cs + off)[0]
    def d(off): return struct.unpack_from('<i', a, cs + off)[0]
    m = mesh.load('p7', 0x23b4)
    o = 0x18f8
    ang = [(w(o + 0x1e + 2 * k) + w(0x182e + 2 * k)) & 0xffff for k in range(3)]
    ang = [sar(x - 0x10000 if x & 0x8000 else x, 4) for x in ang]
    M = c6.matrix(*ang)
    pos = [s32(d(o + 6 + 4 * k) + d(0x1816 + 4 * k)) for k in range(3)]
    F = sw(o + 4)
    gs = (0x3de1 + L) * 16
    base = w(0x19a2) - len(m['V'])   # [19a2] has already advanced past this object
    base = 0
    ok = [0, 0, 0, 0]
    for i in range(len(m['V'])):
        n = [int(x) for x in m['N'][i]]
        d0 = s32(sum(M[0][j] * n[j] for j in range(3)))
        d1 = s32(sum(M[1][j] * n[j] for j in range(3)))
        env = (((d0 & 0xffff0000) | ((d1 >> 16) & 0xffff)) + 0x40004000) & 0xffffffff
        bump = wrap15(n[0]) | (wrap15(n[1]) << 16)
        v = [int(x) for x in m['V'][i]]
        x = s32(sum(M[0][j] * v[j] for j in range(3)) + (pos[0] << 8))
        y = s32(sum(M[1][j] * v[j] for j in range(3)) + (pos[1] << 8))
        z = s32(sum(M[2][j] * v[j] for j in range(3)))
        depth = (((sar(z, 16) * F) >> 14) + 0x4000) & 0xffff
        ebp = s32(z - (pos[2] << 8))
        q = trunc_div(x * F, ebp)
        sx = (sar(q, 3) + ((q >> 2) & 1) + 0xa0) & 0xffff
        q3 = 3 * trunc_div(y * F, ebp)
        sy = (sar(q3, 5) + ((q3 >> 4) & 1) + 0x64) & 0xffff
        k = 4 * (base + i)
        got = [struct.unpack_from('<I', a, gs + 0x6d60 + k)[0], struct.unpack_from('<I', a, gs + 0x55f0 + k)[0],
               struct.unpack_from('<I', a, gs + 0x3e80 + k)[0], struct.unpack_from('<H', a, gs + 0x84d0 + k)[0]]
        for j, e in enumerate((sy | (sx << 16), env, bump, depth)):
            ok[j] += e == got[j]
    print(path, 'vertices', len(m['V']), 'screen %d env %d bump %d depth %d' % tuple(ok), 'angles', ang, 'pos', pos, 'F', F)


for p in sys.argv[1:]:
    main(p)

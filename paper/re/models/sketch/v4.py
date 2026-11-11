from mm import M, IMG
import struct
img = M('img'); s = M(1250)
print('table 0x174a3:')
for k in range(11):
    a = 0x174a3 + 12*k
    nv, nf = img.by(a), img.by(a+1)
    ox, oy, oz = struct.unpack_from('<hhh', img.b, a+2)
    ptr = img.d(a+8)
    print(k, 'nv', nv, 'nf', nf, 'ofs', ox, oy, oz, 'mesh', hex(ptr))
objs = [s.d(0x5db8 + 4*k) for k in range(12)]
print([hex(o) for o in objs])
for k, o in enumerate(objs):
    mat = s.ints(o, 9); pos = s.ints(o+0x48, 3)
    nv, nf = s.d(o+0x6c), s.d(o+0x70)
    vis = 0
    fa = s.d(o+0x74)
    for f in range(nf):
        if not (s.by(fa + f*0x4c + 0x1d) & 0x80): vis += 1
    print(k, hex(o), 'nv', nv, 'nf', nf, 'pos', pos, 'mat', mat, 'visible faces', vis, 'light', s.ints(o+0x60,3))

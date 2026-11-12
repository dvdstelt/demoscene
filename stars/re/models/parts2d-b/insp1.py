import snap, struct
s = snap.load('f18451_133ad.snap'); m = snap.Mem(s['mem']); img = snap.Mem(snap.IMG)
print('seed img %08x snap %08x' % (img.u32(0x4af35), m.u32(0x4af35)))
sin_img = [img.s32(0xa740 + 4*i) for i in range(16)]
sin_snp = [m.s32(0xa740 + 4*i) for i in range(16)]
print('sin img', sin_img); print('sin snp', sin_snp)
print('a740 table same as image:', m.blk(0xa740, 0x2000) == snap.IMG[0xa740:0xb740 + 0x1000])
t = [m.s32(0xa740 + 4*i) for i in range(4096)]
print('max', max(t), 'min', min(t), 't[1024]', t[1024], 'cos0', m.s32(0xb740))
import math
err = max(abs(t[i] - round(65536*math.sin(2*math.pi*i/4096))) for i in range(4096))
print('err vs 65536 sin', err)
for base in (0x13048, 0x130a8):
    print(hex(base), list(m.blk(base, 96)))
for base, n in ((0x13178, 13), (0x13278, 17)):
    for i in range(n):
        d, so, w, h = struct.unpack_from('<IIII', s['mem'], base + 16*i)
        print(hex(base), i, 'dst', d, (d % 320, d // 320), 'src', so, (so % 320, so // 320), 'w', w, 'h', h)
f = snap.ovl(58)
import collections
print(collections.Counter(f).most_common(10))
print('loaded file 58 at', hex(m.u32(0x12810)), 'bufs', hex(m.u32(0x13034)), hex(m.u32(0x13038)), hex(m.u32(0x1303c)), 'code', hex(m.u32(0x13040)))
print('img vs snap palette same', m.blk(0x13048, 0x300) == snap.IMG[0x13048:0x13348])
print('13168', m.u32(0x13168), '13044', m.u32(0x13044), '13539', m.u32(0x13539), '13170', m.u32(0x13170))

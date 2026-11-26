"""Check the maps GenericInit builds against snapshot memory (f9191: gen2, all three shading types)."""
import sys
import numpy as np
import gen
from snap import snap

m = snap(int(sys.argv[1]) if len(sys.argv) > 1 else 9191)
d = 'gen2' if len(sys.argv) < 3 else sys.argv[2]
blk = lambda a, n=65536: np.frombuffer(m.rd(a, n), np.uint8)
objs = m.u32(0x834478)
draw = lambda i: [m.u32(objs + 0x74 * i + 0x60 + 4 * k) for k in range(4)]
types = {m.u32(objs + 0x74 * i + 0x48): draw(i) for i in range(m.u32(0x834468))}
print({t: [hex(x) for x in v] for t, v in types.items()})
env, _ = gen.pcx(d + '/envmap.pcx')
tex, _ = gen.pcx(d + '/texture.pcx')
print('envmap', env.shape, 'texture', tex.shape)
if 9 in types:
    e, b = types[9][1], types[9][2]
    print('type 9 envmap block == envmap.pcx[:256,:256]:', (blk(e).reshape(256, 256) == env[:256, :256]).all())
    bt = gen.bump_table(d + '/bump.pcx', 2)
    s = blk(b, 131072).reshape(256, 256, 2)
    print('type 9 bump table matches:', (s == bt).all(), 'mismatches', (s != bt).sum())
if 11 in types:
    t4, ph, lit = types[11][1], types[11][2], types[11][3]
    s = blk(t4, 262144).reshape(256, 256, 4)
    bt = gen.bump_table(d + '/bump.pcx', 4)
    print('type 11 table: bump', (s[:, :, :2] == bt).all(), 'texture', (s[:, :, 2] == tex[:256, :256]).all(),
          'byte 3 values', np.unique(s[:, :, 3])[:5])
    pm = gen.phong_map(128, 0, 28672, 36864, 12)
    sp = blk(ph).reshape(256, 256)
    print('phong map matches:', (sp == pm).all(), 'mismatches', (sp != pm).sum(), 'max', sp.max())
    if (sp != pm).any():
        ys, xs = np.nonzero(sp != pm); print(list(zip(ys[:10], xs[:10], sp[ys[:10], xs[:10]], pm[ys[:10], xs[:10]])))
    lf = np.frombuffer(gen.load(d + '/texture.lit')[:65536], np.uint8)
    print('lit block == first 64 KB of texture.lit:', (blk(lit) == lf).all())

"""State of the 3D parts' variables and the geosphere in the snapshots."""
import os, sys, pickle, struct
import unicorn  # noqa: F401 (needed to unpickle the context)
import numpy as np
HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
def load(t):
    return pickle.load(open(os.path.join(HERE, 'snaps', 't%d.snap' % t), 'rb'))['mem']
m = load(11)
f = lambda a: struct.unpack('<f', m[a:a + 4])[0]
i = lambda a: struct.unpack('<i', m[a:a + 4])[0]
nv, nf = i(0xf72d44), i(0xf72d48)
print('geosphere: vertices', nv, 'faces', nf)
V = np.frombuffer(m[0xf61404:0xf61404 + 12 * nv], '<i2').reshape(nv, 6)
print(' vertex record (12 bytes) first rows:', V[:6].tolist())
r = np.sqrt((V[:, :3].astype(float) ** 2).sum(1))
print(' radius min %.2f max %.2f mean %.2f' % (r.min(), r.max(), r.mean()))
F = np.frombuffer(m[0xf6cf84:0xf6cf84 + 6 * nf], '<i2').reshape(nf, 3)
print(' faces first:', F[:4].tolist(), 'max index', F.max())
# part 7 object arrays
for name, base in (('part7', 0xf613dc), ('part8', 0xf72d4c)):
    print(name, 'pointers', [hex(i(base + 4 * k)) for k in range(6)])
print('part 9 buffers', [hex(i(a)) for a in (0xf73594, 0xf73580, 0xf73598, 0xf73584)])
print('part 10 buffers', [hex(i(a)) for a in (0xf7391c, 0xf73904, 0xf73918, 0xf73908)])
# float orig of part 7 = int16 vertices?
o7 = np.frombuffer(m[i(0xf613f0):i(0xf613f0) + 0x34 * nv], '<f4').reshape(nv, 13)
print('part7 orig first', o7[:2, :3].tolist(), 'equal to int16:', np.array_equal(o7[:, :3], V[:, :3].astype(np.float32)))
fa = np.frombuffer(m[i(0xf613dc):i(0xf613dc) + 0x48 * 2], '<u4').reshape(2, 18)
print('part7 face 0 dwords', [hex(x) for x in fa[0]])
print('part7 face 0 floats uv', np.frombuffer(m[i(0xf613dc) + 0x1c:i(0xf613dc) + 0x34], '<f4').tolist())
for t in (3084, 4621, 6924, 10380):
    m = load(t)
    f = lambda a: struct.unpack('<f', m[a:a + 4])[0]
    i = lambda a: struct.unpack('<i', m[a:a + 4])[0]
    print('t%d:' % t, 'light', f(0x427f70), 'p7', f(0xf61400), f(0xf613f4), f(0x427f80), i(0x427f74),
          'p8', f(0xf72d70), f(0x427fe4), f(0x427fec), f(0xf72d6c), f(0x427ffc), i(0x427ff0),
          'p9', [f(0xf73574 + 4 * k) for k in range(3)], f(0x428010), i(0x428140), f(0xf735a0),
          'phases', [f(0xf73588 + 4 * k) for k in range(3)], 'p10', f(0xf73920), f(0xf73924),
          'dt', f(0x420489), 'proj', f(0x422534), f(0x422538))
m = load(10380)
print('memory 0xf73904..0xf739a8 at t10380 (dwords):')
for a in range(0xf73904, 0xf739a8, 4):
    v = struct.unpack('<I', m[a:a + 4])[0]
    print('  %#x %#010x %r' % (a, v, struct.unpack('<f', m[a:a + 4])[0]))

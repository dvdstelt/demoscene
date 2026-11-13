"""Dump the static data the 3D parts read (tile colours, blob parameters, cube tables, initial values)."""
import os, sys, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep

print('tile colour table 0x428164 (64 tiles x RGB dwords, as an 8x8 grid):')
T = [struct.unpack('<i', rep.rd(0x428164 + 4 * i, 4))[0] for i in range(192)]
for r in range(8):
    print('  row %d:' % r, ' '.join('(%2d,%2d,%2d)' % tuple(T[3 * (8 * r + c):3 * (8 * r + c) + 3]) for c in range(8)))
print('  dwords after: ', [hex(rep.rd32(0x428164 + 4 * 192 + 4 * i)) for i in range(2)])
print()
for a, kind, name in [
        (0x427f70, 'f', 'light scale'), (0x427f74, 'i', 'part7 701 counter'), (0x427f78, 'f', 'part7 speed'),
        (0x427f7c, 'f', 'part7 K'), (0x427f80, 'f', 'part7 fade'),
        (0x427fe4, 'f', 'part8 Z'), (0x427fe8, 'f', 'part8 802'), (0x427fec, 'f', 'part8 fade'), (0x427ff0, 'i', 'part8 columns'),
        (0x427ff4, 'f', 'part8 speed'), (0x427ff8, 'f', 'part8 K'), (0x427ffc, 'f', 'part8 phase'),
        (0x428000, 'f', 'part9 amp'), (0x428004, 'i', 'part9 R0'), (0x428008, 'i', 'part9 R1'), (0x42800c, 'i', 'part9 R2'),
        (0x428010, 'f', 'part9 fade'), (0x428014, 'f', 'part9 901'), (0x428018, 'i', 'part9 nfaces'), (0x42801c, 'i', 'part9 nverts'),
        (0x428140, 'i', 'part9 902 index'), (0x428144, 'f', 'part10 amp'), (0x428148, 'i', 'part10 R0'), (0x42814c, 'i', 'part10 R1'),
        (0x428150, 'i', 'part10 R2'), (0x428154, 'i', 'part10 nfaces'), (0x428158, 'i', 'part10 nverts')]:
    v = rep.rdf(a) if kind == 'f' else struct.unpack('<i', rep.rd(a, 4))[0]
    print('  %#x %-18s %r  (%#010x)' % (a, name, v, rep.rd32(a)))
print()
print('cube faces 0x428020 (12 x {a, b, c, mask bit}):')
for k in range(12):
    print('  ', [struct.unpack('<i', rep.rd(0x428020 + 16 * k + 4 * j, 4))[0] for j in range(4)])
print('cube corners 0x4280e0 (8 x float xyz):')
for k in range(8):
    print('  ', [rep.rdf(0x4280e0 + 12 * k + 4 * j) for j in range(3)])
print('after corners:', hex(0x4280e0 + 96), [hex(rep.rd32(0x4280e0 + 96 + 4*i)) for i in range(4)])
print('octahedron 0x427f84 / 0x427f9c:', [hex(rep.rd32(0x427f84 + 4*i)) for i in range(14)])

"""Print the engine's object table, counts and texture table from a snapshot: python3 inspect_objs.py TICK"""
import sys; sys.path.insert(0, '.')
import snap
t = int(sys.argv[1]); M = snap.Mem(t)
V, F, O = M.d(0x18d4c), M.d(0x18d48), M.d(0x18d50)
print(f't{t}: verts {V:#x} faces {F:#x} objs {O:#x} [0x18d40] {M.d(0x18d40):#x} [0x18d44] {M.d(0x18d44):#x} wrapuv {M.d(0x1480c):#x}')
print('nobj', M.d(0x18d58), 'nvert', M.d(0x18d5c), 'nface', M.d(0x18d60), 'path idx', M.d(0x157a0),
      'timescale', M.f(0x1579c), 'dist', M.f(0x154b4), '[0x6542]', M.i(0x6542), 'flag15c3f', M.b(0x15c3f),
      '[0x15c3b]', M.d(0x15c3b), 'T', M.f(0x159f7), 'roll', M.f(0x154b8))
print('eye', M.fa(0x1549c, 3), 'target', M.fa(0x154a8, 3))
tt = M.d(0x18d54); print('tex table', hex(tt), [hex(M.d(tt + 4 * i)) for i in range(20)])
for k in range(M.d(0x18d58) + 1):
    o = O + 0x50 * k
    print(k, 'face0', M.d(o), 'nf', M.d(o + 4), 'vert0', M.d(o + 8), 'nv', M.d(o + 12), 'mat', M.fa(o + 0x10, 12), 'z', M.f(o + 0x40), M.d(o + 0x44), M.d(o + 0x48), M.d(o + 0x4c))

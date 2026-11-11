import sys, struct
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re'); sys.path.insert(0, sys.argv[1])
import rep, m
M = m.load('r_after_setup')
S = rep.SEG
def i32(a): return m.i32(M, a)
for n in range(11):
    inst = m.d32(M, 0x5db8 + 4*n)
    e = 0x174a3 + 12*n
    nv, nf, ox, oy, oz, p = struct.unpack_from('<BBhhhI', S, e)
    ok = True
    for i in range(nv):
        x, y, z = struct.unpack_from('<hhh', S, p + 6*i)
        v = inst + 0x7c + 0x24*i
        if (i32(v), i32(v+4), i32(v+8)) != (x+ox, y+oy, z+oz): ok = False
    fp = m.d32(M, inst + 0x74)
    f0 = [hex(M[fp + 0x1c + k]) for k in range(4)]
    nrm = (i32(fp+0x24), i32(fp+0x28), i32(fp+0x2c))
    hdr = [i32(inst + 4*k) for k in range(0x7c//4)]
    print(n, hex(inst), 'verts ok', ok, 'nv nf', hdr[0x6c//4], hdr[0x70//4], 'face0 1c..1f', f0, 'n0', nrm, 'pos', hdr[0x48//4:0x48//4+3])
gnd = m.d32(M, 0x5dd8)
print('ground', hex(gnd), [i32(gnd + 0x6c), i32(gnd+0x70)])
for i in (0, 1, 16, 17, 18, 186):
    v = gnd + 0x7c + 0x24*i
    print('  gv', i, i32(v), i32(v+4), i32(v+8))
pen = m.d32(M, 0x5de4)
print('pencil', hex(pen), [i32(pen + k) for k in (0x48, 0x4c, 0x50, 0x6c, 0x70)])
print('hump table', [i32(0x21455 + 4*k) for k in range(18)])
print('69b7 table static?', bytes(M[0x69b7:0x69b7+0x300]) == S[0x69b7:0x69b7+0x300])
print(list(S[0x69b7:0x69b7+64]))

import sys
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import rep
S = rep.SEG
pal = S[0x169a3:0x169a3+768]
print('base palette (6-bit), groups of 32:')
for g in range(8):
    print(g*32, [tuple(pal[3*i:3*i+3]) for i in range(g*32, g*32+32)])
print('teal 0x681a (entries 128..159):', [tuple(S[0x681a+3*i:0x681a+3*i+3]) for i in range(32)])
print('table 0x3a89:', list(S[0x3a89:0x3a89+16]))
for name, a in (('teal plane 0x187d5', 0x187d5), ('flock plane 0x1878b', 0x1878b)):
    nv, nf = S[a], S[a+1]
    print(name, 'nverts', nv, 'nfaces', nf)
    p = a + 2
    vs = []
    for i in range(nv):
        v = [x - 256 if x > 127 else x for x in S[p:p+3]]; p += 3; vs.append(v)
    print('  verts', vs)
    fs = []
    for i in range(nf):
        fs.append(list(S[p:p+4])); p += 4
    print('  faces', [(f[0], f[1], f[2], hex(f[3])) for f in fs])
    print('  end', hex(p))
print('k 4843', rep.rd32(0x4843), 'pos 6479..', [rep.rd32(0x6479+4*i) for i in range(3)])
print('L 1e48c', [rep.rd32(0x1e48c+4*i) - (1<<32 if rep.rd32(0x1e48c+4*i) >> 31 else 0) for i in range(3)])
print('floats 23a8', [rep.rdf(0x23a8+4*i) for i in range(16)])
print('42e0', rep.rdf(0x42e0), '39a0', rep.rdf(0x39a0), '1e460', rep.rdf(0x1e460), '1e464', rep.rdf(0x1e464))

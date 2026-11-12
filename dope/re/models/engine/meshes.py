"""List the meshes each engine part puts into its object list (script writes 'mov word cs:[slot], seg' and the
initial records), with their header, sizes and file offsets in unpacked/<part>.bin.
usage: python3 models/engine/meshes.py"""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..'))
from engine_model import PARTS

for p, c in PARTS.items():
    img = open(os.path.join(HERE, '..', '..', 'unpacked', p + '.bin'), 'rb').read()
    relocs = set()
    for line in open(os.path.join(HERE, '..', '..', 'unpacked', p + '.txt')):
        f = line.split()
        if len(f) >= 2 and f[1].startswith('0x') and ':' in f[0]:
            relocs.add(int(f[1], 16))
    slots = {c['objs'] + k * 0x3a: k for k in range(1, (c['objlast'] - c['objs']) // 0x3a + 1)}
    segs = {}
    for a, k in slots.items():                         # initial records
        s = struct.unpack_from('<H', img, 0x100 + a)[0]
        if 0x100 + a in relocs:
            segs.setdefault(s, set()).add('init slot %d' % k)
    i = 0
    while True:                                        # 2e c7 06 addr imm16
        i = img.find(b'\x2e\xc7\x06', i, 0x10100)
        if i < 0:
            break
        a, v = struct.unpack_from('<HH', img, i + 3)
        if a in slots and (i + 5) in relocs:
            segs.setdefault(v, set()).add('0010:%04x slot %d' % (i - 0x100, slots[a]))
        i += 1
    print('== %s (object slots %04x..%04x)' % (p, c['objs'], c['objlast']))
    for s in sorted(segs):
        nv, nf, opos, ofac, onrm, otex = struct.unpack_from('<6H', img, s * 16)
        end = max(opos + 6 * nv, ofac + 8 * nf, onrm + 6 * nv, otex + 4 * nv)
        mats = set()
        for j in range(nf):
            w = struct.unpack_from('<H', img, s * 16 + ofac + 8 * j)[0]
            mats.add(w)
        ext = [max(abs(struct.unpack_from('<h', img, s * 16 + opos + 6 * j + 2 * q)[0]) for j in range(nv)) for q in range(3)]
        print('  seg %04x (file 0x%05x, %5d bytes): %4d vertices, %4d faces, offsets pos %04x faces %04x normals %04x '
              'tex %04x; |xyz| max %s; face words %s; used by %s' % (
                  s, s * 16, end, nv, nf, opos, ofac, onrm, otex, ext, sorted('%04x' % m for m in mats)[:6],
                  ', '.join(sorted(segs[s]))[:120]))

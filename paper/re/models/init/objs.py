import sys, struct, math
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re'); sys.path.insert(0, sys.argv[1])
import rep
import numpy as np
from PIL import Image, ImageDraw
OUT = sys.argv[1]
S = rep.SEG
pal = np.frombuffer(S[0x169a3:0x169a3+768], np.uint8).reshape(256, 3).astype(int) * 255 // 63
T3db8 = list(S[0x3db8:0x3dc0]); T3a89 = list(S[0x3a89:0x3a8c])
def parse_word(entry):
    nv, nf, ox, oy, oz, p = struct.unpack_from('<BBhhhI', S, entry)
    V = []
    for i in range(nv):
        x, y, z = struct.unpack_from('<hhh', S, p + 6*i)
        V.append((x + ox, y + oy, z + oz))
    F = []
    q = p + 6*nv
    for i in range(nf):
        a, b, c, fl = S[q:q+4]; q += 4
        F.append((a, b, c, fl, (T3db8[fl >> 4] - 4) & 0xff))
    return V, F, (ox, oy, oz), p
def parse_byte(addr, al):
    nv, nf = S[addr], S[addr+1]
    V = []
    q = addr + 2
    for i in range(nv):
        x, y, z = struct.unpack_from('<bbb', S, q); q += 3
        V.append((x*8, y*8, z*8))
    F = []
    for i in range(nf):
        a, b, c, fl = S[q:q+4]; q += 4
        F.append((a, b, c, fl, (T3a89[fl >> 4] + al) & 0xff if (fl >> 4) < 3 else None))
    return V, F
def render(V, F, name, view=('x', 'y'), title=''):
    V = np.array(V, float)
    ax = {'x': 0, 'y': 1, 'z': 2}
    i, j = ax[view[0]], ax[view[1]]
    W = 400
    mn = V.min(0); mx = V.max(0); span = max(mx[i]-mn[i], mx[j]-mn[j], 1)
    sc = (W-20) / span
    img = Image.new('RGB', (W, W), (255, 255, 255))
    d = ImageDraw.Draw(img)
    # painter by depth along remaining axis
    k = 3 - i - j
    order = sorted(range(len(F)), key=lambda f: -sum(V[F[f][n]][k] for n in range(3)))
    for f in order:
        a, b, c, fl, col = F[f]
        pts = [((V[n][i]-mn[i])*sc+10, (V[n][j]-mn[j])*sc+10) for n in (a, b, c)]
        rgb = tuple(pal[col]) if col is not None else (255, 0, 255)
        d.polygon(pts, fill=rgb, outline=(0, 0, 0))
    d.text((5, 5), title, fill=(0, 0, 0))
    img.save(OUT + '/' + name)
def objwrite(V, F, name):
    with open(OUT + '/' + name, 'w') as f:
        for v in V: f.write('v %d %d %d\n' % v)
        for a, b, c, fl, col in F: f.write('f %d %d %d\n' % (a+1, b+1, c+1))
for n in range(11):
    e = 0x174a3 + 12*n
    V, F, off, p = parse_word(e)
    Vn = np.array(V); 
    raw = np.array([struct.unpack_from('<hhh', S, p + 6*i) for i in range(len(V))])
    flags = sorted(set(f[3] for f in F))
    cols = sorted(set(f[4] for f in F))
    print('obj%d entry %x data %x nv %d nf %d off %s rawmin %s rawmax %s flags %s cols %s' % (n, e, p, len(V), len(F), off, raw.min(0), raw.max(0), [hex(x) for x in flags], [hex(x) for x in cols]))
    for view in (('x', 'y'), ('x', 'z'), ('z', 'y')):
        render(V, F, 'obj%d_%s%s.png' % (n, view[0], view[1]), view, 'obj%d %s%s' % (n, view[0], view[1]))
    objwrite(V, F, 'obj%d.obj' % n)
for addr, al, nm in ((0x1e353, 0, 'pencil'), (0x1878b, 0x1e, 'plane_flock'), (0x187d5, 0x20, 'plane_teal')):
    V, F = parse_byte(addr, al)
    raw = np.array(V)
    print(nm, hex(addr), 'nv', len(V), 'nf', len(F), raw.min(0), raw.max(0), 'flags', sorted(set(hex(f[3]) for f in F)), 'cols', sorted(set(f[4] for f in F if f[4] is not None)))
    for view in (('x', 'y'), ('x', 'z'), ('z', 'y')):
        render(V, F, '%s_%s%s.png' % (nm, view[0], view[1]), view, nm)
    objwrite(V, F, nm + '.obj')

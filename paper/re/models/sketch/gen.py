import ast, json, math
from mm import M
rows = [ast.literal_eval(l) for l in open('strokes.txt') if l.startswith('(')]
st = json.load(open('starts.json'))
def n(r):
    L = math.hypot(r[9]-r[7], r[10]-r[8])
    return max(0, int(round(math.sqrt(L)*1.7300000190734863)) - 3)
items = []
for r in rows:
    e, ordn, fl, k, f, va, vb, ax, ay, bx, by = r
    items.append('%3d %2d/%-3d %3d>%-3d (%3d,%3d)>(%3d,%3d) n%-2d t%d' % (e, k, f, va, vb, ax, ay, bx, by, n(r), st[str(e)]))
with open('strokelist.txt', 'w') as fo:
    for i in range(0, len(items), 2):
        fo.write('  |  '.join(items[i:i+2]) + '\n')
img = M('img')
pal = img.b[0x169a3:0x169a3+768]
with open('pal.txt', 'w') as fo:
    for row in range(16):
        fo.write('%02x: ' % (row*16) + ' '.join('%02x%02x%02x' % tuple(pal[(row*16+c)*3:(row*16+c)*3+3]) for c in range(16)) + '\n')
# raw table
raw = []
for e in range(0x113):
    a = 0x5073 + 5*e
    raw.append('%d:%x' % (img.d(a), img.by(a+4)))
open('rawtable.txt','w').write(' '.join(raw))
print(len(items))

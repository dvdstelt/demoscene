import sys, struct
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re'); sys.path.insert(0, sys.argv[1])
import rep, m
from PIL import Image, ImageDraw
S = rep.SEG
M = m.load('r_after_init')
sin = [m.i32(M, 0x22479 + 4*k) for k in range(1024)]
rec = lambda n: 65536 // n
def sar(x, n): return x >> n
def quad(P0, P1, P2, n):
    step = rec(n) >> 7
    B = 4*P1 - P2 - 3*P0
    A = P0 - 2*P1 + P2
    out = []
    t = 0
    for k in range(n + 1):
        e = B * t
        s = sar(sar(A * t, 0) * t, 8)
        e = sar(e + s, 8) + 2*P0
        out.append(sar(e, 1))
        t += step
    return out
def wrap32(x):
    x &= 0xffffffff
    return x - (1 << 32) if x >> 31 else x
def sx(w): return sar(w * 0x2d000, 16) + 0x12c
def sy(w): return sar(w * 0x23000, 16) + 0x64
pts = []
strokes = []
p = 0x18a2f
for k in range(0x6a):
    typ, hdr = S[p], S[p+1]; p += 2
    n = hdr >> 2
    if n == 0: n = 1
    st = []
    if typ == 2:
        if n < 3: n = 3
        step = rec(n) >> 6
        cx, cy, r = struct.unpack_from('<HHH', S, p); p += 6
        a = 0
        while True:
            s = sin[a & 0x3ff]; c = sin[(a + 0x100) & 0x3ff]
            X = sar(wrap32(s * r + (cx << 16)), 16); Y = sar(wrap32(c * r + (cy << 16)), 16)
            st.append((sar(X * 0x2d000, 16) + 0x12c, sar(Y * 0x23000, 16) + 0x64))
            if a >= 0x400: break
            a += step
    else:
        if typ == 1:
            x0, y0, x2, y2 = struct.unpack_from('<HHHH', S, p); p += 8
            x1 = (x0 + x2) >> 1; y1 = (y0 + y2) >> 1
        else:
            x0, y0, x1, y1, x2, y2 = struct.unpack_from('<HHHHHH', S, p); p += 12
        xs = quad(sx(x0), sx(x1), sx(x2), n)
        ys = quad(sy(y0), sy(y1), sy(y2), n)
        st = list(zip(xs, ys))
    strokes.append((typ, n, st))
    pts += st + [(0, 0)]
pts += [(0, 0)]
base = m.d32(M, 0x483b)
cnt = m.d32(M, 0x4833)
mem = [(m.i32(M, base + 8*i), m.i32(M, base + 8*i + 4)) for i in range(cnt + 1)]
print('end ptr', hex(p), 'points', len(pts), 'mem count', cnt)
print('match', mem[:len(pts)] == pts[:len(mem)], sum(1 for a, b in zip(mem, pts) if a != b))
bad = [(i, a, b) for i, (a, b) in enumerate(zip(mem, pts)) if a != b][:5]
print(bad)
import collections
print(collections.Counter(t for t, n, s in strokes))
allp = [q for t, n, s in strokes for q in s]
xs = [q[0] for q in allp]; ys = [q[1] for q in allp]
print('x range', min(xs), max(xs), 'y range', min(ys), max(ys))
W = 1520; H = 840
img = Image.new('L', (W, H), 255)
d = ImageDraw.Draw(img)
for t, n, s in strokes:
    d.line([(x - min(xs) + 10, y - min(ys) + 10) for x, y in s], fill=0, width=2)
img.save(sys.argv[1] + '/strokes.png')

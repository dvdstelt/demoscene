import sys, struct
sys.path.insert(0, '.'); sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import rep, snap
from model import curve2ad0, s32, sar
from PIL import Image, ImageDraw
S = rep.SEG
sinT = [struct.unpack_from('<i', S, 0)[0]] and None
m = snap.M(4500)
sinT = [m.i32(0x22479 + 4 * i) for i in range(1024)]
recip = lambda n: 65536 // n
esi = 0x18a2f
pts = []
types = []
end_rec = None
for rec in range(0x6a):
    w = struct.unpack_from('<H', S, esi)[0]; esi += 2
    typ, cnt = w & 0xff, w >> 8
    n = cnt >> 2 or 1
    types.append((typ, cnt))
    if typ == 2:
        if n < 3: n = 3
        step = recip(n) >> 6
        cx, cy, r = struct.unpack_from('<HHH', S, esi); esi += 6
        a = 0
        while True:
            sx = sinT[a & 0x3ff]; cs = sinT[(a + 0x100) & 0x3ff]
            x = sar(s32(sx * r + (cx << 16)), 16); y = sar(s32(cs * r + (cy << 16)), 16)
            pts.append((sar(x * 0x2d000, 16) + 0x12c, sar(y * 0x23000, 16) + 0x64))
            if a >= 0x400: break
            a += step
        pts.append((0, 0))
        continue
    if typ == 1:
        x0, y0, x1, y1 = struct.unpack_from('<HHHH', S, esi); esi += 8
        xs = [x0, (x0 + x1) >> 1, x1]; ys = [y0, (y0 + y1) >> 1, y1]
    else:
        x0, y0, x1, y1, x2, y2 = struct.unpack_from('<HHHHHH', S, esi); esi += 12
        xs = [x0, x1, x2]; ys = [y0, y1, y2]
    X = [sar(v * 0x2d000, 16) + 0x12c for v in xs]
    Y = [sar(v * 0x23000, 16) + 0x64 for v in ys]
    cx = curve2ad0(X[0], X[1], X[2], n); cy = curve2ad0(Y[0], Y[1], Y[2], n)
    for i in range(n + 1): pts.append((cx[i], cy[i]))
    pts.append((0, 0))
print('data end %#x' % esi, 'records', len(types), 'type counts', {t: sum(1 for x in types if x[0] == t) for t in (0, 1, 2)})
print('points incl separators', len(pts), '[0x4833]=', m.u32(0x4833))
# compare with memory list
p = m.u32(0x483b)
mem = [(m.i32(p + 8 * i), m.i32(p + 8 * i + 4)) for i in range(len(pts) + 2)]
print('match memory list:', mem[:len(pts)] == pts, mem[len(pts):])
xs = [x for x, y in pts if x]; ys = [y for x, y in pts if x]
print('u range', min(xs), max(xs), 'v range', min(ys), max(ys))
im = Image.new("RGB", (800, 450), 'white'); d = ImageDraw.Draw(im)
prev = None
for x, y in pts:
    if x == 0: prev = None; continue
    if prev: d.line([((prev[0]-300)*0.5+10, (prev[1]-100)*0.5+10), ((x-300)*0.5+10, (y-100)*0.5+10)], fill='black', width=2)
    prev = (x, y)
im.save('hw_uv.png')

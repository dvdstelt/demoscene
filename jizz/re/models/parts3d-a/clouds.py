"""Model of 0x17553 (two textured planes, floor and ceiling) + 0x1770b (black band), part 0x17003.

usage: python3 clouds.py TICK [phase]   compares with full.frames at TICK (uses full.flips for the timer value)
The texture is the interleaved buffer [0x12425] built by 0x17762: dword per texel, low word = top plane (first
argument), high word = bottom plane (second argument), 16-bit 5:6:5 from 0x13d73.
"""
import sys, math
import numpy as np
from snap import mem, frame, flips, RE
from PIL import Image

F = np.float32
K1, K2 = 2500, 2600          # [0x17531], [0x17543]
STEP = 2 * math.pi / 16384   # [0x11596] (double)

def to565(t):
    t = t.astype(np.uint32)
    return ((t[..., 0] >> 3) << 11) | ((t[..., 1] >> 2) << 5) | (t[..., 2] >> 3)

def tga565(name, roll=(0, 0)):
    t = np.array(Image.open(RE + '/dump/%s.tga' % name).convert('RGB'))
    return to565(np.roll(t, roll, (0, 1)))

def params(time, event):
    """[0x17535] H, [0x17539] angle, [0x1753b] X0, [0x1753f] Y0 as set by the part loop (0x17157..0x171d8)."""
    time &= 0xffffffff
    if event <= 199:
        H = 3660 - ((((time >> 4) * 3) & 0xffffffff) >> 9)
        H = max(H, 600)
        return H, 0, 0, (time >> 8) & 0xffff
    e = time >> 4
    return 600, (e >> 7) & 0xffff, (e >> 3) & 0xffff, (-e) & 0xffff

def fround(x):
    # fistp, round to nearest even
    return int(np.rint(x))

def render(time, event, top, bot, pc24=True):
    """top/bot: (256,256) uint16 textures. Returns (200,320) uint16 page rows 0..199 (band black)."""
    H, ang, X0, Y0 = params(time, event)
    r = (lambda x: F(x)) if pc24 else (lambda x: x)
    out = np.zeros((200, 320), np.uint16)
    a = float(r(ang * STEP))       # fmul qword: rounded to 24 bits by the precision control
    s, c = math.sin(a), math.cos(a)
    for d in range(100, 5, -1):
        t = r(H / d)
        A = r(t * K2)
        B = r(r(t - 1) * K1)
        u0 = r(r(B * s) + X0)
        v0 = r(-r(B * c) + Y0)
        Ac, As = r(A * c), r(A * s)
        ue = r(u0 + Ac); us = r(ue - r(Ac + Ac))
        ve = r(v0 + As); vs = r(ve - r(As + As))
        U = fround(r(us * 256)) & 0xffffffff
        V = fround(r(vs * 256)) & 0xffffffff
        du = fround(r(r(ue - us) * F(0.8))) & 0xffffffff
        dv = fround(r(r(ve - vs) * F(0.8))) & 0xffffffff
        # inner loop: 8.16 stepping in rotated registers (eax = ror(U,16), bl = int part)
        eax = ((U << 16) | (U >> 16)) & 0xffffffff; bl = (U >> 16) & 0xff
        edx = ((V << 16) | (V >> 16)) & 0xffffffff; bh = (V >> 16) & 0xff
        ia = ((du << 16) | (du >> 16)) & 0xffffffff; ib = (du >> 16) & 0xff
        ja = ((dv << 16) | (dv >> 16)) & 0xffffffff; jb = (dv >> 16) & 0xff
        rowt = 100 - d; rowb = 199 - (100 - d)
        for x in range(320):
            out[rowt, x] = top[bh, bl]
            out[rowb, x] = bot[bh, bl]
            s1 = eax + ia; cf = s1 >> 32; eax = s1 & 0xffffffff; bl = (bl + ib + cf) & 0xff
            s2 = edx + ja; cf = s2 >> 32; edx = s2 & 0xffffffff; bh = (bh + jb + cf) & 0xff
    out[95:107] = 0
    return out

def flip_for_tick(tick):
    best = None
    for p in flips():
        if int(p[1]) <= tick: best = p
    return best

if __name__ == '__main__':
    tick = int(sys.argv[1])
    start = -18355371          # [0x6542] in part 0x17003 (snapshot t1365)
    p = flip_for_tick(tick)
    timer = int(p[3]); time = timer + start
    ev = int(sys.argv[2]) if len(sys.argv) > 2 else 130
    if ev <= 200 and len(sys.argv) <= 3:
        top = bot = tga565('map8')
    else:
        top = tga565('map1'); bot = tga565('map1a', (64, 64))
    for pc in (True, False):
        img = render(time, ev, top, bot, pc)
        fr = frame(tick)
        eq = (img == fr)
        rows = [r for r in range(200) if not (95 <= r <= 106)]
        print('tick', tick, 'flip', p[0], 'time', time, 'pc24' if pc else 'ext', 'equal %.4f' % eq[rows].mean(),
              'rows exact', sum(eq[r].all() for r in rows), '/', len(rows))
    Image.fromarray(np.concatenate([__import__('snap').rgb565(img), __import__('snap').rgb565(fr)], 1)).save('clouds_%d.png' % tick)

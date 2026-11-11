"""Model of the 2D 'swirl tunnel' of part 0x169e7: table 0x16f4f (precalc) + mapper 0x16cd3.

usage: python3 tunnel2d.py TICK [window]   window: 0x3aa84 (events 500..599) or 0x29220 (event 410)
Compares with full.frames (whole frame, or outside the inset for event 410).
"""
import sys, math
import numpy as np
from snap import mem, frame, flips, rgb565, RE
from PIL import Image
F = np.float32
START2 = -41034228            # [0x6542] of part 0x169e7 (snapshot t2515)

def table(pc24=True):
    """600 x 340 dwords: (round(1.3*r) & 255) << 8 | (round(2*atan2(y,x)/(pi/128)) & 255)."""
    T = np.zeros((340, 600), np.uint32)
    k = F(0.02454369328916073)
    y = F(-169.5)
    for j in range(340):
        x = F(-299.5)
        for i in range(600):
            a = int(np.rint(F(F(math.atan2(y, x)) / k) * F(2)))
            r = int(np.rint(F(F(math.sqrt(F(F(x * x) + F(y * y)))) * F(1.2999999523162842))))
            T[j, i] = ((r & 0xff) << 8) | (a & 0xff)
            x = F(x + 1)
        y = F(y + 1)
    return T

def offsets(time):
    time &= 0xffffffff
    du = ((time * 0x59d4b) >> 32) & 0xff          # dl of edx  -> angle byte offset
    dv = ((time * 0x723eb) >> 40) & 0xff          # dh of edx  -> radius byte offset
    return du, dv

def render(T, time, window, tex):
    du, dv = offsets(time)
    row0, col0 = divmod(window // 4, 600)
    sub = T[row0:row0 + 200, col0:col0 + 320]
    idx = (sub + ((dv << 8) | du)) & 0xffff        # 16-bit add: the angle byte carries into the radius byte
    return tex.reshape(-1)[idx]

def flip_for_tick(tick):
    best = None
    for p in flips():
        if int(p[1]) <= tick: best = p
    return best

if __name__ == '__main__':
    tick = int(sys.argv[1]); window = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x3aa84
    m = mem(2515)
    T = table()
    Tm = m.arr(m.u32(0x16f4b), 600 * 340, '<u4').reshape(340, 600)
    print('table equal to snapshot:', (T == Tm).mean())
    tex = m.arr(m.u32(0x12425), 65536, '<u2')
    p = flip_for_tick(tick); time = int(p[3]) + START2
    img = render(Tm, time, window, tex)
    fr = frame(tick)
    eq = img == fr
    if window == 0x29220:
        mask = np.ones_like(eq); mask[76:173, 89:261] = False
        print('tick', tick, 'time', time, 'equal outside inset %.4f' % eq[mask].mean())
    else:
        print('tick', tick, 'time', time, 'equal %.4f' % eq.mean())
    Image.fromarray(np.concatenate([rgb565(img), rgb565(fr)], 1)).save('tunnel_%d.png' % tick)

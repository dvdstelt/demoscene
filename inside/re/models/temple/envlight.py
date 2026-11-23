"""Check the engine's environment mapping (0x41acc4) and point light (0x41b508) as the temple uses them, on the
vertices of a run_temple.py full dump (t2.pkl): u, v words (+0x28, +0x2a) of objects 0, 1 and the light level
(+0x30) of object 2."""
import math, sys
import numpy as np
import dumpread as D


def trunc(x): return int(math.trunc(x))


def sdiv64(a): return int(a / 64)          # the sbb idiom: signed division rounding toward zero


d = D.load(sys.argv[1])
for rec in d:
    m = D.Blocks(rec)
    cam = m.d32(0x425e74)
    Cw = m.mat(cam + 0x70); cpos = np.array(m.ints(cam + 0x1c, 3), float)
    eu = ev = 0; nchk = 0
    for oi in (0, 1):
        o = m.d32(0x425de0 + 4 * oi)
        R = Cw.T @ m.mat(o + 0x70)                       # 0x4b8f78: object to camera rotation
        v0 = m.d32(o + 0xd0)
        for k in range(m.i32(o + 0xd4)):
            a = v0 + 0x40 * k
            if not m.d32(a + 4) & 2: continue
            n2 = 2 * np.array(m.ints(a + 0x34, 3), float)
            xc, yc = m.i32(a + 0x20), m.i32(a + 0x24)
            U = trunc(R[0] @ n2 + 2048); V = trunc(R[1] @ n2 + 2048)
            u = (-(U & 0xfff) - sdiv64(xc)); v = ((V & 0xfff) + sdiv64(yc))
            u = (u + 0x8000) % 0x10000 - 0x8000; v = (v + 0x8000) % 0x10000 - 0x8000
            eu = max(eu, abs(u - m.i16(a + 0x28))); ev = max(ev, abs(v - m.i16(a + 0x2a))); nchk += 1
    # light on object 2
    lo = m.d32(0x425e5c)
    Rng, Br = m.i32(lo + 0xc8), m.i32(lo + 0xcc)        # +0xc8 = range (the frame routine's value), +0xcc = brightness 1600
    lpos = np.array(m.ints(lo + 0x1c, 3), float)
    o = m.d32(0x425de0 + 8)
    Mw = m.mat(o + 0x70); opos = np.array(m.ints(o + 0x1c, 3), float)
    L = Mw.T @ (lpos - opos)                             # light in object space (0x414150: transpose, as I read it)
    el = 0; nl = 0; maxlev = 0
    v0 = m.d32(o + 0xd0)
    for k in range(m.i32(o + 0xd4)):
        a = v0 + 0x40 * k
        if not m.d32(a + 4) & 2: continue
        p = np.array(m.ints(a + 8, 3), float); nrm = np.array(m.ints(a + 0x34, 3), float)
        dv = L - p; dist = math.sqrt(dv @ dv)
        lev = 0
        if dist <= Rng:
            dot = nrm @ dv
            if dot > 0:
                f = dot / (dist * 512)
                lum = Br if dist < 1 else Br - dist * (Br / Rng)
                c = lum * f
                if c > 0: lev = trunc(lev + c)
        lev = min(lev, 0x3e0)
        el = max(el, abs(lev - m.i16(a + 0x30))); nl += 1; maxlev = max(maxlev, lev)
    print(rec['frame'], 'env map: %d vertices, max |du| %d |dv| %d' % (nchk, eu, ev),
          '| light range=%d brightness=%d: %d vertices, max |dlevel| %d, max level %d' % (Rng, Br, nl, el, maxlev))

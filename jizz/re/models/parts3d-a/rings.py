"""Model of the ring / spoke overlay of part 0x17003: table init 0x180e5 (0x18181 rings, 0x181bc spokes) and the
per-frame step + projection 0x18009. Checks against p1_hooks.pkl (run_hook.py + hooks_p1.py).

Record (0x20 bytes): +0 x1 +4 y1 +8 x2 +0xc y2 (int32), +0x10 a1 +0x12 a2 (u16, 16384 = 360 deg),
+0x14 r1 +0x16 r2 (u16; only the low byte is stepped), +0x18 da1 +0x1a da2 (u16), +0x1c dr1 +0x1d dr2 (u8)."""
import math, pickle, struct
import numpy as np
F = np.float32
STEP = 2 * math.pi / 16384

def init():
    recs = []
    def ring(n, r, dr):
        cs = 0x4000 // n
        a = 0
        for i in range(n):
            recs.append([a, (a + cs) & 0xffff, r, r, 0x28, 0x28, dr, dr]); a = (a + cs) & 0xffff
    def spokes(n, r1, r2, dr, a0, da, step):
        a = a0
        for i in range(n):
            recs.append([a, a, r1, r2, step, step, dr, dr]); a = (a + da) & 0xffff
    ring(30, 0x50, 0xfe); ring(30, 0x70, 0xfe); ring(20, 0xa0, 0xff); ring(20, 0xb3, 0xfd)
    spokes(13, 0x73, 0x4d, 0xfe, 0x1f4, 0x1bc, 0x50)
    spokes(17, 0x78, 0x60, 0xfe, 0x1388, 0x2fc, 0xff40)
    spokes(20, 0x79, 0x10, 0x03, 0x2710, 0x2a0, 0x90)
    return recs   # [a1, a2, r1, r2, da1, da2, dr1, dr2]

def step(recs):
    """one call of 0x18009: advance, then project; returns the 150 segments (x1, y1, x2, y2)."""
    segs = []
    for r in recs:
        r[0] = (r[0] + r[4]) & 0xffff
        r[2] = (r[2] & 0xff00) | ((r[2] + r[6]) & 0xff)
        r[1] = (r[1] + r[5]) & 0xffff
        r[3] = (r[3] & 0xff00) | ((r[3] + r[7]) & 0xff)
        pts = []
        for a, rad in ((r[0], r[2]), (r[1], r[3])):
            a = a - 65536 if a >= 32768 else a       # fild word: signed
            rad = rad - 65536 if rad >= 32768 else rad
            th = a * STEP
            x = int(np.rint(F(F(F(math.cos(th)) * rad) * F(0.78)) + F(160)))
            y = int(np.rint(F(F(F(F(math.sin(th)) * rad) * F(0.78)) * F(0.78)) + F(100)))
            pts += [x, y]
        segs.append(pts)
    return segs

def parse(b):
    out = []
    for k in range(150):
        o = 32 * k
        x1, y1, x2, y2 = struct.unpack_from('<4i', b, o)
        a1, a2, r1, r2, da1, da2 = struct.unpack_from('<6H', b, o + 16)
        out.append(([a1, a2, r1, r2, da1, da2, b[o + 28], b[o + 29]], [x1, y1, x2, y2]))
    return out

if __name__ == '__main__':
    rec = pickle.load(open('p1_hooks.pkl', 'rb'))
    rings = rec['rings']
    print('0x18009 calls:', len(rings), 'ticks', rings[0][0], '..', rings[-1][0])
    # the table before the first call must be the init state (precalc), except x/y
    st = init()
    first = parse(rings[0][2])
    print('init state equal:', all(f[0] == s for f, s in zip(first, st)))
    prev_tick = None; n_ok = 0; n = 0; resets = 0
    for i, (tick, flips, b) in enumerate(rings):
        cur = parse(b)
        if any(c[0] != s for c, s in zip(cur, st)):
            # re-init at event 200 (0x180e5 is called again)
            st2 = init()
            if all(c[0] == s for c, s in zip(cur, st2)):
                st = st2; resets += 1
            else:
                print('state mismatch at call', i, 'tick', tick); break
        segs = step(st)
        if i + 1 < len(rings):
            nxt = parse(rings[i + 1][2])
            # positions written by this call are in the next record (unless reset in between)
            ok = all(nx[1] == sg for nx, sg in zip(nxt, segs))
            n += 1; n_ok += ok
    print('calls whose projected segments match exactly:', n_ok, '/', n, 'resets seen:', resets)

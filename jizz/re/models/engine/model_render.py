"""Python model of Nix's renderer 0x1f5d1 (transform 0x18ea1/0x19022/0x19118, face list 0x19f78, radix sort 0x1966e,
draw loop 0x19240, triangle filler 0x1e07c with its span loops 0x1e77c / 0x1f07c), run on a render dump from
run_engine.py --dump-render (r<N>.pre = state at the call of 0x1f5d1, r<N>.post = target page at its return).

All x87 arithmetic is done in float32 (the intro runs the FPU with 24-bit precision; fild/fld are exact,
fist/fistp round to nearest even). Integer DDAs use 32-bit wrap-around arithmetic.

usage: python3 model_render.py DUMPDIR N [--png out.png]      prints the number of differing pixels
"""
import os, sys, pickle, struct
import numpy as np

f32 = np.float32
INDEF = -0x80000000


def i32(x):
    x &= 0xffffffff
    return x - (1 << 32) if x & 0x80000000 else x


def fist(x):
    """x87 fist(p) to a dword: round to nearest even; out of range or NaN -> integer indefinite"""
    x = float(x)
    if x != x or x >= 2147483647.5 or x < -2147483648.5:
        return INDEF
    return int(np.rint(x))


def fbits(x): return struct.unpack('<I', struct.pack('<f', float(x)))[0]


def sbits(x): return struct.unpack('<i', struct.pack('<f', float(x)))[0]


class Mem:
    def __init__(self, pre):
        self.low = pre['low']; self.eng_lo = pre['eng_lo']; self.eng = pre['eng']; self.tex = pre['tex']

    def buf(self, a):
        if a < 0x33000: return self.low, a
        if 0x220000 <= a < 0x560000: return self.tex, a - 0x220000
        if self.eng_lo <= a < self.eng_lo + len(self.eng): return self.eng, a - self.eng_lo
        raise KeyError(hex(a))

    def u32(self, a): b, o = self.buf(a); return struct.unpack_from('<I', b, o)[0]
    def i32(self, a): b, o = self.buf(a); return struct.unpack_from('<i', b, o)[0]
    def f(self, a): b, o = self.buf(a); return f32(struct.unpack_from('<f', b, o)[0])
    def u8(self, a): b, o = self.buf(a); return b[o]


class Vert:
    """the transformed fields of one 0x40-byte vertex record"""
    __slots__ = ('x', 'y', 'z', 'sx', 'sy', 'u', 'v', 'eu', 'ev', 'inside', 'out', 'flag', 'nx', 'ny', 'nz',
                 'ox', 'oy', 'oz')


def transform(m, W, H):
    """0x18e3b: every object -> 0x18ea1 (vertices) and 0x19f78 (face list). Returns (verts by address, list)."""
    # 0x19118 constants (self-modified immediates)
    clip_y = fbits(f32(H))                         # [0x18fa3]
    clip_x = fbits(f32(f32(W) * f32(65536)))       # [0x18f58]
    near = 16                                      # [0x18f0b]
    cy = f32(H >> 1)                               # [0x191bf]
    cx = f32(f32(W >> 1) * f32(65536))             # [0x191bb]
    kx = f32(f32(f32(f32(1) / f32(65536)) * f32(320)) / f32(W))   # [0x1921b]
    ky = -f32(f32(f32(1) * f32(200)) / f32(H))     # [0x1921f]
    kz = f32(1)                                    # [0x19223]
    VB = m.u32(0x18d4c); FB = m.u32(0x18d48); OB = m.u32(0x18d50)
    verts = {}
    lst = []
    for o in range(m.u32(0x18d58)):
        ob = OB + o * 0x50
        fl = m.u32(ob + 0x4c)
        if fl & 0x40: continue
        if fl & 0x80: raise NotImplementedError('object callback')
        mat = [m.f(ob + 0x10 + 4 * i) for i in range(12)]
        F = m.f(ob + 0x40)
        # 0x19022
        r0 = [f32(mat[i] / kx) for i in range(3)]
        r1 = [f32(mat[3 + i] / ky) for i in range(3)]
        r2 = [f32(mat[6 + i] / kz) for i in range(3)]
        tx = f32(mat[9] / kx); ty = f32(mat[10] / ky); tz = f32(f32(mat[11] / kz) + F)
        e0 = mat[0:3]; e1 = mat[3:6]
        nv = m.u32(ob + 0xc)
        if not (fl & 2):
            for k in range(nv):
                a = VB + (m.u32(ob + 8) + k) * 0x40
                v = Vert()
                X, Y, Z = m.f(a), m.f(a + 4), m.f(a + 8)
                v.ox, v.oy, v.oz = X, Y, Z
                v.flag = m.u8(a + 0x3c)
                v.u, v.v = m.f(a + 0x2c), m.f(a + 0x30)
                v.nx, v.ny, v.nz = m.f(a + 0x20), m.f(a + 0x24), m.f(a + 0x28)
                v.eu, v.ev = m.f(a + 0x34), m.f(a + 0x38)       # stale values if not env mapped
                z = f32(f32(f32(X * r2[0]) + tz) + f32(f32(Y * r2[1]) + f32(Z * r2[2])))
                v.z = z
                v.sx = v.sy = f32(0)
                if fist(z) <= near:
                    v.inside = 4; v.out = 0
                    verts[a] = v
                    continue
                q = f32(F / z)
                inside, out = 1, 0
                x = f32(f32(f32(X * r0[0]) + tx) + f32(f32(Y * r0[1]) + f32(Z * r0[2])))
                v.x = x
                sx = f32(f32(x * q) + cx); v.sx = sx
                b = fbits(sx)
                if not b < clip_x:
                    inside = 0; out = 1 if sbits(sx) < clip_x else 2      # signed compare of the bits (jl)
                y = f32(f32(f32(X * r1[0]) + ty) + f32(f32(Y * r1[1]) + f32(Z * r1[2])))
                v.y = y
                sy = f32(f32(y * q) + cy); v.sy = sy
                b = fbits(sy)
                if not b < clip_y:
                    inside = 0; out += 4 if sbits(sy) < clip_y else 8
                v.inside, v.out = inside, out
                if v.flag & 1:
                    v.eu = f32(f32(f32(v.nx * e0[0]) + f32(f32(v.ny * e0[1]) + f32(v.nz * e0[2]))) + f32(8388608))
                    v.ev = f32(f32(f32(v.nx * e1[0]) + f32(f32(v.ny * e1[1]) + f32(v.nz * e1[2]))) + f32(8388608))
                verts[a] = v
        if fl & 1: continue
        # 0x19f78 face list
        f0 = m.u32(ob); nf = m.u32(ob + 4)
        for k in range(nf):
            fa = FB + (f0 + k) * 0x1c
            A, B, C = (verts[m.u32(fa + 4 * i)] for i in range(3))
            fc = m.u8(fa + 0xc); fd = m.u8(fa + 0xd)
            face = {'addr': fa, 'A': A, 'B': B, 'C': C, 'c': fc, 'type': fd, 'tex': m.u8(fa + 0xe),
                    'env': m.u8(fa + 0xf), 'uvp': m.u32(fa + 0x10), 'clip': 0}
            if fd & 0x80:
                # single-vertex entry (not used by the five scripts)
                zi = fist(A.z)
                if zi - 0x11 < 0: continue
                face['area'] = f32(0)
                lst.append((fist(f32(f32(A.z + A.z) + A.z)), face)); continue
            al = 3 - A.inside - B.inside - C.inside
            if al < 0: continue
            P = f32(f32(A.sx - C.sx) * f32(B.sy - C.sy))
            Q = f32(f32(B.sx - C.sx) * f32(A.sy - C.sy))
            area = f32(P - Q)
            face['area'] = area
            depth = f32(A.z + B.z)
            if not (fc & 0x40) and (fbits(area) & 0x80000000): continue
            depth = f32(depth + C.z)
            if al != 0:
                if al == 3 and (A.out & B.out & C.out): continue
                ah = A.out | B.out | C.out
                if ah:
                    face['clip'] = ah
            lst.append((fist(depth), face))
    return verts, lst


def radix(lst):
    """0x1966e: two counting passes on the low 16 bits of the key; result: key descending (far first)."""
    n = len(lst)
    c0 = [0] * 256; c1 = [0] * 256
    for k, _ in lst:
        c0[k & 255] += 1; c1[~(k >> 8) & 255] += 1
    for i in range(1, 256):
        c0[i] += c0[i - 1]; c1[i] += c1[i - 1]
    tmp = [None] * n
    for e in lst:
        b = e[0] & 255
        c0[b] -= 1; tmp[c0[b]] = e
    out = [None] * n
    for e in tmp:
        b = ~(e[0] >> 8) & 255
        c1[b] -= 1; out[c1[b]] = e
    return out


class Filler:
    def __init__(self, m, page, W, H, pitch=640):
        self.m = m; self.W = W; self.H = H; self.pitch = pitch
        self.page = page          # numpy uint16 array of the 320x200 page (flat, pitch 640 bytes)
        self.T = [np.frombuffer(m.low[a:a + 768 * 4], dtype='<u4') for a in (0x1a479, 0x1b079, 0x1bc79)]
        self.tptr = m.u32(0x18d54)
        self.texcache = {}

    def texture(self, idx, n):
        a = self.m.u32(self.tptr + 4 * idx)
        k = (a, n)
        if k not in self.texcache:
            b, o = self.m.buf(a)
            self.texcache[k] = np.frombuffer(b[o:o + n], dtype=np.uint8)
        return self.texcache[k]

    def draw(self, face):
        m = self.m
        A, B, C = face['A'], face['B'], face['C']
        P = [[A.sx, A.sy, A.u, A.v, A.eu, A.ev], [B.sx, B.sy, B.u, B.v, B.eu, B.ev], [C.sx, C.sy, C.u, C.v, C.eu, C.ev]]
        if face['c'] & 0x80:
            uv = face['uvp']
            for k in range(3):
                P[k][2] = m.f(uv + 8 * k); P[k][3] = m.f(uv + 8 * k + 4)
        a, b, c = P
        clip = face['clip']
        dyA = f32(a[1] - c[1]); dyB = f32(b[1] - c[1])
        N = []
        for k in (2, 3, 4, 5):
            N.append(f32(f32(f32(b[k] - c[k]) * dyA) - f32(f32(a[k] - c[k]) * dyB)))
        inv = f32(f32(65536) / face['area'])
        self.env = self.texture(face['env'], 0x10000)
        self.rgb = self.texture(face['tex'], 0x30000)
        # vertex sort by y
        if clip and (clip & 0xc):
            def key(p):
                k = fbits(p[1])
                k = (k + 0x80000000) & 0xffffffff
                if not (k & 0x80000000): k ^= 0x7fffffff
                return k
        else:
            def key(p): return fbits(p[1])
        # same compare / exchange sequence as the asm (A=edi, B=ecx, C=edx)
        if not key(a) < key(b): a, b = b, a
        if not key(a) < key(c): a, c = c, a
        if key(c) < key(b): b, c = c, b
        self.g = [fist(f32(inv * N[k])) for k in range(4)]     # -du/dx, -dv/dx, -deu/dx, -dev/dx (per pixel)
        yA = fist(f32(a[1] + f32(0.5))); yB = fist(f32(b[1] + f32(0.5))); yC = fist(f32(c[1] + f32(0.5)))
        dyCA = f32(c[1] - a[1])
        if not clip:
            H = self.H
            if not H > yA: return
            if not H > yB: yB = yC = H
            elif not H > yC: yC = H
        self.clipy = bool(clip & 0xc); self.clipx = bool(clip & 3); self.clipped = bool(clip)
        invL = f32(f32(1) / dyCA)
        line = yA * self.pitch
        n1 = yB - yA
        sub_u = [i * (self.g[0] >> 3) for i in range(8)]
        sub_v = [i * (self.g[1] >> 3) for i in range(8)]
        self.sub = (sub_u, sub_v)
        if n1 != 0:
            dxAB = f32(f32(b[0] - a[0]) / f32(b[1] - a[1]))
            sstep = fist(dxAB)
            dy0 = f32(f32(yA) - a[1])
            xs = fist(f32(f32(dy0 * dxAB) + a[0]))
            t0 = f32(dy0 * invL)
            st = []; vals = []
            for k in (0, 2, 3, 4, 5):
                d = f32(c[k] - a[k])
                st.append(fist(f32(d * invL)))
                vals.append(fist(f32(f32(d * t0) + a[k])))
            dxBC = f32(f32(c[0] - b[0]) / f32(c[1] - b[1])) if c[1] != b[1] else None
            r = self.spans(line, n1, xs, sstep, vals, st)
            if r is None: return
            line, vals = r
            n2 = yC - yB
            if n2 == 0: return
            dyB0 = f32(f32(yB) - b[1])
            sstep = fist(dxBC)
            xs = fist(f32(f32(dyB0 * dxBC) + b[0]))
            self.spans(line, n2, xs, sstep, vals, st)
        else:
            n2 = yC - yB
            if n2 == 0: return
            dxBC = f32(f32(c[0] - b[0]) / f32(c[1] - b[1]))
            sstep = fist(dxBC)
            t0 = f32(f32(f32(yA) - a[1]) * invL)
            xs = fist(f32(f32(f32(f32(yB) - b[1]) * dxBC) + b[0]))
            st = []; vals = []
            for k in (0, 2, 3, 4, 5):
                d = f32(c[k] - a[k])
                st.append(fist(f32(d * invL)))
                vals.append(fist(f32(f32(d * t0) + a[k])))
            self.spans(line, n2, xs, sstep, vals, st)

    def spans(self, line, n, xs, sstep, vals, st):
        """0x1e77c / 0x1f07c. vals = [x_long, u, v, eu, ev] on the long edge; returns (line, vals) after n lines
        (None if the clipped version stopped at the bottom)."""
        xl, u, v, eu, ev = vals
        W = self.W; pitch = self.pitch; end = self.H * pitch
        gu, gv, geu, gev = self.g
        sub_u, sub_v = self.sub
        T0, T1, T2 = self.T
        env = self.env; rgb = self.rgb; page = self.page
        if self.clipped:    # 0x1f07c combines the three table entries with OR, 0x1e77c adds them
            def comb(a, b, c): return (int(a) | int(b) | int(c)) & 0xffff
        else:
            def comb(a, b, c): return (int(a) + int(b) + int(c)) & 0xffff
        while n > 0:
            if self.clipy:
                if line >= end:
                    return None
                if line < 0:
                    skip = (-line) // pitch
                    if n > skip:
                        n -= skip
                        xs = i32(xs + skip * sstep); xl = i32(xl + skip * st[0]); u = i32(u + skip * st[1])
                        v = i32(v + skip * st[2]); eu = i32(eu + skip * st[3]); ev = i32(ev + skip * st[4])
                        line = 0
                    else:
                        line += n * pitch
                        xl = i32(xl + n * st[0]); u = i32(u + n * st[1]); v = i32(v + n * st[2])
                        eu = i32(eu + n * st[3]); ev = i32(ev + n * st[4])
                        return line, [xl, u, v, eu, ev]
            s = ((xl & 0xffffffff) >> 13) & 7
            pu = i32(u + sub_u[s]); pv = i32(v + sub_v[s]); peu = eu; pev = ev
            xi = xl >> 16
            cnt = (xs >> 16) - xi
            row = line >> 1
            if cnt > 0:
                draw = True
                if self.clipx:
                    if xi < 0:
                        k = -xi; cnt -= k
                        if cnt <= 0: draw = False
                        pv = i32(pv - k * gv); pu = i32(pu - k * gu); pev = i32(pev - k * gev); peu = i32(peu - k * geu)
                        xi = 0
                    if draw:
                        e = (xs >> 16) - W
                        if (xs >> 16) & 0xffffffff >= W:
                            cnt -= e
                            if cnt <= 0: draw = False
                if draw:
                    for k in range(cnt):
                        e_ = int(env[(((pev >> 16) & 255) << 8) | ((peu >> 16) & 255)])
                        t = 3 * ((((pv >> 16) & 255) << 8) | ((pu >> 16) & 255))
                        page[row + xi + k] = comb(T0[e_ + int(rgb[t])], T1[e_ + int(rgb[t + 1])], T2[e_ + int(rgb[t + 2])])
                        pu = i32(pu - gu); pv = i32(pv - gv); peu = i32(peu - geu); pev = i32(pev - gev)
            elif cnt < 0:
                draw = True
                if self.clipx:
                    if xi < 0: draw = False
                    else:
                        if xi > W:
                            d = xi - W
                            cnt += d
                            if cnt >= 0: draw = False
                            pv = i32(pv + d * gv); pu = i32(pu + d * gu); pev = i32(pev + d * gev); peu = i32(peu + d * geu)
                            xi = W
                        if draw and (xs >> 16) < 0:
                            cnt -= (xs >> 16)
                            if cnt >= 0: draw = False
                if draw:
                    x = xi
                    for k in range(-cnt):
                        peu = i32(peu + geu); pev = i32(pev + gev); pu = i32(pu + gu); pv = i32(pv + gv)
                        x -= 1
                        e_ = int(env[(((pev >> 16) & 255) << 8) | ((peu >> 16) & 255)])
                        t = 3 * ((((pv >> 16) & 255) << 8) | ((pu >> 16) & 255))
                        page[row + x] = comb(T0[e_ + int(rgb[t])], T1[e_ + int(rgb[t + 1])], T2[e_ + int(rgb[t + 2])])
            line += pitch
            xs = i32(xs + sstep); xl = i32(xl + st[0]); u = i32(u + st[1]); v = i32(v + st[2])
            eu = i32(eu + st[3]); ev = i32(ev + st[4])
            n -= 1
        return line, [xl, u, v, eu, ev]


def render(pre):
    m = Mem(pre)
    W, H = m.u32(0x18d64), m.u32(0x18d68)
    off = pre['edi'] - pre['page']
    page = np.frombuffer(pre['pagebytes'], dtype='<u2').copy()
    sub = page[off >> 1:]
    verts, lst = transform(m, W, H)
    order = radix(lst)
    mode = m.u32(0x18d88)
    if mode != 0xff: raise NotImplementedError('clear mode %d' % mode)
    fl = Filler(m, sub, W, H)
    for _, face in order:
        t = face['type']
        if 1 <= t <= 3: fl.draw(face)
        elif t == 0 or t == 255: raise NotImplementedError('type %d' % t)
    return page, verts, lst, order


def to_rgb(p):
    p = p.astype(np.uint32)
    r = (p >> 11) & 31; g = (p >> 5) & 63; b = p & 31
    return np.stack([(r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31], -1).astype(np.uint8)


if __name__ == '__main__':
    d, n = sys.argv[1], int(sys.argv[2])
    pre = pickle.load(open(os.path.join(d, 'r%04d.pre' % n), 'rb'))
    post = pickle.load(open(os.path.join(d, 'r%04d.post' % n), 'rb'))
    page, verts, lst, order = render(pre)
    ref = np.frombuffer(post['pagebytes'], dtype='<u2')
    before = np.frombuffer(pre['pagebytes'], dtype='<u2')
    diff = page != ref
    print('faces in list %d, pixels changed by the renderer %d, model differs at %d pixels'
          % (len(lst), int((ref != before).sum()), int(diff.sum())))
    if '--png' in sys.argv:
        from PIL import Image
        out = sys.argv[sys.argv.index('--png') + 1]
        im = np.concatenate([to_rgb(ref.reshape(200, 320)), to_rgb(page.reshape(200, 320)),
                             (diff.reshape(200, 320)[:, :, None] * np.array([255, 0, 255], np.uint8))], 1)
        Image.fromarray(im).resize((960 * 2, 400), Image.NEAREST).save(out)

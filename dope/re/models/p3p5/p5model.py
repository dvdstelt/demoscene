"""p5 frame model: chequerboard (p4's last frame & 0x0f) + the env-mapped or bump-mapped torus (engine_model, with
p5's env-only filler 2500/2370/2420 and bump filler 1d51/1b30/1c20) + the sliding label.
render_frame(o0, o1, label, labelseg, bumptab, background) -> 64000 bytes."""
import os, struct, pickle
import numpy as np
import engine_model as em

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
IMG = open(os.path.join(RE, 'unpacked', 'p5.bin'), 'rb').read()
SIN = em.sine_table(IMG, 0x100 + 0x12)
MESH = em.Mesh(IMG, 0x1402)
ENVSEG = list(struct.unpack_from('<4H', IMG, 0x100))         # 0010:0000: 03d7 03df 0bd7 0bdf
TEXSEG = list(struct.unpack_from('<4H', IMG, 0x108))         # 0010:0008: 0000 1c96 0000 0000
IMGA = np.frombuffer(IMG + bytes(0x20000), np.uint8)


def background():
    d = pickle.load(open(os.path.join(RE, 'snaps', 'part_p5.snap'), 'rb'))
    return (np.frombuffer(d['vram'], np.uint8)[:64000] & 15).astype(np.uint8)


def bump_table(phase):
    """per-retrace routine 1a70 + 3099: the 256-word offset table at 0x375b for the current phase [0x1546]"""
    sinw = SIN[((phase & 0x7fe) >> 1)]
    dx = (sinw & 0xffff) >> 4
    dx = (dx * 3) & 0xffff
    dx >>= 4
    by = []
    bx = (-8 * dx) & 0xffff
    for k in range(16):
        by.append(bx >> 8)
        bx = (bx + dx) & 0xffff
    return np.array([(by[i] << 8) | by[j] for i in range(16) for j in range(16)], np.int64)


class EnvFiller(em.Filler):
    """2500 + 2370 / 2420: env map only (attribute B), right edge +0x8000, horizontal steps - 1 per word,
    8-bit span counter, clipped spans stop at x = 318"""

    def tri(self, va, vb, vc):
        P = [va, vb, vc]
        S = [p[0] for p in P]
        n_out = sum(1 for s in S if s > 0x1400000)
        if n_out == 3:
            return
        clip = n_out != 0
        def sy(i): return em.s16(P[i][0])
        idx = [0, 1, 2]
        if not sy(idx[0]) < sy(idx[1]): idx[0], idx[1] = idx[1], idx[0]
        if not sy(idx[1]) < sy(idx[2]): idx[1], idx[2] = idx[2], idx[1]
        if not sy(idx[0]) < sy(idx[1]): idx[0], idx[1] = idx[1], idx[0]
        a, b, c = (P[i] for i in idx)
        if em.s16(a[0]) > 0xc9 or em.s16(c[0]) < 1:
            return
        if em.s16(a[0]) == em.s16(b[0]) and em.s32(a[0]) >= em.s32(b[0]):
            a, b = b, a
        s16, s32, tdiv, M32 = em.s16, em.s32, em.tdiv, em.M32
        da, db, dc = s32(a[0]), s32(b[0]), s32(c[0])
        Ba, Bb, Bc = a[3], b[3], c[3]
        ecx = Ba
        flat = True
        dy_ab = s16(db - da)
        s_ab = getattr(self, 's_ab', 0)
        if dy_ab != 0:
            s_ab = tdiv(s32(db - da), dy_ab); flat = False
        self.s_ab = s_ab
        dy_ac = s16(dc - da)
        if dy_ac == 0:
            return
        s_ac = tdiv(s32(dc - da), dy_ac)
        dy_bc = s16(dc - db)
        s_bc = tdiv(s32(dc - db), dy_bc) if dy_bc != 0 else getattr(self, 's_bc', 0)
        self.s_bc = s_bc
        XL = da & M32; XR = (da + 0x8000) & M32
        def w(v, k): return s16(v >> (16 * k))
        def step16(x, y, n): return tdiv(s16(y - x), n) & 0xffff
        st = getattr(self, 'st', 0)
        if s_ab == s_ac or dy_ab == 0:
            ebp = s32(db - da)
            if ebp == 0:
                return
        else:
            ebp = s32((s_ab - s_ac) * (dy_ab & 0xffff))
            if s_ab >= s_ac:
                st = (step16(w(Ba, 1), w(Bc, 1), dy_ac) << 16) | step16(w(Ba, 0), w(Bc, 0), dy_ac)
            else:
                st = (step16(w(Ba, 1), w(Bb, 1), dy_ab) << 16) | step16(w(Ba, 0), w(Bb, 0), dy_ab)
        def hgrad(xa, xb, xc):
            q = tdiv(s16(xa - xc) * dy_ab, dy_ac)
            q = s16(q - xa + xb)
            return (tdiv(s32(q << 16), ebp) - 1) & 0xffff
        self.hB = (hgrad(w(Ba, 1), w(Bb, 1), w(Bc, 1)) << 16) | hgrad(w(Ba, 0), w(Bb, 0), w(Bc, 0))
        ya, yb, yc = s16(da), s16(db), s16(dc)
        def wordstep(v, s, k):          # per-word k*step without cross-word carry (rol trick)
            lo = (v + ((k * s16(s)) & 0xffff)) & 0xffff
            hi = ((v >> 16) + ((k * s16(s >> 16)) & 0xffff)) & 0xffff
            return (hi << 16) | lo
        if yb <= 1 or flat:
            t = dy_ab & 0xffff
            edi = s32(s_ac * t)
            ecx = (ecx + t * st) & M32
            edi = s32(edi + XL); esi = db
            if s32(edi) >= s32(esi):
                edi, esi = esi, edi
            XL = edi & M32; XR = (esi + 0x7fff) & M32
            row = max(ya, 1)
            XL = (XL + (row * 320 << 16)) & M32; XR = (XR + (row * 320 << 16)) & M32
        else:
            sl, sr = (s_ab, s_ac) if s_ab < s_ac else (s_ac, s_ab)
            yb = min(yb, 0xc9)
            if ya < 1:
                k = 1 - ya
                ecx = wordstep(ecx, st, k)
                XL = (XL + k * sl) & M32; XR = (XR + k * sr) & M32
                ya = 1
            n = yb - ya
            if n == 0:
                return
            XL = (XL + (ya * 320 << 16)) & M32; XR = (XR + (ya * 320 << 16)) & M32
            XL, XR, ecx = self.rows(ya, n, sl, sr, XL, XR, ecx, st, clip)
        self.st = st
        if dy_bc == 0:
            return
        st = (step16(w(ecx, 1), w(Bc, 1), dy_bc) << 16) | step16(w(ecx, 0), w(Bc, 0), dy_bc)
        self.st = st
        sl, sr = (s_ac, s_bc) if s_ac > s_bc else (s_bc, s_ac)
        yc = min(yc, 0xc9)
        if yb < 1:
            k = 1 - yb
            ecx = wordstep(ecx, st, k)
            XL = (XL + k * sl) & M32; XR = (XR + k * sr) & M32
            yb = 1
        n = yc - yb
        if n == 0:
            return
        self.rows(yb, n, sl, sr, XL, XR, ecx, st, clip)

    def rows(self, y0, n, sl, sr, XL, XR, ecx, st, clip):
        M32 = em.M32
        sl2 = (sl + 0x1400000) & M32; sr2 = (sr + 0x1400000) & M32
        base = y0 * 320
        hB = self.hB
        for r in range(n):
            left = XL >> 16; right = XR >> 16
            if not clip:
                cnt = (right - left) & 0xffff
                if cnt != 0 and not cnt & 0x8000:
                    cnt = (cnt & 0xff) or 256
                    self.span(left, cnt, 0, ecx, 0, hB)
            else:
                B0 = ecx
                l = left
                ok = True
                if l < base:
                    B0 = (B0 - (l - base) * hB) & M32
                    l = base
                elif l >= base + 320:
                    ok = False
                if ok:
                    rr = right
                    if rr < base: rr = base
                    if rr >= base + 320: rr = base + 319
                    cnt = rr - l
                    if cnt > 0:
                        cnt = (cnt & 0xff) or 256
                        self.span(l, cnt, 0, B0, 0, hB)
                base += 320
            ebp = ((XR & 0xffff) << 16) | (XL >> 16)
            edx = ((XL & 0xffff) << 16) | (XR >> 16)
            esi = ((sr2 & 0xffff) << 16) | (sl2 >> 16)
            edi = ((sl2 & 0xffff) << 16) | (sr2 >> 16)
            t = ebp + esi; cf = t >> 32; ebp = t & M32
            t = edx + edi + cf; cf = t >> 32; edx = t & M32
            ebp = (ebp + cf) & M32
            XL = ((ebp & 0xffff) << 16) | (edx >> 16)
            XR = ((edx & 0xffff) << 16) | (ebp >> 16)
            ecx = (ecx + st) & M32
        return XL, XR, ecx


def obj_fields(b):
    w = lambda o: struct.unpack_from('<h', b, o)[0]
    d = lambda o: struct.unpack_from('<i', b, o)[0]
    return dict(seg=struct.unpack_from('<H', b, 0)[0], zoom=w(4), pos=(d(6), d(10), d(14)), ang=(w(0x1e), w(0x20), w(0x22)),
                mat=struct.unpack_from('<H', b, 0x2a)[0], cull=w(0x2c), pivot=(d(0x2e), d(0x32), d(0x36)))


def render_object(fb, cam, obj, bumptab):
    ax, ay, az = ((obj['ang'][i] + cam['ang'][i]) & 0xffff for i in range(3))
    M = em.matrix(SIN, ax, ay, az)
    pos = tuple(em.s32(obj['pos'][i] + cam['pos'][i]) for i in range(3))
    verts = em.transform(MESH, M, pos, obj['pivot'], obj['zoom'], 'uv')
    faces = em.visible_faces(MESH, verts, obj['cull'])
    madd_lo, madd_hi = obj['mat'] & 0xff, obj['mat'] >> 8
    envf = None; bumpf = {}
    for f in faces:
        mat, ia, ib, ic = (int(v) for v in MESH.F[f])
        env = ENVSEG[((mat & 0xff) + madd_lo) & 3] * 16
        tex = TEXSEG[((mat >> 8) + madd_hi) & 3] * 16
        if tex == 0:
            if envf is None:
                def pixel(A, B, n, env=env):
                    bx = (((B >> np.uint64(8)) & np.uint64(0xff)) << np.uint64(8)) | (B >> np.uint64(24))
                    return IMGA[env + bx.astype(np.int64)]
                envf = EnvFiller(IMG, fb, pixel)
            envf.tri(verts[ia >> 2], verts[ib >> 2], verts[ic >> 2])
        else:
            def pixel(A, B, n, env=env, tex=tex):
                lo = A & np.uint64(0xffff); hi = A >> np.uint64(16)
                row = (((lo >> np.uint64(8)) & np.uint64(0x1f)) << np.uint64(2)) | ((hi >> np.uint64(14)) & np.uint64(3))
                col = (hi >> np.uint64(6)) & np.uint64(0x7f)
                t = IMGA[tex + (row * np.uint64(256) + col).astype(np.int64)]
                si = bumptab[t]
                bx = (((B >> np.uint64(8)) & np.uint64(0xff)) << np.uint64(8)) | (B >> np.uint64(24))
                return IMGA[env + ((bx.astype(np.int64) + si) & 0xffff)]
            key = (env, tex)
            if key not in bumpf:
                bumpf[key] = em.Filler(IMG, fb, pixel)
            bumpf[key].tri(verts[ia >> 2], verts[ib >> 2], verts[ic >> 2])


def render_frame(o0, o1, label, labelseg, bumptab, bg):
    fb = np.zeros(65536, np.uint8)
    fb[0x140:0x140 + 64000] = bg
    render_object(fb, obj_fields(bytes(o0)), obj_fields(bytes(o1)), bumptab)
    if label >= 0:
        lab = np.frombuffer(IMG[labelseg * 16 + 0x30a:labelseg * 16 + 0x30a + 70 * 44], np.uint8).reshape(44, 70)
        a = label; x0 = 0
        if a > 0x46:
            x0 = a - 0x46; a = 0x46
        for r in range(44):
            o = 0xb40 + x0 + r * 320
            fb[o:o + a] = lab[r, 70 - a:70]
    return fb[0x140:0x140 + 64000].copy()

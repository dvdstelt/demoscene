"""Python model of the object script interpreter 0x14394 and its generators (sphere 0x1488d, cylinder 0x14b8a,
torus 0x14e7c, face halving 0x15174, translate / twist / rotate 0x151e9..0x153a5, duplicate 0x152a0, merge
0x15276, normals 0x1a089, texture-seam fix 0x14651). Builds the objects of the five scripts and compares them with
the engine buffers in a snapshot.

Precision: the generators run with the x87 at 24-bit precision (float32 per operation; fsin/fcos/fpatan results
are extended, modelled as float64 rounded by the following operation); the normal pass 0x1a089 starts with fninit
(64-bit precision, modelled as float64) and stores float32.

usage: python3 model_objects.py            compare every script with the snapshot of its part
"""
import os, sys, math, struct
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep
from scripts import parse, SCRIPTS

f32 = np.float32
f64 = np.float64
PI = f64(math.pi)
K_U = f32(rep.rdf(0x15718))      # 128/pi
K_V = f32(rep.rdf(0x15714))      # 256/pi
C65536 = f32(65536)
KVEC = [f32(rep.rdf(0x15790 + 4 * i)) for i in range(3)]   # helper vector for the cylinder's basis


def sin(x): return f64(math.sin(f64(x)))
def cos(x): return f64(math.cos(f64(x)))
def mul(a, b): return f32(f64(a) * f64(b))      # one x87 multiply rounded to 24 bits (ext operands allowed)
def add(a, b): return f32(f64(a) + f64(b))
def sub(a, b): return f32(f64(a) - f64(b))
def div(a, b): return f32(f64(a) / f64(b))


class Vertex:
    __slots__ = ('p', 'n', 'u', 'v', 'flag')

    def __init__(self, p, u, v, flag):
        self.p = list(p); self.n = [f32(0)] * 3; self.u = u; self.v = v; self.flag = flag


class Face:
    __slots__ = ('a', 'b', 'c', 'fc', 'type', 'tex', 'env', 'uv')

    def __init__(self, a, b, c, ftype, tex):
        self.a, self.b, self.c = a, b, c; self.fc = 0; self.type = ftype; self.tex = tex; self.env = 0; self.uv = None


class Scene:
    def __init__(self):
        self.V = []; self.F = []; self.objs = []        # obj = [first_face, nfaces, first_vertex, nverts]
        self.tex = 1; self.ftype = 3; self.us = f32(1); self.vs = f32(1)

    def new_object(self, nv, nf):
        self.objs.append([len(self.F), nf, len(self.V), nv])
        return len(self.V), len(self.F)

    def face(self, a, b, c, seam):
        f = Face(a, b, c, self.ftype, self.tex)
        self.F.append(f)
        if seam: self.seam_fix(f)

    def seam_fix(self, f):
        """0x14651: texture coordinates mod 2^24 (fprem keeps the sign); where two corners of the face differ by
        >= 2^23 (half the texture) the second one is moved by +-2^24; if anything moved, the face gets its own
        (u, v) table and flag 0x80 in byte +0xc"""
        W = f64(16777216.0)
        uv = []
        for vi in (f.a, f.b, f.c):
            V = self.V[vi]
            uv += [f32(math.fmod(f64(V.u), W)), f32(math.fmod(f64(V.v), W))]
        moved = False
        for i, j in ((0, 2), (2, 4), (4, 0), (1, 3), (3, 5), (5, 1)):
            d = sub(uv[i], uv[j])
            if abs(int(np.rint(f64(d)))) >= 0x800000:
                uv[j] = add(uv[j], -W if d < 0 else W)
                moved = True
        if moved:
            f.fc |= 0x80; f.uv = uv

    # --- generators -------------------------------------------------------------------------------------------
    def sphere(self, M, S, centre, R, A1, F1, A2, F2):
        v0, _ = self.new_object((M + 1) * S, 2 * (M - 1) * S)
        dphi = div(f32(PI + PI), S); dth = div(PI, M)
        th = f32(0)
        for j in range(M + 1):
            ph = f32(0)
            for i in range(S):
                a = mul(sin(mul(ph, F1)), A1)
                b = mul(sin(mul(th, F2)), A2)
                r = mul(add(mul(a, b), 1), R)
                x = add(mul(mul(sin(th), cos(ph)), r), centre[0])
                y = add(mul(cos(th), r), centre[1])
                z = add(mul(mul(sin(th), sin(ph)), r), centre[2])
                u = mul(mul(mul(ph, K_U), C65536), self.us)
                v = mul(mul(mul(th, K_V), C65536), self.vs)
                self.V.append(Vertex((x, y, z), u, v, self.ftype >> 1))
                ph = add(ph, dphi)
            th = add(th, dth)
        for rb in range(0, S * M, S):
            nb = rb + S
            for i in range(S):
                i1 = (i + 1) % S
                if nb < S * M: self.face(v0 + rb + i, v0 + nb + i1, v0 + nb + i, i1 == 0)
                if rb != 0: self.face(v0 + rb + i, v0 + rb + i1, v0 + nb + i1, i1 == 0)

    def cylinder(self, N, M, C, A, R0, R1):
        v0, _ = self.new_object((M + 1) * N, 2 * M * N)
        U = normalize(cross(A, KVEC)); V = normalize(cross(U, A))
        dphi = div(f32(PI + PI), N); ds = div(1, M)
        s = f32(0)
        for j in range(M + 1):
            ph = f32(0)
            r = add(mul(sub(R1, R0), s), R0)
            for i in range(N):
                p = [add(add(C[k], mul(s, A[k])), mul(add(mul(cos(ph), U[k]), mul(sin(ph), V[k])), r)) for k in range(3)]
                u = mul(mul(mul(ph, K_U), C65536), self.us)
                v = mul(mul(mul(s, f32(256)), C65536), self.vs)
                self.V.append(Vertex(p, u, v, self.ftype >> 1))
                ph = add(ph, dphi)
            s = add(s, ds)
        for rb in range(0, N * M, N):
            nb = rb + N
            for i in range(N):
                i1 = (i + 1) % N
                self.face(v0 + rb + i, v0 + nb + i1, v0 + rb + i1, i1 == 0)
                self.face(v0 + rb + i, v0 + nb + i, v0 + nb + i1, i1 == 0)

    def torus(self, N, M, R, r, A1, F1, A2, F2, tw):
        v0, _ = self.new_object(N * M, 2 * N * M)
        twopi = f32(PI + PI)
        dphi = div(twopi, N); dth = div(twopi, M)
        th = f32(0)
        for j in range(M):
            ph = f32(0)
            for i in range(N):
                a = add(ph, mul(th, tw))
                rr = add(add(r, mul(sin(mul(a, F1)), A1)), mul(sin(mul(th, F2)), A2))
                rad = add(mul(cos(a), rr), R)
                p = (mul(cos(th), rad), mul(sin(a), rr), mul(sin(th), rad))
                u = mul(mul(mul(ph, K_U), C65536), self.us)
                v = mul(mul(mul(th, K_U), C65536), self.vs)
                self.V.append(Vertex(p, u, v, self.ftype >> 1))
                ph = add(ph, dphi)
            th = add(th, dth)
        NM = N * M
        for j in range(M):
            rb = j * N; nb = (rb + N) % NM
            for i in range(N):
                i1 = (i + 1) % N
                seam = (i1 & nb) == 0          # the asm tests "i1 AND next row base" (a cheap i1 == 0 || nb == 0)
                self.face(v0 + rb + i, v0 + rb + i1, v0 + nb + i1, seam)
                self.face(v0 + rb + i, v0 + nb + i1, v0 + nb + i, seam)

    def halve(self):
        o = self.objs[-1]
        f0, nf = o[0], o[1]
        keep = [self.F[f0 + k] for k in range(nf) if (k >> 1) % 2 == 0][:nf >> 1]
        del self.F[f0:f0 + nf]
        self.F[f0:f0] = keep
        o[1] = nf >> 1

    def transform(self, o, M):
        f0, nf, v0, nv = self.objs[o]
        for k in range(nv):
            V = self.V[v0 + k]
            p = V.p
            V.p = [add(mul(p[0], M[r][0]), add(mul(p[1], M[r][1]), mul(p[2], M[r][2]))) for r in range(3)]

    def translate(self, o, d):
        f0, nf, v0, nv = self.objs[o]
        for k in range(nv):
            V = self.V[v0 + k]
            V.p = [add(V.p[i], d[i]) for i in range(3)]

    def twist(self, o, k):
        f0, nf, v0, nv = self.objs[o]
        for i in range(nv):
            V = self.V[v0 + i]
            a = mul(V.p[1], k)
            c = f32(cos(a)); s = f32(sin(a))
            M = [[c, -s, f32(0)], [s, c, f32(0)], [f32(0), f32(0), f32(1)]]
            p = V.p
            V.p = [add(mul(p[0], M[r][0]), add(mul(p[1], M[r][1]), mul(p[2], M[r][2]))) for r in range(3)]

    def rot(self, o, axis, k):
        c = f32(cos(k)); s = f32(sin(k)); z = f32(0); one = f32(1)
        M = {'x': [[one, z, z], [z, c, -s], [z, s, c]],
             'y': [[c, z, s], [z, one, z], [-s, z, c]],
             'z': [[c, -s, z], [s, c, z], [z, z, one]]}[axis]
        self.transform(o, M)

    def duplicate(self, o):
        f0, nf, v0, nv = self.objs[o]
        nv0, nf0 = self.new_object(nv, nf)
        for k in range(nv):
            V = self.V[v0 + k]
            W = Vertex(V.p, V.u, V.v, V.flag); W.n = list(V.n)
            self.V.append(W)
        d = nv0 - v0
        for k in range(nf):
            F = self.F[f0 + k]
            G = Face(F.a + d, F.b + d, F.c + d, F.type, self.tex)
            G.fc = F.fc; G.env = F.env; G.uv = F.uv
            self.F.append(G)

    def merge(self):
        if len(self.objs) >= 2:
            a, b = self.objs[-2], self.objs[-1]
            a[3] += b[3]; a[1] += b[1]
            self.objs.pop()

    def normals(self):
        for f0, nf, v0, nv in self.objs:
            if nf == 0 or nv == 0: continue
            for k in range(nf):
                F = self.F[f0 + k]
                A, B, C = self.V[F.a], self.V[F.b], self.V[F.c]
                d1 = [f32(f64(B.p[i]) - f64(A.p[i])) for i in range(3)]
                d2 = [f32(f64(C.p[i]) - f64(A.p[i])) for i in range(3)]
                n = [f64(d1[1]) * f64(d2[2]) - f64(d1[2]) * f64(d2[1]),
                     f64(d1[2]) * f64(d2[0]) - f64(d1[0]) * f64(d2[2]),
                     f64(d1[0]) * f64(d2[1]) - f64(d1[1]) * f64(d2[0])]
                for V in (A, B, C):
                    V.n = [f32(f64(V.n[i]) + n[i]) for i in range(3)]
            for k in range(nv):
                V = self.V[v0 + k]
                L = math.sqrt((f64(V.n[0]) ** 2 + f64(V.n[1]) ** 2) + f64(V.n[2]) ** 2)
                s = f64(8388608.0) / L if L else float('inf')
                V.n = [f32(f64(V.n[i]) * s) for i in range(3)]


def cross(a, b):
    """0x1542e with ebx = a, esi = b: a x b"""
    return [sub(mul(a[1], b[2]), mul(a[2], b[1])), sub(mul(a[2], b[0]), mul(a[0], b[2])),
            sub(mul(a[0], b[1]), mul(a[1], b[0]))]


def normalize(v):
    s = div(1, f32(math.sqrt(add(mul(v[0], v[0]), add(mul(v[1], v[1]), mul(v[2], v[2]))))))
    return [mul(s, x) for x in v]


def build(scene):
    S = Scene()
    ops, _ = parse(SCRIPTS[scene])
    for p, op, name, d in ops[1:]:
        if name == 'texture': S.tex = d['index']
        elif name == 'uvscale': S.us = f32(d['u']); S.vs = f32(d['v'])
        elif name == 'facetype': S.ftype = d['type']
        elif name == 'sphere':
            S.sphere(d['M'], d['S'], [f32(x) for x in d['centre']], f32(d['R']), f32(d['A_phi']), f32(d['F_phi']),
                     f32(d['A_theta']), f32(d['F_theta']))
        elif name == 'cylinder':
            S.cylinder(d['N'], d['M'], [f32(x) for x in d['start']], [f32(x) for x in d['axis']], f32(d['R0']), f32(d['R1']))
        elif name == 'torus':
            S.torus(d['N'], d['M'], f32(d['R']), f32(d['r']), f32(d['A1']), f32(d['F1']), f32(d['A2']), f32(d['F2']),
                    f32(d['twist']))
        elif name == 'halve_faces': S.halve()
        elif name == 'translate': S.translate(d['obj'], [f32(x) for x in d['d']])
        elif name == 'twist_z_by_y': S.twist(d['obj'], f32(d['k']))
        elif name == 'rot_x': S.rot(d['obj'], 'x', f32(d['k']))
        elif name == 'rot_y': S.rot(d['obj'], 'y', f32(d['k']))
        elif name == 'rot_z': S.rot(d['obj'], 'z', f32(d['k']))
        elif name == 'duplicate': S.duplicate(d['obj'])
        elif name == 'merge_last_two': S.merge()
        elif name == 'normals': S.normals()
        elif name == 'end': break
    return S


def ulps(a, b):
    ia = np.frombuffer(np.float32(a).tobytes(), np.int32)[0]; ib = np.frombuffer(np.float32(b).tobytes(), np.int32)[0]
    if (ia < 0) != (ib < 0): return abs(float(a) - float(b)) and 1 << 30
    return abs(int(ia) - int(ib))


def compare(scene, snapname):
    from snap import Snap
    s = Snap(snapname)
    S = build(scene)
    VB = s.u32(0x18d4c); FB = s.u32(0x18d48); OB = s.u32(0x18d50)
    print(f'scene {scene} vs snapshot t{snapname}: model {len(S.objs)} objects {len(S.V)} vertices {len(S.F)} faces; '
          f'snapshot {s.u32(0x18d58)} objects {s.u32(0x18d5c)} vertices {s.u32(0x18d60)} faces')
    for o, (f0, nf, v0, nv) in enumerate(S.objs):
        so = [s.u32(OB + o * 0x50 + 4 * i) for i in range(4)]
        worst = {'p': 0, 'n': 0, 'uv': 0}
        exact = {'p': 0, 'n': 0, 'uv': 0}
        for k in range(nv):
            V = S.V[v0 + k]; a = VB + (v0 + k) * 0x40
            for key, mv, off in (('p', V.p, 0), ('n', V.n, 0x20), ('uv', [V.u, V.v], 0x2c)):
                d = max(ulps(mv[i], s.f32(a + off + 4 * i)) for i in range(len(mv)))
                worst[key] = max(worst[key], d); exact[key] += d == 0
            assert s.u8(a + 0x3c) == V.flag
        fbad = 0; uvbad = 0; nflag = 0
        for k in range(nf):
            F = S.F[f0 + k]; a = FB + (f0 + k) * 0x1c
            ptr = [(s.u32(a + 4 * i) - VB) // 0x40 for i in range(3)]
            if ptr != [F.a, F.b, F.c] or s.u8(a + 0xc) != F.fc or s.u8(a + 0xd) != F.type or s.u8(a + 0xe) != F.tex:
                fbad += 1
            if F.fc & 0x80:
                nflag += 1
                up = s.u32(a + 0x10)
                if max(ulps(F.uv[i], s.f32(up + 4 * i)) for i in range(6)) > 0: uvbad += 1
        print(f'  obj {o}: header model {[f0, nf, v0, nv]} snapshot {so}; vertices exact (pos/normal/uv) '
              f'{exact["p"]}/{exact["n"]}/{exact["uv"]} of {nv}, worst ulps {worst}; faces differing {fbad} of {nf}; '
              f'seam faces {nflag} (uv table not exact: {uvbad})')


if __name__ == '__main__':
    for scene, snapname in ((0, '1365'), (1, '3285'), (2, '2515'), (3, '4050'), (4, '7506')):
        compare(scene, snapname)

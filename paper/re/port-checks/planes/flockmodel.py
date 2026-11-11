# Exact model of the flock part (0x33d0 / 0x342f / 0x3618 / 0x37af) from the exe, for checking the frame schedule.
import json, math, struct, sys
import numpy as np
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import rep
SEG = rep.SEG
RE = '/home/dennis/Downloads/acme_303/paper/re/'
PROBE = '/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-planes/runs/flock.probe.json'


def s32(x):
    x &= 0xffffffff
    return x - (1 << 32) if x >> 31 else x


def sintab():
    t = [int(round(65536 * math.sin(2 * math.pi * k / 1024))) for k in range(1024)]
    t[661] = -51912; t[905] = -43712
    return t


SIN = sintab()
def S(a): return SIN[a & 1023]
def C(a): return SIN[(a + 256) & 1023]
def rotz(a): s, c = S(a), C(a); return [c, -s, 0, s, c, 0, 0, 0, 65536]
def rotxy(a, b):
    cb, sb, ca, sa = C(b), S(b), C(a), S(a)
    return [cb, 0, -sb, (-sb * sa) >> 16, ca, -((cb * sa) >> 16), (sb * ca) >> 16, sa, (cb * ca) >> 16]
def mul(A, B):
    out = []
    for r in range(3):
        for c in range(3):
            acc = 0
            for k in range(3): acc += s32((A[3 * r + k] * B[3 * k + c]) >> 16)
            out.append(s32(acc))
    return out


class Rng:
    def __init__(s):
        s.T = [struct.unpack_from('<I', SEG, 0x9a0 + 4 * k)[0] for k in range(64)]
        s.i = SEG[0xaa0] & 63; s.j = SEG[0xaa1] & 63; s.n = 0
    def __call__(s):
        x = (s.T[s.i] + s.T[s.j]) & 0xffffffff; s.T[s.j] = x; s.i = (s.i + 1) & 63; s.j = (s.j + 1) & 63; s.n += 1
        return x
    def skip(s, n):
        for _ in range(n): s()


class Flock:
    def __init__(s, rng):
        s.rng = rng
        c, a, b = s.rpm3(0x3f); s.tgt = [c, a, b]
        s.planes = []
        for n in range(32):
            c, a, b = s.rpm3(0x3ff)
            s.planes.append(dict(p=[c << 11, a << 11, b << 10], yaw=0, pitch=0, spd=0,
                                 m=[65536, 0, 0, 0, 65536, 0, 0, 0, 65536], wp=[0, 0, 0]))
        s.vel = [0, 0, 0]
        s.sway = [643653, -5643454, 6434]  # 33c4, 33c8, 33cc
        s.V = rotxy(0x100, 0); s.cam = [-10000, 0, 0]
    def rpm(s, mask):
        x = s.rng(); v = x & mask
        return -v if x & 0x10000 else v
    def rpm3(s, mask):
        a = s.rpm(mask); b = s.rpm(mask); c = s.rpm(mask); return c, a, b
    def steer(s):
        c, a, b = s.rpm3(1)
        v = [s.vel[0] + c, s.vel[1] + a, s.vel[2] + b]
        if (s.rng() & 0x3f) == 0:
            c, a, b = s.rpm3(7); v = [c, a, b]
        v = [max(-7, min(7, x)) for x in v]
        lim = (450, 450, 14)
        n = [s.tgt[k] + v[k] for k in range(3)]
        for k in range(3):
            if n[k] < -lim[k] or n[k] > lim[k]:
                v[k] = -v[k]; n[k] += v[k]
        s.tgt, s.vel = n, v
        T = [x << 4 for x in s.tgt]
        for q in s.planes:
            d = [(T[k] - q['wp'][k]) >> 4 for k in range(3)]
            m = q['m']
            q['yaw'] = -(s32((s.rpm(0x3f) << 16) + m[0] * d[0] + m[3] * d[1] + m[6] * d[2]) >> 20)
            q['pitch'] = -(s32((s.rpm(0x3f) << 16) + m[1] * d[0] + m[4] * d[1] + m[7] * d[2]) >> 20)
            q['spd'] += s.rpm(3) + 12
        s.sway[0] += 0x64; s.sway[2] += 0x8f; s.sway[1] -= 0x48
    def move(s):
        """0x3618: returns the camera used for drawing this frame (V, cam before the update)"""
        Vd, camd = list(s.V), list(s.cam)
        Sm = [0, 0, 0]
        for q in s.planes:
            q['m'] = mul(q['m'], rotxy(q['pitch'], q['yaw']))
            sp = q['spd']; q['spd'] = 0
            for k in range(3):
                q['p'][k] = s32(q['p'][k] + q['m'][3 * k + 2] * sp); q['wp'][k] = q['p'][k] >> 12; Sm[k] += q['wp'][k]
        Sm = [x >> 5 for x in Sm]; Sm[0] -= s.cam[0] + s.cam[1] + s.cam[2]
        s.cam = [S(s.sway[0] >> 4) >> 5, S(s.sway[2] >> 4) >> 6, S(s.sway[1] >> 4) >> 6]
        c = [x >> 8 for x in Sm]
        b = s32(s.V[0] * c[0] + s.V[1] * c[1] + s.V[2] * c[2]) >> 15
        a = s32(s.V[3] * c[0] + s.V[4] * c[1] + s.V[5] * c[2]) >> 15
        s.V = mul(rotz(2), mul(rotxy(a, b), s.V))
        return Vd, camd


def plane_model():
    a = 0x1878b
    nv, nf = SEG[a], SEG[a + 1]; p = a + 2
    V = []
    for i in range(nv):
        V.append([(x - 256 if x > 127 else x) * 8 for x in SEG[p:p + 3]]); p += 3
    F = []
    for i in range(nf):
        F.append(list(SEG[p:p + 4])); p += 4
    return V, F


def frames(nframes):
    """list of frame states: frame n (1-based) = (planes [(m, wp)], V, cam) as drawn by loop iteration n"""
    rng = Rng(); rng.skip(179785)
    p = json.load(open(PROBE))
    st = p['rng388b']
    assert rng.T == list(st[:64]) and (rng.i, rng.j) == (st[64], st[65]), 'rng mismatch'
    fl = Flock(rng)
    fl.steer()
    out = [None]
    for n in range(1, nframes + 1):
        fl.steer()
        Vd, camd = fl.move()
        out.append(([(list(q['m']), list(q['wp'])) for q in fl.planes], Vd, camd, list(fl.tgt)))
    return out, rng.n


def mask(state, PV, PF):
    from PIL import Image, ImageDraw
    planes, V, cam, _ = state
    im = Image.new('L', (320, 200), 0)
    d = ImageDraw.Draw(im)
    for m, wp in planes:
        W = mul(V, m)
        T = [sum(V[3 * r + k] * (wp[k] - cam[k]) for k in range(3)) for r in range(3)]
        scr = []; zs = []
        for v in PV:
            X = [W[3 * r] * v[0] + W[3 * r + 1] * v[1] + W[3 * r + 2] * v[2] + T[r] for r in range(3)]
            zs.append(X[2] / 65536.0)
            if X[2] / 65536.0 > 400:
                scr.append((160 + 224 * X[0] / X[2], 100 + 224 * X[1] / X[2]))
            else:
                scr.append(None)
        for f in PF:
            if any(scr[i] is None for i in f[:3]): continue
            p0, p1, p2 = (scr[i] for i in f[:3])
            if (p2[1] - p0[1]) * (p1[0] - p0[0]) > (p2[0] - p0[0]) * (p1[1] - p0[1]):
                d.polygon([(math.floor(q[0]), math.floor(q[1])) for q in (p0, p1, p2)], fill=255)
    return np.asarray(im) > 0

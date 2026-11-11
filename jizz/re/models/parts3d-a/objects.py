"""Check the object generators (0x1488d op 0 'sphere', 0x14e7c op 14 'torus', 0x15174 op 15 'every other quad')
against the vertex / face arrays in the snapshots (t1365: part 0x17003, t2515: part 0x169e7)."""
import math, struct
import numpy as np
from snap import mem
import rep
F = np.float32
K2PI = F(rep.rdf(0x15718))   # 256/(2pi), float at 0x15718
KPI = F(rep.rdf(0x15714))    # 256/pi,    float at 0x15714

def sphere(n, R0, c=(0, 0, 0), A1=0, f1=0, A2=0, f2=0, sx=2, sy=2):
    N, M = 2 * n, n
    du, dv = F(2 * math.pi) / F(N), F(math.pi) / F(M)
    out = []
    v = F(0)
    for j in range(M + 1):
        u = F(0)
        for i in range(N):
            R = F(F(F(F(math.sin(u * f1)) * A1) * F(F(math.sin(F(v * f2))) * A2) + 1) * R0)
            x = F(F(F(math.sin(v)) * F(math.cos(u))) * R) + c[0]
            y = F(F(math.cos(v)) * R) + c[1]
            z = F(F(F(math.sin(v)) * F(math.sin(u))) * R) + c[2]
            tu = F(F(F(u * K2PI) * 65536) * sx)
            tv = F(F(F(v * KPI) * 65536) * sy)
            out.append((x, y, z, tu, tv))
            u = F(u + du)
        v = F(v + dv)
    return np.array(out, np.float64)

def torus(N, M, R0, r0, A1, f1, A2, f2, g, sx, sy):
    du, dv = F(2 * math.pi) / F(N), F(2 * math.pi) / F(M)
    out = []
    v = F(0)
    for j in range(M):
        u = F(0)
        for i in range(N):
            a = F(u + F(v * g))
            rr = F(F(F(r0) + F(F(math.sin(F(a * f1))) * A1)) + F(F(math.sin(F(v * f2))) * A2))
            RR = F(F(rr * F(math.cos(a))) + R0)
            out.append((F(F(math.cos(v)) * RR), F(F(math.sin(a)) * rr), F(F(math.sin(v)) * RR),
                        F(F(F(u * K2PI) * 65536) * sx), F(F(F(v * K2PI) * 65536) * sy)))
            u = F(u + du)
        v = F(v + dv)
    return np.array(out, np.float64)

def objects(m):
    ot, vt, ft = m.u32(0x18d50), m.u32(0x18d4c), m.u32(0x18d48)
    n = m.u32(0x18d58)
    res = []
    for k in range(n):
        o = ot + 0x50 * k
        f0, fc, v0, vc, fl = m.u32(o), m.u32(o + 4), m.u32(o + 8), m.u32(o + 0xc), m.u32(o + 0x4c)
        V = np.array([struct.unpack_from('<3f', m.b, vt + 0x40 * (v0 + i)) +
                      struct.unpack_from('<2f', m.b, vt + 0x40 * (v0 + i) + 0x2c) for i in range(vc)])
        faces = []
        for i in range(fc):
            fa = ft + 0x1c * (f0 + i)
            p = struct.unpack_from('<3I', m.b, fa)
            faces.append(tuple((q - vt) // 0x40 - v0 for q in p) + tuple(m.b[fa + 0xc:fa + 0x10]))
        res.append(dict(f0=f0, fc=fc, v0=v0, vc=vc, flags=fl, V=V, faces=faces))
    return res

if __name__ == '__main__':
    m = mem(1365)
    obs = objects(m)
    for o in obs: print('obj', {k: o[k] for k in ('f0', 'fc', 'v0', 'vc', 'flags')}, 'face0..', o['faces'][:4])
    s = sphere(12, F(-4000))
    t = torus(20, 90, F(-2300), F(500), F(50), F(5), F(60), F(3), F(3), F(2), F(4))
    for name, mod, o in (('sphere', s, obs[0]), ('ribbons', t, obs[1])):
        d = np.abs(mod - o['V'])
        print(name, 'max |dxyz|', d[:, :3].max(), 'max |duv|', d[:, 3:].max())
    m2 = mem(2515)
    obs2 = objects(m2)
    for o in obs2: print('obj', {k: o[k] for k in ('f0', 'fc', 'v0', 'vc', 'flags')}, 'face0..', o['faces'][:4])
    t2 = torus(30, 50, F(-2500), F(600), F(0), F(6), F(120), F(4), F(0), F(2), F(2))
    d = np.abs(t2 - obs2[0]['V']); print('tunnel', 'max |dxyz|', d[:, :3].max(), 'max |duv|', d[:, 3:].max())

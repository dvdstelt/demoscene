"""Python model of Nix's object script interpreter (0x14394) for the parts3d-b scripts, checked against the
object buffers in a snapshot.

usage: python3 objects.py SCRIPT TICK      e.g. objects.py 0x157fa 3285 (tube), 0x15973 4435 (spiky), 0x159cb 7506
All maths in float32 (the original runs the x87 at 24-bit precision; fsin/fcos of a float32 argument).
Vertex record (0x40 bytes) fields modelled: +0 x, +4 y, +8 z, +0x2c u, +0x30 v (16.16 texels), +0x20.. normal.
Faces: (i0, i1, i2) vertex indices, texture index (+0xe), mode (+0xd).
"""
import sys; sys.path.insert(0, '.')
import numpy as np
import script

f32 = np.float32
PI = f32(np.pi)
C40 = f32(40.7436637878418)    # [0x15718] = 256/(2 pi)
C81 = f32(81.4873275756836)    # [0x15714] = 256/pi
K16 = f32(65536.0)             # [0x1570c]
WRAP = 16777216.0              # [0x14804] = 2^24 (256 texels in 16.16)


class Obj:
    def __init__(s, V, UV, F, tex, mode):
        s.V, s.UV, s.F, s.tex, s.mode = V, UV, F, tex, mode


def sphere(n, c, R, A1, F1, A2, F2, us, vs):
    N = 2 * n
    V, UV = [], []
    dphi = f32(2 * PI) / f32(N); dth = PI / f32(n)
    th = f32(0)
    for j in range(n + 1):
        ph = f32(0)
        for i in range(N):
            r = f32(f32(1) + f32(np.sin(f32(ph * f32(F1)))) * f32(A1) * f32(np.sin(f32(th * f32(F2)))) * f32(A2)) * f32(R)
            st = f32(np.sin(th))
            V.append((f32(st * f32(np.cos(ph)) * r) + f32(c[0]), f32(np.cos(th)) * r + f32(c[1]), f32(st * f32(np.sin(ph)) * r) + f32(c[2])))
            UV.append((ph * C40 * K16 * f32(us), th * C81 * K16 * f32(vs)))
            ph = f32(ph + dphi)
        th = f32(th + dth)
    F = []
    for j in range(n):
        row, nxt = j * N, (j + 1) * N
        for i in range(N):
            i1 = (i + 1) % N
            if nxt < N * n: F.append((row + i, nxt + i1, nxt + i))
            if j != 0: F.append((row + i, row + i1, nxt + i1))
    return np.array(V, np.float32), np.array(UV, np.float32), F


def norm(v): return v / np.float32(np.sqrt(np.sum(v.astype(np.float32) ** 2)))


def tube(N, M, P0, D, r0, r1, us, vs):
    K = np.array([3.767313003540039, -2.1928300857543945, 0.5280860066413879], np.float32)  # [0x15790]
    D = np.array(D, np.float32); P0 = np.array(P0, np.float32)
    U = norm(np.cross(D, K)); W = norm(np.cross(U, D))     # 0x1542e(esi, ebx) = ebx x esi
    V, UV = [], []
    dphi = f32(2 * PI) / f32(N); dt = f32(1) / f32(M)
    t = f32(0)
    for j in range(M + 1):
        r = f32(f32(r1) - f32(r0)) * t + f32(r0)
        ph = f32(0)
        for i in range(N):
            cp, sp = f32(np.cos(ph)), f32(np.sin(ph))
            V.append(tuple(P0[k] + t * D[k] + f32(cp * U[k] + sp * W[k]) * r for k in range(3)))
            UV.append((ph * C40 * K16 * f32(us), t * f32(256) * K16 * f32(vs)))
            ph = f32(ph + dphi)
        t = f32(t + dt)
    F = []
    for j in range(M):
        row, nxt = j * N, (j + 1) * N
        for i in range(N):
            i1 = (i + 1) % N
            F.append((row + i, nxt + i1, row + i1)); F.append((row + i, nxt + i, nxt + i1))
    return np.array(V, np.float32), np.array(UV, np.float32), F


def torus(N, M, R, r, A1, F1, A2, F2, T, us, vs):
    V, UV = [], []
    dphi = f32(2 * PI) / f32(N); dth = f32(2 * PI) / f32(M)
    th = f32(0)
    for j in range(M):
        ph = f32(0)
        for i in range(N):
            a = ph + th * f32(T)
            rr = f32(r) + f32(np.sin(a * f32(F1))) * f32(A1) + f32(np.sin(th * f32(F2))) * f32(A2)
            rho = rr * f32(np.cos(a)) + f32(R)
            V.append((f32(np.cos(th)) * rho, f32(np.sin(a)) * rr, f32(np.sin(th)) * rho))
            UV.append((ph * C40 * K16 * f32(us), th * C40 * K16 * f32(vs)))
            ph = f32(ph + dphi)
        th = f32(th + dth)
    F = []
    for j in range(M):
        row, nxt = j * N, ((j + 1) % M) * N
        for i in range(N):
            i1 = (i + 1) % N
            F.append((row + i, row + i1, nxt + i1)); F.append((row + i, nxt + i1, nxt + i))
    return np.array(V, np.float32), np.array(UV, np.float32), F


def rotmat(axis, a):
    c, s = f32(np.cos(f32(a))), f32(np.sin(f32(a)))
    if axis == 'x': return np.array([[1, 0, 0], [0, c, -s], [0, s, c]], np.float32)
    if axis == 'y': return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], np.float32)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]], np.float32)


def run(addr):
    ts, dist, ops = script.parse(addr)
    tex, mode, us, vs = 1, 3, 1, 1               # 0x14567 defaults
    objs = []
    for a, name, p in ops:
        if name == 'tex': tex = p['t']
        elif name == 'mode': mode = p['m']
        elif name == 'uvscale': us, vs = p['us'], p['vs']
        elif name == 'sphere':
            V, UV, F = sphere(p['n'], p['c'], p['R'], p['A1'], p['F1'], p['A2'], p['F2'], us, vs); objs.append(Obj(V, UV, F, tex, mode))
        elif name == 'tube':
            V, UV, F = tube(p['N'], p['M'], p['P0'], p['D'], p['r0'], p['r1'], us, vs); objs.append(Obj(V, UV, F, tex, mode))
        elif name == 'torus':
            V, UV, F = torus(p['N'], p['M'], p['R'], p['r'], p['A1'], p['F1'], p['A2'], p['F2'], p['T'], us, vs); objs.append(Obj(V, UV, F, tex, mode))
        elif name == 'dup':
            o = objs[p['obj']]; objs.append(Obj(o.V.copy(), o.UV.copy(), list(o.F), tex, o.mode))
        elif name == 'twist':
            o = objs[p['obj']]
            for k in range(len(o.V)):
                ang = o.V[k][1] * f32(p['k'])
                o.V[k] = rotmat('z', ang) @ o.V[k]
        elif name in ('rotx', 'roty', 'rotz'):
            o = objs[p['obj']]; Mx = rotmat(name[-1], p['a'])
            o.V = (Mx @ o.V.T).T.astype(np.float32)
        elif name == 'translate':
            o = objs[p['obj']]; o.V = o.V + np.array(p['d'], np.float32)
        elif name == 'halve':
            o = objs[-1]; o.F = [fc for k, fc in enumerate(o.F) if (k // 2) % 2 == 0]
        elif name == 'merge':
            b = objs.pop(); o = objs[-1]; base = len(o.V)
            o.V = np.concatenate([o.V, b.V]); o.UV = np.concatenate([o.UV, b.UV]); o.F = o.F + [(x + base, y + base, z + base) for x, y, z in b.F]
        elif name == 'normals':
            for o in objs:
                Nn = np.zeros_like(o.V, dtype=np.float64)
                for i0, i1, i2 in o.F:
                    n = np.cross(o.V[i1].astype(np.float64) - o.V[i0], o.V[i2].astype(np.float64) - o.V[i0])
                    Nn[i0] += n; Nn[i1] += n; Nn[i2] += n
                o.N = (Nn / np.linalg.norm(Nn, axis=1, keepdims=True) * 8388608.0).astype(np.float32)
    return ts, dist, objs


if __name__ == '__main__':
    import snap
    addr, t = int(sys.argv[1], 16), int(sys.argv[2])
    ts, dist, objs = run(addr)
    M = snap.Mem(t)
    VB, FB, OB = M.d(0x18d4c), M.d(0x18d48), M.d(0x18d50)
    for k, o in enumerate(objs):
        ob = OB + 0x50 * k
        f0, nf, v0, nv = M.d(ob), M.d(ob + 4), M.d(ob + 8), M.d(ob + 12)
        raw = np.frombuffer(bytes(M.m[VB + 0x40 * v0: VB + 0x40 * (v0 + nv)]), np.float32).reshape(nv, 16)
        pos, uv, nrm = raw[:, 0:3], raw[:, 11:13], raw[:, 8:11]
        fr = np.frombuffer(bytes(M.m[FB + 0x1c * f0: FB + 0x1c * (f0 + nf)]), np.uint8).reshape(nf, 0x1c)
        fv = fr[:, 0:12].copy().view('<u4').reshape(nf, 3)
        fidx = (fv.astype(np.int64) - VB) // 0x40 - v0
        texs = set(fr[:, 0xe]); modes = set(fr[:, 0xd])
        ok_f = (len(o.F) == nf) and np.array_equal(np.array(o.F), fidx)
        dpos = np.abs(o.V - pos).max() if len(o.V) == nv else None
        rel = (np.abs(o.V - pos) / (np.abs(pos) + 1)).max() if len(o.V) == nv else None
        duv = np.abs(np.fmod(o.UV.astype(np.float64), WRAP) - np.fmod(uv.astype(np.float64), WRAP)).max() / 65536 if len(o.V) == nv else None
        dn = (np.abs(o.N - nrm).max() / 8388608) if hasattr(o, 'N') and len(o.V) == nv else None
        print(f'obj {k}: verts {len(o.V)}/{nv} faces {len(o.F)}/{nf} faces equal {ok_f} tex {o.tex}/{texs} mode {o.mode}/{modes} '
              f'max|dpos| {dpos:.4g} (rel {rel:.2g}) max|duv| {duv:.4g} texels max|dn| {dn}')

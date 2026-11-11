"""Python model of the engine camera: 0x159fb / 0x15a59 (eye and target paths), 0x15c40 (roll wobble), 0x154bc
(look-at matrix with roll, copied into every object). Compares with the matrices that the renderer received
(object +0x10..+0x40 in a render dump r<N>.pre of run_engine.py).

x87 at 24-bit precision: every fadd/fsub/fmul/fdiv/fsqrt result is float32; fsin/fcos/fpatan/fldpi are extended
(modelled with float64), rounded by the operation that uses them or by the dword store.

usage: python3 model_camera.py DUMPDIR N [N ...]
"""
import os, sys, math, pickle, struct
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep

f32 = np.float32
f64 = np.float64
PI = math.pi


def mul(a, b): return f32(f64(a) * f64(b))
def add(a, b): return f32(f64(a) + f64(b))
def sub(a, b): return f32(f64(a) - f64(b))
def div(a, b): return f32(f64(a) / f64(b))
def S(x): return math.sin(f64(x))
def C(x): return math.cos(f64(x))


def scene_params(scene):
    return [f32(rep.rdf(0x15afc + 36 * scene + 4 * i)) for i in range(9)]


def path(t, s, p):
    """0x15a59: point on the camera path at time t, scaled by s; also returns the roll t * p[8]"""
    Rh, Rv, _, _, a, b, c, d, rs = p
    ta = mul(t, a)
    phi = add(ta, mul(S(ta), c))
    cphi = f32(C(phi)); sphi = f32(S(phi))
    th = mul(S(mul(t, b)), d)
    ct = C(th)
    x = mul(mul(mul(ct, cphi), Rh), s)
    z = mul(mul(mul(ct, sphi), Rh), s)
    y = mul(mul(S(th), Rv), s)
    return [x, y, z], mul(t, rs)


def camera(t, scene, F, wobble):
    p = scene_params(scene)
    E, _ = path(t, f32(1), p)                      # 0x1549c: eye
    T, roll = path(add(t, p[3]), p[2], p)          # 0x154a8: target; [0x154b8] roll
    if wobble:                                     # 0x15c40
        r = add(mul(S(mul(t, f32(0.21))), PI), roll)
        r = add(r, mul(S(mul(t, f32(0.62))), PI))
        roll = add(r, f32(S(mul(t, f32(0.14)))))
    # 0x154bc
    T = [sub(T[k], E[k]) for k in range(3)]
    s = div(1, f32(math.sqrt(add(mul(T[0], T[0]), add(mul(T[1], T[1]), mul(T[2], T[2]))))))
    T = [mul(s, T[k]) for k in range(3)]
    T = [add(mul(T[k], F), E[k]) for k in range(3)]       # target moved to distance F in front of the eye
    nT = [-T[k] for k in range(3)]
    D = [add(nT[k], E[k]) for k in range(3)]               # eye - target
    yaw = f32(PI - math.atan2(f64(D[0]), f64(D[2])))
    cy = f32(C(yaw)); sy = f32(S(yaw))
    h = f32(math.sqrt(add(mul(D[0], D[0]), mul(D[2], D[2]))))
    pitch = f32(f64(f32(rep.rdf(0x15710))) - math.atan2(-f64(h), f64(D[1])))
    cp = f32(C(pitch)); sp = f32(S(pitch))
    M = [[cy, f32(0), sy], [mul(sp, sy), cp, -mul(cy, sp)], [-mul(sy, cp), sp, mul(cy, cp)]]
    sr = f32(S(roll)); cr = f32(C(roll))
    r0 = [sub(mul(M[0][k], cr), mul(M[1][k], sr)) for k in range(3)]
    r1 = [add(mul(M[0][k], sr), mul(M[1][k], cr)) for k in range(3)]
    M = [r0, r1, M[2]]
    tr = [add(mul(nT[0], M[r][0]), add(mul(nT[1], M[r][1]), mul(nT[2], M[r][2]))) for r in range(3)]
    return M, tr, roll


def ulps(a, b):
    ia = struct.unpack('<i', struct.pack('<f', float(a)))[0]; ib = struct.unpack('<i', struct.pack('<f', float(b)))[0]
    if (ia < 0) != (ib < 0): return 0 if float(a) == float(b) else 1 << 30
    return abs(ia - ib)


if __name__ == '__main__':
    d = sys.argv[1]
    for n in map(int, sys.argv[2:]):
        pre = pickle.load(open(os.path.join(d, 'r%04d.pre' % n), 'rb'))
        low = pre['low']
        rf = lambda a: f32(struct.unpack_from('<f', low, a)[0])
        ru = lambda a: struct.unpack_from('<I', low, a)[0]
        t = rf(0x159f7); scene = ru(0x157a0); F = rf(0x154b4); wob = low[0x15c3f]
        # the metaball part (0x10fda) calls 0x159fb / 0x154bc itself, without 0x15c40
        M, tr, roll = camera(t, scene, F, wob and ru(0x18d50) and True)
        OB = ru(0x18d50) - pre['eng_lo']
        got = [f32(struct.unpack_from('<f', pre['eng'], OB + 0x10 + 4 * i)[0]) for i in range(13)]
        mine = M[0] + M[1] + M[2] + tr + [F]
        u = [ulps(a, b) for a, b in zip(mine, got)]
        print('render %d: t = %.6f scene %d wobble %d: matrix/translation ulps %s, roll %s vs %s'
              % (n, t, scene, wob, u, roll, rf(0x154b8)))

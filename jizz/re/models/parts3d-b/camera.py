"""Model of the engine camera: path 0x159fb / 0x15a59 (table 0x15afc, 9 floats per path) and look-at 0x154bc.

usage: python3 camera.py TICK      compares with the eye/target/matrix in snapshot TICK (uses its T = [0x159f7])
camera(T, path, dist) -> eye, target', M (3x3, rows), trans (= M * -target'), roll
camera space: p_c = M (p - target'); the engine then adds dist (obj+0x40) to z (see notes).
"""
import sys; sys.path.insert(0, '.')
import numpy as np
f32 = np.float32
sys.path.insert(0, '../..'); import rep


def path_params(k):
    return [f32(rep.rdf(0x15afc + 36 * k + 4 * i)) for i in range(9)]   # A B S2 dt f1 f2 a1 a2 roll


def path_point(T, S, P):
    A, B, S2, dt, f1, f2, a1, a2, roll = P
    a = T * f1 + a1 * f32(np.sin(T * f1))
    b = a2 * f32(np.sin(T * f2))
    return np.array([A * f32(np.cos(a)) * f32(np.cos(b)) * S, B * f32(np.sin(b)) * S, A * f32(np.sin(a)) * f32(np.cos(b)) * S], np.float32)


def camera(T, k, dist, wobble=False):
    P = path_params(k)
    A, B, S2, dt, f1, f2, a1, a2, rollk = P
    T = f32(T)
    eye = path_point(T, f32(1), P)
    tgt = path_point(f32(T + dt), S2, P)
    roll = f32(T + dt) * rollk
    if wobble:   # 0x15c40, only when [0x15c3f] != 0 (not in these parts)
        roll = roll + f32(np.pi) * f32(np.sin(T * f32(0.21))) + f32(np.pi) * f32(np.sin(T * f32(0.62))) + f32(np.sin(T * f32(0.14)))
    d = tgt - eye
    d = d / f32(np.sqrt(np.sum(d * d)))
    tgt2 = d * f32(dist) + eye                    # [0x154a8]: point at distance dist along the view direction
    w = eye - tgt2                                 # [0x15700] = -d * dist
    yaw = f32(np.pi) - f32(np.arctan2(w[0], w[2]))
    c1, s1 = f32(np.cos(yaw)), f32(np.sin(yaw))
    pitch = f32(3 * np.pi / 2) - f32(np.arctan2(-np.sqrt(w[0] * w[0] + w[2] * w[2]), w[1]))
    c2, s2 = f32(np.cos(pitch)), f32(np.sin(pitch))
    M = np.array([[c1, 0, s1], [s1 * s2, c2, -c1 * s2], [-s1 * c2, s2, c1 * c2]], np.float32)
    cr, sr = f32(np.cos(roll)), f32(np.sin(roll))
    r0, r1 = M[0].copy(), M[1].copy()
    M[0] = r0 * cr - r1 * sr
    M[1] = r0 * sr + r1 * cr
    trans = M @ (-tgt2)
    return eye, tgt2, M, trans, roll


if __name__ == '__main__':
    import snap
    t = int(sys.argv[1]); Mm = snap.Mem(t)
    T, k, dist = Mm.f(0x159f7), Mm.d(0x157a0), Mm.f(0x154b4)
    eye, tgt, M, trans, roll = camera(T, k, dist)
    O = Mm.d(0x18d50)
    mat = Mm.fa(O + 0x10, 12)
    print('T', T, 'path', k, 'dist', dist)
    print('eye   model', eye, 'snap', Mm.fa(0x1549c, 3))
    print('tgt   model', tgt, 'snap', Mm.fa(0x154a8, 3))
    print('roll  model', roll, 'snap', Mm.f(0x154b8))
    print('M     model', M.ravel(), '\n      snap ', mat[:9])
    print('trans model', trans, 'snap', mat[9:])
    print('max diff matrix', np.abs(M.ravel() - mat[:9]).max(), 'trans', np.abs(trans - mat[9:]).max())
    print('forward row 2 vs view dir', M[2], (tgt - eye) / np.linalg.norm(tgt - eye))

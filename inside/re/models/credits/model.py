"""Model of the credits part's per-refresh state (tick 0x405f30) and per-frame geometry (0x4061fc, 0x406470,
0x4063fc), checked against the dumps of models/credits/run_inside.py --dump (see check() below).

Arithmetic: the x87 runs at 53-bit precision (control word 0x127f), so Python doubles reproduce it; floats are
rounded with f32(); float -> int conversions truncate toward zero."""
import copy, math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import path as pathmod

f32 = lambda x: float(np.float32(x))
D = pathmod.D
DEG = float.fromhex('0x1.1df46a252dd11p-6')        # 0x420f38 = 0.01745329252 (not exactly pi/180)
SPIN = (0.01, 0.008675, 0.0123)                     # pushed doubles at 0x40604f..0x4060a0, radians per refresh
trunc = lambda x: int(math.trunc(x))


class State:
    """Everything the tick routine steps, with the values init (0x406d10) leaves."""
    def __init__(self):
        self.pos = 0.0               # [0x426434] float, distance flown, +47 per refresh
        self.theta = 0.0             # [0x42284c] float, camera roll, +0.01745329252 per refresh
        self.C = [0.0, 0.0, 0.0]     # [0x426390] doubles, camera position (smoothed)
        self.S = [0.0, 0.0, 0.0]     # [0x426320] doubles, spike ball position (smoothed)
        self.s = 9.0                 # [0x422850] float, spike ball lead in rings (9.0, -0.015 per refresh after 9:48)
        self.event = 0               # [0x426430], set by 0x405f10 at 9:48
        self.spin = np.eye(3)        # [0x4262d8] 3x3 doubles, spike ball orientation


def rot(a, b, c):
    """0x4145f4(M, a, b, c): M = I, then 0x414270(a), 0x414344(b), 0x414418(c), each right-multiplying M by a
    rotation about x, y, z. Signs as checked against the dumped spin matrix (check())."""
    ca, sa, cb, sb, cc, sc = math.cos(a), math.sin(a), math.cos(b), math.sin(b), math.cos(c), math.sin(c)
    Rx = np.array([[1, 0, 0], [0, ca, -sa], [0, sa, ca]])
    Ry = np.array([[cb, 0, sb], [0, 1, 0], [-sb, 0, cb]])
    Rz = np.array([[cc, -sc, 0], [sc, cc, 0], [0, 0, 1]])
    return Rx @ Ry @ Rz


def lerp_path(P, i, j, f):
    """(1 - f) * path[i] + f * path[j] per axis, f a float; x0 * (1 - f) + x1 * f."""
    return [P[i][0][k] * (1 - f) + P[j][0][k] * f for k in range(3)]


def tick(st, P):
    """0x405f30, once per screen refresh."""
    st.pos = f32(st.pos + 47.0)
    n = trunc(st.pos / D)
    while n < 0: n += 1024
    n %= 1024
    f = f32((trunc(st.pos) % D) / D)
    st.theta = f32(st.theta + DEG)
    L = lerp_path(P, n, n + 1, f)                    # n + 1 is not wrapped (n stays < 150 in the part)
    st.C = [st.C[k] * 0.8 + L[k] * 0.2 for k in range(3)]
    st.spin = st.spin @ rot(*SPIN)                   # 0x414070(M, E): M = M E
    if st.event:
        st.s = f32(st.s - 0.015)
    m = (n + trunc(st.s) + 3) % 1024
    L = lerp_path(P, m, (m + 1) % 1024, f)
    L[0] = f32(L[0])                                 # x is stored as a float before the multiply
    st.S = [st.S[k] * 0.96 + L[k] * 0.04 for k in range(3)]


def lookat(cam, target):
    """0x41077c / 0x414cec: rows r, u, d of the camera matrix."""
    d = np.array(target, float) - np.array(cam, float)
    d /= math.sqrt(d[1] * d[1] + d[0] * d[0] + d[2] * d[2])
    r = np.array([-d[2], 0.0, d[0]]); r /= math.sqrt(r[1] * r[1] + r[0] * r[0] + r[2] * r[2])
    u = np.array([d[1] * r[2] - d[2] * r[1], d[2] * r[0] - d[0] * r[2], d[0] * r[1] - d[1] * r[0]])
    u /= math.sqrt(u[1] * u[1] + u[0] * u[0] + u[2] * u[2])
    return np.array([r, u, d])


def frame(st, P, template, light_min=0x70, st_tunnel=None):
    """0x4061fc + 0x406470 + 0x4063fc: camera, tunnel vertices, smoke and spike positions for a drawn frame.
    st_tunnel: the state 0x406470 sees, when a tick ran after 0x4061fc (default: the same state)."""
    out = {}
    stt = st_tunnel or st
    n = trunc(st.pos / D)
    while n < 0: n += 1024
    n %= 1024
    fi = trunc(st.pos) % D
    cam = [trunc(c) for c in st.C]                   # camera +0x10.. (ints)
    m = (n + 3) % 1024
    f = fi / D
    T = [P[m][0][k] * (1 - f) + P[(m + 1) % 1024][0][k] * f for k in range(3)]
    target = [trunc(cam[0] * 0.8 + T[0] * 0.2), trunc(T[1]), trunc(T[2])]
    M = lookat(cam, target)
    out['cam'], out['target'], out['M_lookat'] = cam, target, M
    # tunnel: 11 rings x 12 vertices, ring k from path entry q + k (q = trunc(pos) / D, not wrapped)
    q = trunc(stt.pos) // D
    r10 = q % 10
    verts = []
    for k in range(11):
        e = (q + k) % 1024
        v = ((r10 + k) * 0x1000) // 10                # [0x426338 + 4 (r10 + k)]
        for j in range(12):
            tx, ty = template[j]
            R = P[e][2][j]
            w = M @ np.array([tx * R, ty * R, 0.0])   # 0x4144ec: M v with the look-at matrix
            x = trunc((P[e][0][0] + w[0]) - cam[0])
            y = trunc((P[e][0][1] + w[1]) - cam[1])
            z = trunc((P[e][0][2] + w[2]) - cam[2])
            dist = math.sqrt(float(np.int32(x * x + y * y + z * z)))  # int32 sum (no overflow in practice)
            ex = trunc(dist - 7 * D)
            L = P[e][1][j]
            if ex > 0: L -= ex
            if L < light_min: L = light_min
            u = trunc(j * 256 * 16.0 * (1 / 12))     # 0x420f88 = 1/12
            verts.append((x, y, z, u & 0xffff, v & 0xffff, L))
    out['verts'] = verts
    out['seam_u'] = 0x1000 + 0x1000 // D            # u of the 11 seam copies (4101)
    out['smoke'] = [(P[(q + k) % 1024][0][0] + 180, P[(q + k) % 1024][0][1] + 180, P[(q + k) % 1024][0][2])
                    for k in range(12)]
    out['spike'] = [trunc(c) for c in st.S]
    c, s = math.cos(stt.theta), math.sin(stt.theta)
    out['M_render'] = M @ np.array([[c, -s, 0], [s, c, 0], [0, 0, 1.0]])   # 0x414070(cam M, rot(0, 0, theta))
    return out


TEMPLATE = [(0, 483), (-245, 420), (-420, 238), (-483, 0), (-420, -238), (-245, -413), (0, -476), (238, -413),
            (413, -238), (476, 0), (413, 238), (238, 420)]   # check only: the remake reads them from tuubi.sro


def check(dump):
    """Step the model through the run and compare with every dumped tick and frame."""
    from dumpread import records, I, D as Dd, F, mat
    P = pathmod.build()
    st = State()
    nt = nf = 0
    worst = {'C': 0, 'S': 0, 'pos': 0, 'theta': 0, 'spin': 0, 'M': 0, 'verts': 0, 'smoke': 0, 'spike': 0}
    first_tick = True
    races = late = 0
    st_pre = st
    for r in records(dump):
        if first_tick and 'vars' in r:               # the snapshot is a tick into the part: replay from 0
            first_tick = False
            k = round(F(r['vars'], 0x114) / 47)
            for _ in range(k): tick(st, P)
        if r['tag'] == 'tick':
            v = r['vars']
            worst['pos'] = max(worst['pos'], abs(st.pos - F(v, 0x114)))
            worst['C'] = max(worst['C'], max(abs(a - b) for a, b in zip(st.C, Dd(v, 0x70, 3))))
            worst['S'] = max(worst['S'], max(abs(a - b) for a, b in zip(st.S, Dd(v, 0, 3))))
            worst['theta'] = max(worst['theta'], abs(st.theta - F(r['f42284c'], 0)))
            st.event = I(v, 0x110)
            tick(st, P)
            nt += 1
        elif r['tag'] == 'pre':
            st_pre = copy.deepcopy(st)
        elif r['tag'] == 'render':
            nf += 1
            cam = r['cam']
            o = frame(st_pre, P, TEMPLATE, st_tunnel=st)
            if st_pre.pos != st.pos:
                races += 1
                v0 = r['tverts'][0]
                if (I(v0, 0x28) >> 16) & 0xffff != o['verts'][0][4] or I(v0, 8) != o['verts'][0][0]:
                    # the tick came during 0x406470, after it read pos: the tunnel used the old pos
                    late += 1
                    o2 = frame(st_pre, P, TEMPLATE, st_tunnel=st_pre)
                    o['verts'], o['smoke'] = o2['verts'], o2['smoke']
            if tuple(o['cam']) != I(cam, 0x10, 3):
                print('frame %d: camera %s vs %s' % (r['n'], o['cam'], I(cam, 0x10, 3)))
            worst['M'] = max(worst['M'], np.abs(o['M_render'] - mat(cam)).max())
            for k, b in enumerate(r['tverts'][:132]):
                got = (I(b, 8), I(b, 0xc), I(b, 0x10), I(b, 0x28) & 0xffff, (I(b, 0x28) >> 16) & 0xffff,
                       I(b, 0x30) & 0xffff)
                if got != o['verts'][k]:
                    worst['verts'] += 1
                    if worst['verts'] < 5: print('frame %d vertex %d: model %s dump %s' % (r['n'], k, o['verts'][k], got))
            for k in range(12):
                if I(r['smoke'][k], 0x10, 3) != tuple(o['smoke'][k]): worst['smoke'] += 1
            if I(r['spike_obj'], 0x10, 3) != tuple(o['spike']): worst['spike'] += 1
            spin = mat(r['spike_obj'])
            worst['spin'] = max(worst['spin'], np.abs(spin - st_pre.spin).max())
    print('%d ticks, %d frames (%d with a tick after 0x4061fc, %d of them after 0x406470 read pos); worst '
          'differences: %s' % (nt, nf, races, late, worst))


if __name__ == '__main__':
    check(sys.argv[1])

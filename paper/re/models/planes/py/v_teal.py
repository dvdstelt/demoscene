import numpy as np, struct
from emu import Emu
from model import Fix, s32
e = Emu(6100)
sin = e.ii(0x22479, 1024)
F = Fix(sin)
W = e.d(0x1edd5)
def table():
    return [e.ii(W + 12 * n, 3) for n in range(72)]
def noise(tbl, k):
    edx = 0
    for n, cl in enumerate((4, 3, 2, 1)):
        ph, amp, spd = tbl[k + 2 * n]
        x = s32(ph << (cl + 1)) >> 2
        x = s32(x + 0x8000000)
        f = x & 0xff
        x >>= 8
        s0 = sin[x & 0x3ff]; s1 = sin[(x + 1) & 0x3ff]
        val = s32(s1 * f + s0 * (f ^ 0xff)) >> 8
        val = s32(val * amp) >> cl
        edx = s32(edx + val)
    return edx >> 15
def step_model(st):
    tbl = [[s32(p + 2 * s), a, s] for p, a, s in st['tbl']]   # 0x26d0 twice
    st['tbl'] = tbl
    N1 = noise(tbl, 0); N2 = noise(tbl, 8); N3 = noise(tbl, 16)
    yaw = (N1 * 5) >> 3; pitch = (N2 >> 6) + 25; roll = N3 + 0x180
    A = F.rotx(0x100); B = F.roty(yaw)
    Cm = Fix.mul(B, A)
    P = F.rotx(pitch); Z = F.rotz(roll)
    R = Fix.mul(Z, Fix.mul(P, Cm))
    st['pos'][0] = s32(st['pos'][0] + ((R[6] * 3) >> 1))
    st['pos'][1] = s32(st['pos'][1] + ((R[7] * 3) >> 1))
    h = st['hist'][st['idx']]
    cam = (h[0] >> 10, h[1] >> 10, N2 >> 11)
    V = h[2]
    st['hist'][st['idx']] = (st['pos'][0], st['pos'][1], R)
    st['idx'] = (st['idx'] + 1) % 28
    k = st['k']
    objpos = [(st['pos'][c] >> 10) + ((V[c] * k) >> 15) - ((V[6 + c] * k) >> 15) for c in range(3)]
    return dict(R=R, cam=cam, V=V, objpos=objpos, N=(N1, N2, N3), ang=(yaw, pitch, roll))
H = e.d(0x2449d)
st = dict(tbl=table(), pos=e.ii(0x6479, 3), idx=e.i(0x24499), k=e.i(0x4843),
          hist=[(e.i(H + 48 * n), e.i(H + 48 * n + 4), e.ii(H + 48 * n + 12, 9)) for n in range(28)])
obj = e.d(0x6816)
for t in range(5):
    # counter <= 0x2f0 at 6100, k stays
    e.call(0x6439)
    r = step_model(st)
    ok = dict(R=r['R'] == e.ii(0x6455, 9), pos=st['pos'][:2] == e.ii(0x6479, 2),
              cam=list(r['cam']) == e.ii(0x1ea9d, 3), V=r['V'] == e.ii(0x1e468, 9),
              objpos=r['objpos'] == e.ii(obj + 0x48, 3),
              objm=e.ii(obj, 9) == [r['R'][i] for i in (0, 3, 6, 1, 4, 7, 2, 5, 8)],
              fm=np.allclose(np.array(e.ff(0x23a8, 9)), np.array(r['V']) / 65536.0),
              fc=e.ff(0x23cc, 3) == [float(x) for x in r['cam']])
    print(t, ok, r['ang'], st['pos'], r['objpos'])

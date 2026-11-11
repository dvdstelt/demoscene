import json, numpy as np
from model import Fix, s32
from snap import M
p = json.load(open('../runs/teal.probe.json'))
m61 = M(6100); m70 = M(7000)
sin = m61.ii(0x22479, 1024); F = Fix(sin)
tb = p['table']; tbl = [[tb[3 * n], tb[3 * n + 1], tb[3 * n + 2]] for n in range(72)]
def noise(tbl, k):
    edx = 0
    for n, cl in enumerate((4, 3, 2, 1)):
        ph, amp, spd = tbl[k + 2 * n]
        x = s32(s32(ph << (cl + 1)) >> 2) ; x = s32(x + 0x8000000); f = x & 0xff; x >>= 8
        val = s32(sin[(x + 1) & 0x3ff] * f + sin[x & 0x3ff] * (f ^ 0xff)) >> 8
        edx = s32(edx + (s32(val * amp) >> cl))
    return edx >> 15
pos = [0, 0, 0]; hist = [(0, 0, [0] * 9)] * 28; idx = 0
counter = {}
for line in open('/home/dennis/Downloads/acme_303/paper/re/full.ticks'):
    a = line.split(); counter[int(a[0])] = int(a[1])
k = 900
def step(tick):
    global idx, k
    N1, N2, N3 = noise(tbl, 0), noise(tbl, 8), noise(tbl, 16)
    R = Fix.mul(F.rotz(N3 + 0x180), Fix.mul(F.rotx((N2 >> 6) + 25), Fix.mul(F.roty((N1 * 5) >> 3), F.rotx(0x100))))
    pos[0] = s32(pos[0] + ((R[6] * 3) >> 1)); pos[1] = s32(pos[1] + ((R[7] * 3) >> 1))
    h = hist[idx]; hist[idx] = (pos[0], pos[1], R); idx = (idx + 1) % 28
    if tick is not None and counter[tick] > 0x2f0 and k: k -= 2
    return (N1, N2, N3)
for i in range(28): step(None)
traj = []
for tick in range(6018, 7741):
    for e in tbl: e[0] = s32(e[0] + 2 * e[2])
    N = step(tick)
    traj.append((tick, pos[0] >> 10, pos[1] >> 10, (N[0] * 5) >> 3, (N[1] >> 6) + 25, N[2] + 0x180, k))
    if tick == 6099: print('6100 pos', pos[:2], m61.ii(0x6479, 2), 'idx', idx, m61.i(0x24499), 'k', k, m61.i(0x4843))
    if tick == 6999: print('7000 pos', pos[:2], m70.ii(0x6479, 2), 'idx', idx, m70.i(0x24499), 'k', k, m70.i(0x4843))
traj = np.array(traj)
np.save('../teal_traj.npy', traj)
print('k reaches 0 at tick', traj[np.argmax(traj[:, 6] == 0), 0], 'starts dec at', traj[np.argmax(traj[:, 6] < 900), 0])
for c, n in zip(range(1, 7), ('x', 'y', 'yaw', 'pitch', 'roll', 'k')): print(n, traj[:, c].min(), traj[:, c].max())

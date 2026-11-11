import sys, struct
import numpy as np
from collections import deque
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import rep
O = '/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-logo/'
fm = np.load(O + 'fillmap.npy')
FR = 64768
d = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', dtype=np.uint8, mode='r')
def fr(t): return np.array(d[t*FR:t*FR+64000]).reshape(200,320)
seeds = [struct.unpack_from('<I', rep.SEG, 0x585f + 4*i)[0] for i in range(9)]
print('seeds', [hex(s) for s in seeds], [(s % 320, s // 320) for s in seeds])
print('seed map values', [int(fm[s // 320, s % 320]) if s else None for s in seeds])
H, W = fm.shape
# geodesic distance from each seed
INF = 10**9
def dist(sx, sy):
    D = np.full((H, W), INF, np.int64)
    if fm[sy, sx] == 0: return D
    D[sy, sx] = 0; q = deque([(sx, sy)])
    while q:
        x, y = q.popleft()
        for nx, ny in ((x-1,y),(x+1,y),(x,y+1),(x,y-1)):
            if 0 <= nx < W and 0 <= ny < H and fm[ny, nx] and D[ny, nx] == INF:
                D[ny, nx] = D[y, x] + 1; q.append((nx, ny))
    return D
Tk = [5681, 5715, 5748, 5782, 5815, 5849, 5883, 5916]
Ds = [dist(s % 320, s // 320) for s in seeds[:8]]
for k, D in enumerate(Ds):
    r = D < INF
    print('seed', k, 'tick', Tk[k], 'region px', r.sum(), 'max depth', D[r].max())
best = np.full((H, W), INF, np.int64)
for k, D in enumerate(Ds):
    best = np.minimum(best, np.where(D < INF, D + Tk[k], INF))
nz = fm > 0
print('nonzero px', nz.sum(), 'unreached', (nz & (best == INF)).sum())
print('last fill tick (if unlimited)', best[best < INF].max())
# compare with frames
for t in [5700, 5750, 5800, 5850, 5900, 5950, 6000, 6017]:
    f = fr(t)[50:50+H, 48:48+W]
    drawn = (f >= 0x41) & (f <= 0x4f)
    for off in (0, 1):
        sim = best + off <= t
        print(t, 'off', off, 'drawn', drawn.sum(), 'sim', sim.sum(), 'mismatch', (drawn != sim).sum(), 'colour ok', (f[sim & drawn] == 0x40 + fm[sim & drawn]).all())
print('fraction filled at end (6017):', ((best + 1 <= 6017) & nz).sum() / nz.sum())
for k, D in enumerate(Ds):
    ys, xs = np.nonzero(D < INF)
    print('seed', k, 'screen bbox x', xs.min() + 48, xs.max() + 48, 'y', ys.min() + 50, ys.max() + 50, 'done at tick', Tk[k] + D[D < INF].max())
# list size estimate: drawn + walls adjacent
nz = fm > 0
pad = np.pad(nz, 1)
adj = pad[:-2, 1:-1] | pad[2:, 1:-1] | pad[1:-1, :-2] | pad[1:-1, 2:]
print('wall pixels pushed (4-adjacent to ink):', (adj & ~nz).sum(), 'total list', (adj & ~nz).sum() + nz.sum())

from common import *
import part5
from framemap import framemap
T = read_ticks_full(); IRQ0 = T[4693]['irqs']
FM = framemap('38520', 0, 4693, 5751, IRQ0)
cbf = {}
for k in sorted(T):
    n = T[k]['irqs'] - IRQ0
    if 4693 <= k <= 5751 and n > 0 and n not in cbf: cbf[n] = k
pos = lambda n: (T[cbf[n]]['order'], T[cbf[n]]['row']) if n in cbf else (15, 13)
st = part5.State(pos); R = part5.Renderer()
zmin, zmax, smax = 1 << 30, 0, 0
for j, k, n in FM:
    while st.n < n: st.callback()
    R.project(st)
    zs = [p[2] for p in R.P]
    if j == 0:
        print('frame 0: z range', min(zs), max(zs))
        continue
    zmin, zmax = min(zmin, min(zs)), max(zmax, max(zs))
    smax = max(smax, max(R.P[a][2] + R.P[b][2] for a, b in part5.EDGES))
print('drawn frames 1..: z range', zmin, zmax, 'max z sum', smax)
print('final state: n', st.n, 'pitch', st.pitch, 'yaw', st.yaw, 'roll', st.roll, 'mappos %#x' % st.mappos)
st = part5.State(pos); R = part5.Renderer()
for j, k, n in FM:
    while st.n < n: st.callback()
    R.project(st)
    if j == 0: continue
    beh = [i for i, p in enumerate(R.P) if p[2] <= 0]
    if beh:
        used = [(a, b) for a, b in part5.EDGES if (a in beh or b in beh) and R.P[a][2] + R.P[b][2] > 0]
        print('drawn', j, 'record', k, 'n', n, 'vertices behind', beh, 'edges drawn with a stale end', len(used),
              [(R.P[v][0], R.P[v][1]) for v in beh])

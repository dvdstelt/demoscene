"""Statistics over the models: byte overflows in the glow adds, behind-camera vertices, line counts."""
import sys
import numpy as np
from common import *
import part3, part5
from framemap import framemap

T = read_ticks_full()
stats = dict(blobs=0, overflow_blobs=0, maxval=0)


def counting_blob(buf, o, KER=part3.KER):
    before = [buf[o + d + i] for d, _, _ in KER for i in range(8)]
    orig(buf, o)
    sums = [before[r * 8 + i] + [b for b in (KER[r][1].to_bytes(4, 'little') + KER[r][2].to_bytes(4, 'little'))][i]
            for r in range(8) for i in range(8)]
    stats['blobs'] += 1
    m = max(sums)
    stats['maxval'] = max(stats['maxval'], m)
    if m > 255:
        stats['overflow_blobs'] += 1


which = sys.argv[1]
step = int(sys.argv[2])
if which == '3':
    orig = part3.blob
    part3.blob = counting_blob
    IRQ0 = 2325
    FM = framemap('3554b', 0, 2334, 3470, IRQ0)
    cbf = {}
    for k in sorted(T):
        n = T[k]['irqs'] - IRQ0
        if 2331 <= k <= 3470 and n > 0 and n not in cbf: cbf[n] = k
    pos = lambda n: (T[cbf[n]]['order'], T[cbf[n]]['row']) if n in cbf else (0, 0)
    st = part3.State(pos); R = part3.Renderer()
    behind = 0
    for j, k, n in FM:
        while st.n < n: st.callback()
        if j % step == 0:
            R.draw(st)
        else:
            R.noise(); R.hist[32] = R.h; R.hist = R.hist[1:] + [R.hist[32]]
            R.h = ((part3.SIN[st.a30] + part3.SIN[st.a34]) >> 2) & 0xfff
    print('part 3', stats)
else:
    orig = part5.blob
    part5.blob = counting_blob
    IRQ0 = T[4693]['irqs']
    FM = framemap('38520', 0, 4693, 5751, IRQ0)
    cbf = {}
    for k in sorted(T):
        n = T[k]['irqs'] - IRQ0
        if 4693 <= k <= 5751 and n > 0 and n not in cbf: cbf[n] = k
    pos = lambda n: (T[cbf[n]]['order'], T[cbf[n]]['row']) if n in cbf else (15, 13)
    st = part5.State(pos); R = part5.Renderer()
    behind_frames = 0; zmin = 1 << 30; zsum_max = 0; nkeys = []
    for j, k, n in FM:
        while st.n < n: st.callback()
        if j % step == 0 or j < 3:
            R.project(st)
            zs = [p[2] for p in R.P]
            zmin = min(zmin, min(zs))
            used = [e for e, (a, b) in enumerate(part5.EDGES) if R.P[a][2] + R.P[b][2] > 0]
            nkeys.append(len(used))
            if any(R.P[a][2] <= 0 or R.P[b][2] <= 0 for e in used for a, b in [part5.EDGES[e]]):
                behind_frames += 1
            zsum_max = max(zsum_max, max(R.P[a][2] + R.P[b][2] for a, b in part5.EDGES))
            if j % (step * 4) == 0 and j > 2:
                R.seed = R.seed  # noise skipped
                buf = R.draw(st, (T[k - 1]['order'], T[k - 1]['row']))
    print('part 5', stats, 'zmin', zmin, 'frames drawing an edge with an endpoint behind', behind_frames,
          'max z sum', zsum_max, 'edges drawn min/max', min(nkeys), max(nkeys))

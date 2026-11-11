import numpy as np, pickle
from flockmodel import *
col5 = {}
for l in open(RE + 'full.ticks'):
    a = l.split(); col5[int(a[0])] = int(a[4])
G = {}; g = -2
for t in range(8818, 10968):
    g += col5[t]; G[t] = g
fr = pickle.load(open('flock_frames.pkl', 'rb'))
PV, PF = plane_model()
data = np.memmap(RE + 'full.frames', dtype=np.uint8, mode='r'); FR = 64768
for t in list(range(8840, 10968, 197)) + [9499]:
    cap = np.asarray(data[t * FR:t * FR + 64000]).reshape(200, 320) < 64
    f = G[t]
    out = []
    for dc in (-1, 0, 1):
        st = (fr[f][0], fr[f + dc][1], fr[f + dc][2], None)
        m = mask(st, PV, PF)
        out.append(round((m & cap).sum() / max(1, (m | cap).sum()), 3))
    print(t, f, out)

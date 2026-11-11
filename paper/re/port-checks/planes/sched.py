import numpy as np, pickle
from flockmodel import *
col5 = {}
for l in open(RE + 'full.ticks'):
    a = l.split(); col5[int(a[0])] = int(a[4])
G = {}; g = -2  # 2 greetings 0x7a64 calls in tick 8818 (hypothesis)
for t in range(8818, 10968):
    g += col5[t]; G[t] = g
fr, ncalls = frames(1080)
print('rng calls after 1080 frames', ncalls - 179785)
pickle.dump(fr, open('flock_frames.pkl', 'wb'))
PV, PF = plane_model()
data = np.memmap(RE + 'full.frames', dtype=np.uint8, mode='r'); FR = 64768
res = []
for t in list(range(8840, 10968, 97)) + [9499]:
    cap = np.asarray(data[t * FR:t * FR + 64000]).reshape(200, 320) < 64
    best = []
    for off in range(-3, 4):
        f = G[t] - 1 + off
        if f < 1 or f >= len(fr): continue
        m = mask(fr[f], PV, PF)
        iou = (m & cap).sum() / max(1, (m | cap).sum())
        best.append((round(iou, 3), off))
    best.sort(reverse=True)
    print(t, 'G', G[t], 'cap px', cap.sum(), best[:3])

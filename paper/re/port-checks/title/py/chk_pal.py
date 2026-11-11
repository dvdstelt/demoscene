import sys
from tmodel import *
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/remake/tools')
# fade levels: replicate fade.cpp events
EV = {100: (0x8000, 150), 1046: (0xff00, 1000), 1080: (0x8000, 400), 7631: (0, 300), 7744: (0x8000, 400), 8707: (0, 300), 8818: (0x8000, 300), 10857: (0, 300), 10968: (0x8000, 400), 11344: (0xff00, 1000), 11377: (0x8000, 512)}
lv = 0; tg = 0; st = 0; LV = []
for t in range(12236):
    if t in EV:
        tg, st = EV[t]
        if t == 8818: lv = 300
    if lv < tg: lv = min(lv + st, tg)
    elif lv > tg: lv = max(lv - st, tg)
    LV.append(lv)
def find(t, a0, a1, sh=11):
    _, pal = cap(t)
    hits = []
    for a in range(a0, a1):
        p, _ = model.palette(a, sh)
        if np.array_equal(model.fade(p, LV[t]), pal.astype(np.int32)): hits.append(a)
    return hits
for t in map(int, sys.argv[1:]):
    if t < 2000: print(t, 'level', hex(LV[t]), 'angles', find(t, max(0, t-110), t-100))
    elif t < 11377: print(t, 'level', hex(LV[t]), 'angles', [a for a in find(t, 0, 1024)])
    else: print(t, 'level', hex(LV[t]), 'angles', find(t, 0, 1024, 12))

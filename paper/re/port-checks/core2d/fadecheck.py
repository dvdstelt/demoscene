import json, numpy as np
EV = [(100, 0x8000, 150, None), (1046, 0xff00, 1000, None), (1080, 0x8000, 400, None), (7631, 0, 300, None),
      (7744, 0x8000, 400, None), (8707, 0, 300, None), (8818, 0x8000, 300, 300), (10857, 0, 300, None),
      (10968, 0x8000, 400, None), (11344, 0xff00, 1000, None), (11377, 0x8000, 512, None)]
N = 12236
lv = [0]*N; level = 0; tg = 0; st = 0; e = 0
for t in range(N):
    while e < len(EV) and EV[e][0] == t:
        _, tg, st, s = EV[e]; e += 1
        if s is not None: level = s
    if level < tg: level = min(level + st, tg)
    elif level > tg: level = max(level - st, tg)
    lv[t] = level
log = json.load(open('fade/all.json'))
bad = [t for t in range(12235) if log[str(t)][0] != lv[t]]
print('model vs emulator log: mismatches', len(bad), bad[:10])
def up(p, level):
    L = level >> 8
    if L <= 128: return (p * L) >> 7
    b = min(L, 255) - 128
    return ((((p * (127 - b)) >> 6) & 255) + b) >> 1
FR = 64768
data = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', dtype=np.uint8, mode='r')
nrec = len(data) // FR
img = open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()[0xd00:]
defpal = np.frombuffer(img[0x169a3:0x169a3+768], dtype=np.uint8).astype(int)
bad255 = []; badfull = []; nfull = 0
for t in range(min(nrec, 12235)):
    dac = np.asarray(data[t*FR+64000:(t+1)*FR]).astype(int)
    if dac[765] != up(63, lv[t]) or dac[766] != up(63, lv[t]) or dac[767] != up(63, lv[t]):
        bad255.append((t, int(dac[765]), up(63, lv[t]), lv[t]))
    # default palette parts: sketch..logo (1080..6017) and flock 8818..10967 entries 0..127
    if 1080 <= t < 7741 or 8818 <= t < 10968:
        nfull += 1
        exp = np.array([up(int(p), lv[t]) for p in defpal[:384]])
        if not np.array_equal(exp, dac[:384]): badfull.append(t)
print('records', nrec, 'entry 255 mismatches', len(bad255), bad255[:20])
print('default-palette ticks checked', nfull, 'mismatches (entries 0..127)', len(badfull), badfull[:20])

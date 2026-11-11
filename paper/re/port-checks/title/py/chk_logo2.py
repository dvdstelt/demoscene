import sys, pickle, collections
from tmodel import *
logo, lb = model.logo_buffer(G)
states = pickle.load(open('b1996_states.pkl', 'rb'))
Zs = pickle.load(open('zs.pkl', 'rb'))
stats = collections.Counter()
fails = []
step = int(sys.argv[3]) if len(sys.argv) > 3 else 1
for t in range(int(sys.argv[1]), int(sys.argv[2]), step):
    idx, _ = cap(t)
    fr = ticks[t][4]
    found = None
    for dn in (0, -1, -2, 1):
        n = t - 11377 + dn
        s = Zs[n] >> 4
        b0 = (t - 11377) // 8
        for db in (0, -1, 1, -2):
            b = b0 + db
            _, vga = model.logo_frame(logo, lb, states[b].astype(np.int64), s)
            if np.array_equal(vga.reshape(200, 320).astype(np.uint8), idx): found = (dn, db, (t - 11377) % 8); break
        if found: break
    stats[(fr,) + (found if found else (None,))] += 1
    if not found: fails.append(t)
for k, v in sorted(stats.items(), key=str): print(k, v)
print('fails', fails[:20])

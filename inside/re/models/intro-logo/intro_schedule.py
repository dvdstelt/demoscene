"""The intro's frame schedule in the capture: which tick routine calls (refreshes) come before each frame's drop
loop, the frame rate over time, ticks that fell inside a drop loop, and where the 3:00 event ran. This is what an
exact replay needs (the light field is stepped once per drawn frame)."""
import sys, pickle
import numpy as np
import intro_model as M
ev = pickle.load(open(sys.argv[1], 'rb'))
ticks = 1                                 # the tick of refresh 38 (T = 2) ran before the snapshot, after frame -1's drops
rows = []                                 # (frame n, ticks before its drop loop, refresh)
inside = []
cur = None
for i, e in enumerate(ev):
    k = e['kind']
    if k == 'tick_in':
        if cur is not None:
            inside.append((cur, ticks))
        ticks += 1
    elif k == 'f_entry':
        cur = e['n']; rows.append((e['n'], ticks, e['refresh']))
    elif k == 'f_drops':
        cur = None
    elif k == 'ev_4031a0':
        print('3:00 event (0x4031a0) after frame %d, %d ticks so far, refresh %d' % (rows[-1][0], ticks, e['refresh']))
rows = [(-1, 0, 38)] + rows
print('frames', len(rows), 'ticks', ticks)
print('ticks inside a drop loop (frame, tick number):', inside)
tb = np.array([r[1] for r in rows])
print('frames per tick overall: %.4f; per 100 ticks:' % (len(rows) / ticks))
h = np.bincount(tb // 100)
print(' '.join(str(x) for x in h))
gaps = np.diff(tb)
print('ticks between consecutive frames: histogram', {int(k): int(v) for k, v in zip(*np.unique(gaps, return_counts=True))})
# index-0 pixels of intro.raw stay black?
blk = [i for i in range(256) if M.np.frombuffer(M.DAT.get('intro/intro.pal'), M.np.uint8).reshape(256, 3)[i].sum() == 0]
print('black palette entries', blk, ' shd[L][0] for all L:', sorted(set(M.SHD[:, 0])), ' shd[L][76]:', sorted(set(M.SHD[:, 76])))
np.savetxt(sys.argv[2] if len(sys.argv) > 2 else '/dev/null', np.array(rows), fmt='%d', header='frame ticks_before refresh')

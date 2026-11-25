"""For every drawn frame without a heart behind the transparent one (after the first that had one): records shown,
and the number of pixels the stale map changes (versus a clean map)."""
import wdata
import wlog
import wmodel
from check_frames import env_after

D = wdata.load()
F = wlog.frames()
first = min(i for i, f in enumerate(F) if f['behind'])
runs = []
for i in range(first, len(F)):
    f = F[i]
    if f['behind'] or not f['shown']:
        continue
    a, _, _ = wmodel.render(D, f['counts'], env_state=env_after(i - 1))
    b, _, _ = wmodel.render(D, f['counts'])
    d = int((a != b).sum())
    runs.append((f['shown'][0], d))
cur = None
for rec, d in runs:
    vis = d > 0
    if cur and cur[2] == vis and rec - cur[1] <= 3:
        cur[1] = rec
        cur[3] = max(cur[3], d)
    else:
        if cur:
            print('records %d..%d stale map %s (max %d pixels)' % (cur[0], cur[1], 'visible' if cur[2] else 'not visible', cur[3]))
        cur = [rec, rec, vis, d]
print('records %d..%d stale map %s (max %d pixels)' % (cur[0], cur[1], 'visible' if cur[2] else 'not visible', cur[3]))

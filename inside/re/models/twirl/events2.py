"""Find when the two events at 30:48 (set 3, nigga4) and 30:12 (set 4, jeesus2) took effect: try tick-step numbers
for each and report which render records 10843..10850 exactly."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import frame
from sheet import open_frames, indexed
raw, _ = open_frames(os.path.join(frame.RE, 'full.frames'))
picks = {1: 0, 2: 1, 3: 2, 4: 3, 0: 4}
for e3 in range(268, 273):
    for e4 in range(e3, e3 + 3):
        states, sets = frame.schedule([(138, 2), (e3, 3), (e4, 4), (555, 0)])
        res = []
        for k in range(10842, 10852):
            px, _ = indexed(raw, k)
            n = k - 10575
            best = min(((int((frame.render(states[m], frame.SETS[s], frame.pic(picks[s])) != px).sum()), m - n, s)
                        for m in (n - 1, n) for s in {sets[m], sets[m + 1]}))
            res.append(best)
        print(e3, e4, [r[0] for r in res], [r[1:] for r in res][:4])

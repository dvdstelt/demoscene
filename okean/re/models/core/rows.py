"""The time of every music row in the capture, from the 'tick' events of run_core.py's logs, checked against
full.ticks and full.gus.

A tick event is logged at the entry of the player tick 0x1343c with the position before the tick; the position
after tick i is the position before tick i + 1. A row starts at the tick after which the position differs.
Writes models/core/rows.txt: order row start_tick time_s refresh line ticks_in_row
usage: python3 models/core/rows.py
"""
import glob
import os

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
T = 808 * 625 / 25.175e6
LP = T / 625
ticks = {}
for path in sorted(glob.glob(os.path.join(HERE, 'runs', 'p*.ev'))):
    for line in open(path):
        f = line.split()
        if f[0] != 'tick':
            continue
        m = int(f[10])
        rec = (float(f[3]), int(f[1]), float(f[2]), int(f[6]), int(f[7]), int(f[9]))
        if m in ticks and abs(ticks[m][0] - rec[0]) > 1e-9:
            print('runs disagree at tick', m, ticks[m], rec)
        ticks[m] = rec
ms = sorted(ticks)
print('ticks %d..%d (%d), missing: %s' % (ms[0], ms[-1], len(ms), [m for m in range(ms[0], ms[-1]) if m not in ticks][:10]))
# compare with full.gus
gus = {}
for line in open(os.path.join(RE, 'full.gus')):
    f = line.split()
    if len(f) == 4 and f[1] == 'irq' and 't' in f[2]:
        gus[int(f[0])] = float(f[3])
bad = [m for m in ms if m in gus and abs(gus[m] - ticks[m][0]) > 2e-6]
print('tick times differing from full.gus by > 2 us:', len(bad), bad[:5])
rows = []
for i, m in enumerate(ms[:-1]):
    if ms[i + 1] != m + 1:
        continue
    before = ticks[m][3:5]
    after = ticks[m + 1][3:5]
    if after != before:
        rows.append([after[0], after[1], m, ticks[m][0], ticks[m][1], ticks[m][2], ticks[m + 1][5]])
for i in range(len(rows) - 1):
    rows[i].append(rows[i + 1][2] - rows[i][2])
rows[-1].append(0)
with open(os.path.join(HERE, 'rows.txt'), 'w') as f:
    f.write('# order row start_tick time_s refresh line speed ticks_in_row   (refresh/line: when the tick IRQ was '
            'served; line 0..624, retrace at 502)\n')
    for r in rows:
        f.write('%d %d %d %.6f %d %.2f %d %d\n' % tuple(r))
print('rows:', len(rows))
# check against full.ticks: position at the end of every refresh
pos_after = {}
for m in ms[:-1]:
    if m + 1 in ticks:
        pos_after[m] = ticks[m + 1][3:5]
mism = 0
n = 0
for line in open(os.path.join(RE, 'full.ticks')):
    if line.startswith('#'):
        continue
    f = line.split()
    k, mt, o, r = int(f[0]), int(f[5]), int(f[2]), int(f[3])
    if mt in pos_after:
        n += 1
        if pos_after[mt] != (o, r):
            mism += 1
print('full.ticks records checked: %d, position differs from the tick model: %d' % (n, mism))
# speeds
sp = {}
for r in rows:
    sp.setdefault(r[6], []).append((r[0], r[1]))
for s in sorted(sp):
    print('speed %d: %d rows, first %s last %s' % (s, len(sp[s]), sp[s][0], sp[s][-1]))

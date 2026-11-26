"""From a run_vivid_pos.py .pos log: when MIDAS's player changes the row (live, in its timer interrupt) and when the
demo's main line sees it (its poll right after the next retrace, line 412 of the refresh period).

usage: python3 models/core/rowdelay.py runs/z2611.pos
"""
import sys

rows = [l.split() for l in open(sys.argv[1])]
prev = None
delays = []
for r in rows:
    t, per, line, cnt, lo, lr = float(r[0]), int(r[1]), float(r[2]), int(r[3]), int(r[4]), int(r[5])
    if prev is not None and (lo, lr) != prev:
        # the interrupt that changed the row ran at (per, line); the next retrace is at line 412 of this period
        # or the next one (an interrupt AT the retrace (line 412.x) is the screen interrupt itself)
        wait = (412 - line) / 449 if line < 412 else (449 - line + 412) / 449
        delays.append(wait)
        if len(delays) <= 12:
            print('row %d:%d at period %d line %.1f -> seen after the retrace %.3f refreshes later' % (lo, lr, per, line, wait))
    prev = (lo, lr)
delays.sort()
print('%d row changes; delay to the next retrace: min %.3f median %.3f max %.3f refreshes' % (
    len(delays), delays[0], delays[len(delays) // 2], delays[-1]))

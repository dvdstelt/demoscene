"""When each caption page shows: runs the printer (sheet_model.Printer) on the draw schedule of compare_all.py and
lists, per page of the script, its address, text, and the first and last record (and position) that show it."""
import os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sheet_model import RE, Tables, Printer
T = Tables()
ticks = {}
for ln in open(os.path.join(RE, 'full.ticks')):
    if ln[0] != '#':
        f = ln.split()
        ticks[int(f[0])] = (int(f[2]), int(f[3]))
repeats = [6976, 6982, 7065, 7071, 7166, 7249, 7261, 7338, 7344, 7427, 7433, 7516, 7522, 7528, 7611, 7623, 7700,
           7706, 7712, 7789, 7795, 7801, 7878, 7890, 7973, 7979, 8062, 8074]
pr = Printer(T)
buf = np.zeros(64000, np.uint8)
P = 6895
pages = {}
for k in range(6896, 8097):
    if k in repeats:
        continue
    if k == 7517:                       # the hidden draw (copied early in 7517, never recorded)
        pr.call(buf, *ticks[P])
        P = 7516
    start = pr.ptr
    pr.call(buf, *ticks[P])
    pages.setdefault(start, []).append((k, ticks[P]))
    P = k
for a, lst in sorted(pages.items()):
    end = a
    while T.rd(end, 1)[0] != 0x40:
        end += 1
    txt = T.rd(a, end - a + 1).decode('latin1').replace('\r', '\\r')
    (k0, p0), (k1, p1) = lst[0], lst[-1]
    print('%05x %-32r records %d..%d (%d), position at draw %d:%d .. %d:%d' % (a, txt, k0, k1, len(lst), *p0, *p1))

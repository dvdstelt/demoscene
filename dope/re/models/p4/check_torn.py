"""For the landscape frames that check_capture.py does not match whole: test every screen row against the
composites of the last calls of the frame loop before the capture (landscape rendered with [2113] = P, P-2, P-4,
since an IRQ may advance it while 1efc runs, and the fire states of those calls). A row that matches one of them is
explained by tearing (the loop writes VGA memory without waiting for the retrace).
usage (from dope/re): python3 models/p4/check_torn.py frame ...
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, RE)
import fire_model as FM  # noqa: E402
import land_model as LM  # noqa: E402
import sheet  # noqa: E402

rows = [l.split() for l in open(os.path.join(HERE, 'out', 'frames.log'))]
raw, W, H, rec, hdr, n = sheet.open_frames(os.path.join(RE, 'full.frames'))
labels = {s + 0xf64: FM.seg(s, 70 * 44, 0x30a).reshape(44, 70) for s in (0x273, 0x36e)}
F = np.zeros(32000, np.uint8)
prev = 0
state = []
for r in rows:
    off = int(r[11])
    if off != prev:
        F = FM.fire_step(F, off)
        prev = off
    state.append((int(r[0]), F, r))
cache = {}
for f in map(int, sys.argv[1:]):
    o = hdr + f * rec
    idx = np.asarray(raw[o + 776:o + 776 + W * H]).reshape(H, W)[:200, :320]
    cand = [s for s in state if s[0] <= f][-4:]
    ok = np.zeros(200, bool)
    for fr, Fs, r in cand:
        P = int(r[10])
        for dp in (0, -2, -4):
            for k in [None] + list(range(1, 56)):
                key = (P + dp, k)
                if key not in cache:
                    cache[key] = LM.render((P + dp) & 0xffff, [199] * 320,
                                           split=None if k is None else (k, (P + dp - 2) & 0xffff))[0].reshape(-1)
                scr = FM.composite(Fs, cache[key], labels.get(int(r[14])), max(int(r[6]), 0))
                ok |= (scr == idx).all(1)
    bad = np.nonzero(~ok)[0]
    print('frame %d: rows explained %d/200%s' % (f, ok.sum(), (', unexplained rows %s' % bad.tolist()) if len(bad) else ''))

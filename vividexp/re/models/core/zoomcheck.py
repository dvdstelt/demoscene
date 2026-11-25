"""Check the zoom model against every record of the five zoom sections: each record must equal the model frame drawn
after that record's retrace, or the one before it (the emulated CPU had not finished drawing / copying), or a row
split between the two (the copy to VGA was under way when the record was taken).

usage: python3 models/core/zoomcheck.py full.frames full.script
"""
import os
import sys
from collections import Counter

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))
import core_model as cm                          # noqa: E402
from sheet import open_frames, indexed           # noqa: E402

raw, n = open_frames(sys.argv[1])
script = cm.read_script(sys.argv[2], 1)
res = Counter()
prev = {'vga': None}
bad = []


def on_record(k, c, waiting):
    cur = c.vga.reshape(200, 320).copy()
    if waiting is None or waiting[0] != 'zoom':
        prev['vga'] = cur
        return
    px, _ = indexed(raw, k)
    z = waiting[2]['z']
    if (px == cur).all():
        res['new'] += 1
    elif prev['vga'] is not None and (px == prev['vga']).all():
        res['previous'] += 1
    else:
        ok = False
        if prev['vga'] is not None:
            for r in range(201):
                if (px[:r] == cur[:r]).all() and (px[r:] == prev['vga'][r:]).all():
                    res['split'] += 1
                    ok = True
                    break
        if not ok:
            res['other'] += 1
            bad.append((k, z, int((px != cur).sum())))
    prev['vga'] = cur


cm.replay(script, min(5505, n - 1), on_record)
print(res)
print(bad[:20])

# the odd record: a split inside the copy (rep movsd) at a byte offset?
if bad:
    k0 = bad[0][0]
    keep = {}

    def grab(k, c, waiting):
        if k in (k0 - 1, k0):
            keep[k] = c.vga.copy()
    cm.replay(script, k0, grab)
    px, _ = indexed(raw, k0)
    px = px.reshape(-1)
    for o in range(64000 + 1):
        if (px[:o] == keep[k0][:o]).all() and (px[o:] == keep[k0 - 1][o:]).all():
            print('record %d: new frame up to byte %d (row %d col %d), previous frame after it' % (k0, o, o // 320, o % 320))
            break

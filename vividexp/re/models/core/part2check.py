"""Part II's core-drawn records: the flash between the water and the first bump (DAC only: the screen holds the
water's last frame) and the greets picture at the end (VGA and DAC).

usage: python3 models/core/part2check.py full.frames full.script
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))
import core_model as cm                          # noqa: E402
from sheet import open_frames, indexed           # noqa: E402

raw, n = open_frames(sys.argv[1])
script = cm.read_script(sys.argv[2], 2)
first = script[0][0]
# part II's lines are logged with the record numbers of the whole run: shift them so that replay() starts at 0
off = first - 1
script = [(f - off, ln, no, t) for f, ln, no, t in script]
res = {'flash_dac_ok': 0, 'flash_dac_bad': 0, 'greets_ok': 0, 'greets_bad': 0}
bad = []
g0 = next(f for f, ln, no, t in script if t[2] == 'PutPCXSYNC')
w0 = next(f for f, ln, no, t in script if t[2] == 'WaitSYNC')
w1 = next(f for f, ln, no, t in script if t[2] == 'RunBUMPEFEK')


def on_record(k, c, waiting):
    kk = k + off
    if kk >= n:
        return
    if w0 + 1 <= k <= w1:
        _, pal = indexed(raw, kk)
        ok = (pal == c.dac).all()
        res['flash_dac_ok' if ok else 'flash_dac_bad'] += 1
        if not ok:
            bad.append(('flash', kk, int((pal != c.dac).sum())))
    if k >= g0:
        px, pal = indexed(raw, kk)
        ok = (pal == c.dac).all() and (px.reshape(-1) == c.vga).all()
        res['greets_ok' if ok else 'greets_bad'] += 1
        if not ok:
            bad.append(('greets', kk, int((pal != c.dac).sum()), int((px.reshape(-1) != c.vga).sum())))


# the DAC is black when part II starts (main 0x4003b7 sets it after part I)
cm.replay(script, n - 1 - off, on_record)
print(res)
print(bad[:20])

"""Checks the exit fades of p6 (0010:3D22) and p7 (0010:33BE) against full.frames: every DAC in the fade must be
(v * L) >> 8 of the DAC read at the start, for a level L = 256 - 7 n (p6) or 256 - n (p7); prints which n each
captured frame shows (several steps per frame in the emulator, one per retrace on a real VGA).

usage: python3 models/core/exit_fades.py     (from dope/re, inside the toolbox)"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, RE)
import sheet

raw, W, H, rec, hdr, n = sheet.open_frames(os.path.join(RE, 'full.frames'))


def dac(i):
    o = hdr + i * rec
    return np.frombuffer(bytes(raw[o + 8:o + 776]), np.uint8).astype(np.int32)


for name, f0, f1, step in (('p6', 29570, 29597, 7), ('p7', 32030, 32119, 1)):
    base = None
    levels = list(range(256, -1, -step))
    out = []
    for i in range(f0, f1 + 1):
        d = dac(i)
        if base is None or not any(np.array_equal(d, (base * L) >> 8) for L in levels):
            if base is not None and out:
                break
            base = d                      # still before the fade: the palette the fade starts from
            continue
        L = next(L for L in levels if np.array_equal(d, (base * L) >> 8))
        if L != 256:
            out.append((i, (256 - L) // step))
    print(name, 'frame: step n =', ' '.join('%d:%d' % x for x in out))
    if out:
        print('   %d frames for steps %d..%d (%.1f steps per frame)' % (
            len(out), out[0][1], out[-1][1], (out[-1][1] - out[0][1]) / max(1, len(out) - 1)))
    print('   frame after the last one: DAC all zero:', not dac(out[-1][0] + 1).any() if out else None)

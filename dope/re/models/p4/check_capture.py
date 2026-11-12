"""Compare whole p4 frames of the capture (full.frames) with the models: fire (fire_model.py), landscape
(land_model.py), composite with dither and label, and the palette 1999 with the fade value of the last odd IRQ.

The state per frame (offsets, [2113], label width, fade) comes from the hooked run (models/p4/out/*.log); the
pixels and the DAC come only from the models. A frame can be torn: the frame loop does not wait for the retrace
and the capture takes VGA memory at the end of the frame, so a composite in progress shows two states.
usage (from dope/re): python3 models/p4/check_capture.py [frame ...]
"""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, RE)
import fire_model as FM  # noqa: E402
import land_model as LM  # noqa: E402
import sheet  # noqa: E402

OUT = os.path.join(HERE, 'out')
img = FM.img


def main():
    frames = [int(x) for x in sys.argv[1:]] or list(range(13660, 21082, 97))
    rows = [l.split() for l in open(os.path.join(OUT, 'frames.log'))]
    irqs = [l.split() for l in open(os.path.join(OUT, 'retrace.log'))]
    ini = np.load(os.path.join(OUT, 'init.npz'))
    lbm = np.load(os.path.join(OUT, 'lbm.npz'))['area']
    pic3 = ini['pic']
    pal3 = ini['pal157e']
    pal_land = FM.seg(0x0010, 48, 0x20d2)
    pal_cheq = lbm[0x10:0x40]
    cheq = lbm[0x310:0x310 + 64000]
    labels = {}
    for s in (0x273, 0x36e):
        labels[s + 0xf64] = FM.seg(s, 70 * 44, 0x30a).reshape(44, 70)
    raw, W, H, rec, hdr, n = sheet.open_frames(os.path.join(RE, 'full.frames'))
    # fire state after each recompute, indexed by the log row
    F = np.zeros(32000, np.uint8)
    prev = 0
    state = []                    # per log row: (frame, F, row)
    for r in rows:
        off = int(r[11])
        if off != prev:
            F = FM.fire_step(F, off)
            prev = off
        state.append((int(r[0]), F, r))
    land_start = next(int(r[0]) for r in rows if int(r[10]) and int(r[14]) == 0x36e + 0xf64) if False else None
    tot = 0
    for f in frames:
        cand = [s for s in state if s[0] <= f]
        if not cand:
            continue
        fr, F, r = cand[-1]
        seg196b = int(r[14])
        P = int(r[10])
        lw = int(r[6])
        # which background: the landscape runs from the first call after [1969]-reset to 0 with the grey palette
        o = hdr + f * rec
        dac = np.frombuffer(bytes(raw[o + 8:o + 776]), np.uint8).reshape(256, 3)
        idx = np.asarray(raw[o + 776:o + 776 + W * H]).reshape(H, W)[:200, :320]
        # palette: last odd IRQ before the end of frame f
        js = [j for j, q in enumerate(irqs) if int(q[0]) <= f and int(q[2]) & 1]
        a = int(irqs[js[-1] + 1][4]) if js and js[-1] + 1 < len(irqs) else None
        best = None
        for name, pal16, bg in (('picture', pal3, pic3), ('landscape', pal_land, None), ('chequerboard', pal_cheq, cheq)):
            if a is None:
                continue
            p = FM.palette(pal16.astype(np.int32), a)
            dok = int((p == dac[:240]).all(1).sum())
            if bg is None:
                bg = LM.render(P, [199] * 320)[0].reshape(-1)
            scr = FM.composite(F, bg, labels.get(seg196b), max(lw, 0))
            pok = int((scr == idx).sum())
            if best is None or (dok, pok) > best[1:3]:
                best = (name, dok, pok)
        lab = (dac[240:] == FM.seg(0x273, 48, 0x2da).reshape(16, 3)).all()
        print('frame %d: %-12s A=%5s DAC 0..239 equal %3d/240, labels pal %s, pixels equal %5d/64000' % (
            f, best[0], a, best[1], lab, best[2]))
        tot += best[2] == 64000


if __name__ == '__main__':
    main()

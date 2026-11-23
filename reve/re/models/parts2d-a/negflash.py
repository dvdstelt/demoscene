"""Entries 5 and 6 (init 0x204aef, frames 0x204b2c / 0x204bd8): GFX\\PULSE.RAW flashing, plain and through
GFX\\NEGATIF.PAL, with black frames between the flashes; then (entry 6) shown steadily.

usage: python3 negflash.py [--lag 0]

Replays the frame routine's counter (per drawn frame; the capture draws one frame per refresh) from the image's
initial values and compares every record of the two entries with the model (indices and DAC). Entry 5's init takes
more than a refresh (it unpacks PULSE.RAW), so its first frame is drawn at once, before the retrace of record 2737,
and frame k is record 2737 + k (k = 0..399); entry 6's init is empty, so its first frame waits for the retrace of
record 3137 and shows in record 3138; record 3137 still shows entry 5's last frame. Record 3193 has entry 6's pixels through WIZ.RAW's
palette (entry 7's init sets the DAC before its first frame): the comparison stops at 3192.
"""
import argparse

import numpy as np

from common import frames, indexed, raw, file, rep

SET5, SET6, SET7 = 2737, 3137, 3193   # full.log: retrace routine set at frame N (entries 5, 6, 7)


def flash_sequence(nframes):
    """What the frame routine 0x204b2c shows at its k-th call (k = 0, 1, ...): 'black', 'plain' or 'negative'."""
    toggle, count, step = rep.rd32(0x204bcc), rep.rd32(0x204bd0), rep.rd32(0x204bd4)   # 0, 16000, 1000
    out = []
    for _ in range(nframes):
        count -= step
        if count > 0:
            out.append('black')
            continue
        count = 0x3e80
        step += 5
        toggle ^= 1
        out.append('plain' if toggle else 'negative')
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lag', type=int, default=0, help='record of the first frame = SET5 + lag')
    a = ap.parse_args()
    raw_frames, n = frames()
    pulse, pal = raw('GFX\\PULSE.RAW')
    neg = np.frombuffer(file('GFX\\NEGATIF.PAL'), np.uint8)
    pal6 = pal >> 2
    images = {'black': np.zeros_like(pulse), 'plain': pulse, 'negative': neg[pulse]}
    seq = flash_sequence(SET6 - SET5)
    bad = 0
    flashes = []
    for k, what in enumerate(seq + ['black'] + ['plain'] * (SET7 - SET6 - 1)):
        r = SET5 + a.lag + k
        px, dac = indexed(raw_frames, r)
        ok = (px == images[what]).all() and (dac == pal6).all()
        if what != 'black' and k < len(seq):
            flashes.append((r, what))
        if not ok:
            bad += 1
            print('record %d: model %s, capture differs (%d pixels, dac %s)' % (
                r, what, (px != images[what]).sum(), (dac == pal6).all()))
    print('records %d..%d compared, %d differ; %d flashes in entry 5:' % (SET5 + a.lag, SET7 - 1 + a.lag, bad, len(flashes)))
    print(' '.join('%d%s' % (r, 'n' if w == 'negative' else 'p') for r, w in flashes))
    gaps = np.diff([r for r, _ in flashes])
    print('refreshes between flashes:', ' '.join(map(str, gaps)))


if __name__ == '__main__':
    main()

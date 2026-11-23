"""Pixel check of the SurRender port (src/inside/engine.*) against the capture, for the logo part.

    python3 tools/engine_check.py --shots DIR R1 R2 ... [--offsets 0] [--diff OUT.png]   (in the toolbox)

Render the shots in the original's mode at the original's size first (without the palette fades, the title with
the original's pixels):

    INSIDE_ENGINE=original ./build/inside-remake --nofade --original-art --refshots R1,R2,... --shotdir DIR \
        --shotsize 320x200

The capture shows the frame drawn after a varying number of ticks (mostly the state of the refresh before; the slow
machine skips some tick states altogether), so compare per record with --records --offsets -2,-1,0: each record
against the shots of the refreshes before it.

For every refresh R the remake's DIR/shot_r<R>.png (320 x 200) is compared with record R + offset of the capture
(inside/re/full.frames, through logo.pal, or the record's DAC palette with --dac). Pixels are equal when every channel differs by at most 1
(8-bit rounding of the 6-bit palette). Two regions: "3d" = the pixels the mask bitmap (logo/inmask.raw at (31, 9),
253 x 147, index 0 transparent) and the title (logo/inside.raw at (34, 13)) leave alone, where the picture is the
engine's render alone; "mask" = the rest (the type 4 bitmap drawn through the fit of inmask.shd's rows, and the
title). With several offsets the best one is
reported per refresh (the capture's frame latency varies by a refresh). --diff writes the last refresh's capture,
remake and difference (white = different) side by side, 3x.
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..', 're')
sys.path.insert(0, RE)

ap = argparse.ArgumentParser()
ap.add_argument('refreshes', nargs='+', type=float)
ap.add_argument('--shots', required=True)
ap.add_argument('--frames', default=os.path.join(RE, 'full.frames'))
ap.add_argument('--offsets', default='0')
ap.add_argument('--diff')
ap.add_argument('--records', action='store_true', help='the numbers are records; compare each with the shots at '
                'record + offset (the frame the capture shows was drawn after a varying number of ticks)')
ap.add_argument('--dac', action='store_true', help="the capture's colours through the record's DAC palette "
                '(default: through logo/logo.pal, so that the palette fades and their rounding stay out)')
a = ap.parse_args()

data = np.memmap(a.frames, dtype=np.uint8, mode='r')
assert bytes(data[:8]) == b'INSDFRM1', 'not an Inside capture'
W, H, RECORD, HEADER = struct.unpack('<IIII', bytes(data[8:24]))


import datfile  # noqa: E402

DAT = datfile.Dat(os.path.join(RE, '..', 'original', 'INSIDE.DAT'))
BASE = np.minimum(np.frombuffer(DAT.get('logo/logo.pal'), np.uint8)[:768].reshape(256, 3).astype(np.int32), 63)


def capture(k):
    o = HEADER + k * RECORD
    px = np.asarray(data[o:o + W * H]).reshape(H, W)
    pal = np.asarray(data[o + W * H:o + W * H + 768]).reshape(256, 3).astype(np.int32) if a.dac else BASE
    return ((pal * 255 + 31) // 63)[px]


def mask_region():
    d = DAT
    m = np.frombuffer(d.get('logo/inmask.raw'), np.uint8)[:253 * 147].reshape(147, 253)
    t = np.frombuffer(d.get('logo/inside.raw'), np.uint8)[:249 * 139].reshape(139, 249)
    r = np.zeros((200, 320), bool)
    r[9:9 + 147, 31:31 + 253] = m != 0
    r[13:13 + 139, 34:34 + 249] |= t != 0   # the title (from 4:32; a redraw unless --original-art)
    return r


region = mask_region()
offsets = [int(x) for x in a.offsets.split(',')]
tot3 = same3 = totm = samem = 0
for r in a.refreshes:
    best = None
    for off in offsets:
        sr_, rec = (r + off, int(round(r))) if a.records else (r, int(round(r)) + off)
        path = os.path.join(a.shots, 'shot_r%07.2f.png' % sr_)
        if not os.path.exists(path):
            continue
        shot = np.asarray(Image.open(path).convert('RGB'), np.int32)
        assert shot.shape == (200, 320, 3), 'render the shots at --shotsize 320x200'
        cap = capture(rec)
        eq = (np.abs(shot - cap) <= 1).all(axis=2)
        s3 = int(eq[~region].sum())
        if best is None or s3 > best[0]:
            mdiff = np.abs(shot - cap)[region].mean()
            best = (s3, off, int(eq[region].sum()), mdiff, cap, eq, shot)
    s3, off, sm, mdiff, cap, eq, shot = best
    n3, nm = int((~region).sum()), int(region.sum())
    tot3 += n3
    same3 += s3
    totm += nm
    samem += sm
    print('%s %.2f (%s %+d): 3d %d of %d equal (%.3f %%), %d differ; mask region %.2f %% equal, '
          'mean abs diff %.2f' % ('record' if a.records else 'refresh', r, 'shot' if a.records else 'record', off, s3, n3, 100.0 * s3 / n3, n3 - s3, 100.0 * sm / nm, mdiff))
    if a.diff:
        d = np.where(eq[..., None], 0, 255) * np.ones(3, np.int32)
        img = np.concatenate([cap, shot, d], axis=1).astype(np.uint8)
        Image.fromarray(img).resize((960 * 3, 600), Image.NEAREST).save(a.diff)
print('total 3d: %d of %d equal (%.3f %%), %d differ; mask region: %d of %d equal (%.3f %%)' % (
    same3, tot3, 100.0 * same3 / tot3, tot3 - same3, samem, totm, 100.0 * samem / totm))

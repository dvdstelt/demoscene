"""Side-by-side check of remake frames against the original's capture.

    python3 tools/compare.py OUT.jpg --shots DIR R1 R2 ...      (run inside the toolbox: numpy + PIL)

R are refreshes of the capture (inside/re/full.frames record R: the picture on screen from program time R / 70.086 s,
VGA memory through the DAC palette; see inside/re/NOTES.md). The remake renders refresh R with
`inside-remake --refshots R1,R2,... --shotdir DIR`, which writes DIR/shot_r<R>.png (R with two decimals). Left = the
capture, right = the remake; one row per refresh, labelled with the record, the music position and the frame
routine.
Options:
  --frames FILE      capture (default ../../re/full.frames relative to this script; full.ticks next to it)
  --panel WxH        size of each half (default 480x360)
  --crop X,Y,W,H     zoom both into this rectangle of VGA coordinates (320x200 space)
  --score            print the mean absolute RGB difference (0..255) per refresh, remake area-downscaled to 320x200
  --smooth           show the original with bilinear instead of nearest-neighbour scaling
  --offset N         compare the remake's refresh R with the capture's record R + N
"""
import argparse
import os
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))

ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('refreshes', nargs='+', type=float)
ap.add_argument('--shots', required=True)
ap.add_argument('--frames', default=os.path.join(HERE, '..', '..', 're', 'full.frames'))
ap.add_argument('--panel', default='480x360')
ap.add_argument('--crop')
ap.add_argument('--score', action='store_true')
ap.add_argument('--smooth', action='store_true')
ap.add_argument('--offset', type=int, default=0)
a = ap.parse_args()

pw, ph = map(int, a.panel.split('x'))
data = np.memmap(a.frames, dtype=np.uint8, mode='r')
assert bytes(data[:8]) == b'INSDFRM1', 'not an Inside capture'
W, H, RECORD, HEADER = struct.unpack('<IIII', bytes(data[8:24]))
nrec = (len(data) - HEADER) // RECORD
ticks = {}
for line in open(os.path.splitext(a.frames)[0] + '.ticks'):
    if line.startswith('#'):
        continue
    f = line.split()
    ticks[int(f[0])] = (int(f[2]), int(f[3]), f[7])


def capture(k):
    o = HEADER + k * RECORD
    px = np.asarray(data[o:o + W * H]).reshape(H, W)
    pal = np.asarray(data[o + W * H:o + W * H + 768]).reshape(256, 3).astype(np.uint32)
    return ((pal * 255 + 31) // 63).astype(np.uint8)[px]


crop = tuple(map(float, a.crop.split(','))) if a.crop else (0, 0, 320, 200)
try:
    font = ImageFont.truetype('DejaVuSans-Bold.ttf', 16)
except OSError:
    font = ImageFont.load_default()
sheet = Image.new('RGB', (2 * pw, ph * len(a.refreshes)))
d = ImageDraw.Draw(sheet)
for i, r in enumerate(a.refreshes):
    k = int(round(r)) + a.offset
    y = i * ph
    if 0 <= k < nrec:
        orig = Image.fromarray(capture(k))
        cx, cy, cw, chh = crop
        orig = orig.crop((int(cx), int(cy), int(cx + cw), int(cy + chh)))
        orig = orig.resize((pw, ph), Image.BILINEAR if a.smooth else Image.NEAREST)
        sheet.paste(orig, (0, y))
    path = os.path.join(a.shots, 'shot_r%07.2f.png' % r)
    score = None
    if os.path.exists(path):
        rem = Image.open(path).convert('RGB')
        rw, rh = rem.size
        if a.score and 0 <= k < nrec:
            small = np.asarray(rem.resize((320, 200), Image.BOX), dtype=np.int32)
            score = np.abs(small - capture(k).astype(np.int32)).mean()
        cx, cy, cw, chh = crop
        rem = rem.crop((int(cx * rw / 320), int(cy * rh / 200), int((cx + cw) * rw / 320), int((cy + chh) * rh / 200)))
        sheet.paste(rem.resize((pw, ph), Image.LANCZOS), (pw, y))
    else:
        d.text((pw + 10, y + 10), 'missing ' + os.path.basename(path), fill=(255, 80, 80), font=font)
    t = ticks.get(k)
    label = 'record %d  %s' % (k, '%d:%02d  frame fn %s' % t if t else '')
    if score is not None:
        label += '  diff %.1f' % score
        print('refresh %.2f record %d: mean abs diff %.2f' % (r, k, score))
    d.text((6, y + 4), label, fill=(255, 255, 0), font=font)
    d.text((pw + 6, y + 4), 'remake %.2f' % r, fill=(255, 255, 0), font=font)
sheet.save(a.out, quality=90)
print('wrote', a.out)

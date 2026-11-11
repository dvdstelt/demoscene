"""Side-by-side check of remake frames against the original's capture.

    python3 tools/compare.py OUT.jpg --shots DIR T1 T2 ...      (run inside the toolbox: numpy + PIL)

For every tick T: left = record T of the original capture (paper/re/full.frames: per tick 64000 bytes VGA + 768
bytes 6-bit palette, the last whole frame drawn in that tick), right = DIR/shot_<T>.png from
`paper --shots T1,T2,... --shotdir DIR`. One row per tick.
Options:
  --frames FILE      capture (default ../re/full.frames relative to this script's folder)
  --panel WxH        size of each half (default 480x360)
  --crop X,Y,W,H     zoom both into this rectangle of VGA coordinates (320x200 space)
  --score            print the mean absolute RGB difference (0..255) per tick, remake area-downscaled to 320x200
  --smooth           show the original with bilinear instead of nearest-neighbour scaling
"""
import argparse
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

FR = 64000 + 768
HERE = os.path.dirname(os.path.abspath(__file__))

ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('ticks', nargs='+', type=float)
ap.add_argument('--shots', required=True)
ap.add_argument('--frames', default=os.path.join(HERE, '..', '..', 're', 'full.frames'))
ap.add_argument('--panel', default='480x360')
ap.add_argument('--crop')
ap.add_argument('--score', action='store_true')
ap.add_argument('--smooth', action='store_true')
a = ap.parse_args()

pw, ph = map(int, a.panel.split('x'))
data = np.memmap(a.frames, dtype=np.uint8, mode='r')
crop = tuple(map(float, a.crop.split(','))) if a.crop else (0, 0, 320, 200)
try:
    font = ImageFont.truetype('DejaVuSans-Bold.ttf', 16)
except OSError:
    font = ImageFont.load_default()


def original(t):
    k = int(t)
    f = np.asarray(data[k * FR:(k + 1) * FR])
    pal = (f[64000:].reshape(256, 3).astype(np.uint16) * 255 // 63).astype(np.uint8)
    return Image.fromarray(pal[f[:64000]].reshape(200, 320, 3))


def shot(t):
    n = os.path.join(a.shots, 'shot_%05d.png' % int(t)) if t == int(t) else os.path.join(a.shots, 'shot_%08.2f.png' % t)
    return Image.open(n).convert('RGB')


def cut(im, smooth):
    sx, sy = im.width / 320.0, im.height / 200.0
    x, y, w, h = crop
    box = (x * sx, y * sy, (x + w) * sx, (y + h) * sy)
    return im.resize((pw, ph), Image.BILINEAR if smooth else Image.NEAREST, box=box) if im.width == 320 else \
        im.resize((pw, ph), Image.LANCZOS, box=box)


sheet = Image.new('RGB', (pw * 2, ph * len(a.ticks)), (0, 0, 0))
d = ImageDraw.Draw(sheet)
for i, t in enumerate(a.ticks):
    o, r = original(t), shot(t)
    sheet.paste(cut(o, a.smooth), (0, i * ph))
    sheet.paste(cut(r, True), (pw, i * ph))
    for x, label in ((0, 'original %g' % t), (pw, 'remake %g' % t)):
        d.text((x + 7, i * ph + 5), label, fill=(0, 0, 0), font=font)
        d.text((x + 6, i * ph + 4), label, fill=(255, 255, 0), font=font)
    if a.score:
        rs = np.asarray(r.resize((320, 200), Image.BOX), dtype=np.float32)
        print('tick %g  mean abs diff %.2f' % (t, np.abs(rs - np.asarray(o, dtype=np.float32)).mean()))
sheet.save(a.out, quality=90)
print('wrote', a.out)

"""Side-by-side check of remake frames against the original's capture.

    python3 tools/compare.py OUT.jpg --shots DIR T1 T2 ...      (run inside the toolbox: numpy + PIL)

T are music times in seconds (0 = the start of the music, as in the remake). Left = the record of the original's
capture (jizz/re/full.frames: per timer tick the visible 320x200 page, 16-bit 5:6:5) whose tick ends closest to T
(full.ticks gives the time of each tick; music time 0 = the end of tick 434), right = DIR/shot_<T>.png from
`jizz-remake --shots T1,T2,... --shotdir DIR`. One row per time.
Options:
  --frames FILE      capture (default ../re/full.frames relative to this script's folder; full.ticks next to it)
  --panel WxH        size of each half (default 480x360)
  --crop X,Y,W,H     zoom both into this rectangle of VGA coordinates (320x200 space)
  --score            print the mean absolute RGB difference (0..255) per time, remake area-downscaled to 320x200
  --smooth           show the original with bilinear instead of nearest-neighbour scaling
  --offset S         add S seconds to T for the capture (to look for a time offset)
"""
import argparse
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER, RECORD = 32, 128000
MUSIC_TICK = 434  # the first player tick: music time 0 is its end

ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('times', nargs='+', type=float)
ap.add_argument('--shots', required=True)
ap.add_argument('--frames', default=os.path.join(HERE, '..', '..', 're', 'full.frames'))
ap.add_argument('--panel', default='480x360')
ap.add_argument('--crop')
ap.add_argument('--score', action='store_true')
ap.add_argument('--smooth', action='store_true')
ap.add_argument('--offset', type=float, default=0.0)
a = ap.parse_args()

pw, ph = map(int, a.panel.split('x'))
data = np.memmap(a.frames, dtype=np.uint8, mode='r')
nrec = (len(data) - HEADER) // RECORD
ticks = np.loadtxt(os.path.splitext(a.frames)[0] + '.ticks', comments='#', usecols=(0, 2))
tend = ticks[:, 1] - ticks[ticks[:, 0] == MUSIC_TICK][0, 1]  # music time at the end of each tick
crop = tuple(map(float, a.crop.split(','))) if a.crop else (0, 0, 320, 200)
try:
    font = ImageFont.truetype('DejaVuSans-Bold.ttf', 16)
except OSError:
    font = ImageFont.load_default()


def record(t):
    return int(min(nrec - 1, np.argmin(np.abs(tend - t))))


def original(t):
    k = record(t)
    px = np.frombuffer(bytes(data[HEADER + k * RECORD:HEADER + (k + 1) * RECORD]), dtype='<u2').reshape(200, 320)
    r = ((px >> 11) & 31).astype(np.uint16) * 255 // 31
    g = ((px >> 5) & 63).astype(np.uint16) * 255 // 63
    b = (px & 31).astype(np.uint16) * 255 // 31
    return Image.fromarray(np.dstack([r, g, b]).astype(np.uint8)), k


def shot(t):
    return Image.open(os.path.join(a.shots, 'shot_%07.3f.png' % t)).convert('RGB')


def cut(im, smooth):
    sx, sy = im.width / 320.0, im.height / 200.0
    x, y, w, h = crop
    box = (x * sx, y * sy, (x + w) * sx, (y + h) * sy)
    return im.resize((pw, ph), Image.BILINEAR if smooth else Image.NEAREST, box=box) if im.width == 320 else \
        im.resize((pw, ph), Image.LANCZOS, box=box)


sheet = Image.new('RGB', (pw * 2, ph * len(a.times)), (0, 0, 0))
d = ImageDraw.Draw(sheet)
for i, t in enumerate(a.times):
    (o, k), r = original(t + a.offset), shot(t)
    sheet.paste(cut(o, a.smooth), (0, i * ph))
    sheet.paste(cut(r, True), (pw, i * ph))
    for x, label in ((0, 'original %.3f s (tick %d)' % (t + a.offset, k)), (pw, 'remake %.3f s' % t)):
        d.text((x + 7, i * ph + 5), label, fill=(0, 0, 0), font=font)
        d.text((x + 6, i * ph + 4), label, fill=(255, 255, 0), font=font)
    if a.score:
        rs = np.asarray(r.resize((320, 200), Image.BOX), dtype=np.float32)
        print('%.3f s (tick %d)  mean abs diff %.2f' % (t, k, np.abs(rs - np.asarray(o, dtype=np.float32)).mean()))
sheet.save(a.out, quality=90)
print('wrote', a.out)

"""Side-by-side check of remake frames against the original's capture.

    python3 tools/compare.py OUT.jpg --shots DIR T1 T2 ...      (run inside the toolbox: numpy + PIL)

T are music times in seconds (t = xmpGetTimer / 1,193,182, as in the remake). Left = the record of the original's
capture (clone/re/full.frames, see clone/re/README.md and NOTES.md: one record per timer tick, the 320x200 picture
on screen at the end of the tick, RGB) whose picture was drawn with the timer value closest to T: the frame on
screen at the end of tick k is flip full.ticks[k] column 9, drawn with the timer full.flips column 4 (main reads the
timer after each frame and the next frame shows the state integrated up to it, notes-core 5.2). The remake draws
the frame at T with the state at timer T, so equal times compare equal states. Right = DIR/shot_<T>.png from
`clone-remake --shots T1,T2,... --shotdir DIR`. One row per time; the label gives the capture's tick, flip, the
music time the frame was drawn with, the music position order:row and main's part number.
Options:
  --frames FILE      capture (default ../../re/full.frames relative to this script; full.ticks, full.flips next to it)
  --panel WxH        size of each half (default 480x360)
  --crop X,Y,W,H     zoom both into this rectangle of VGA coordinates (320x200 space)
  --score            print the mean absolute RGB difference (0..255) per time, remake area-downscaled to 320x200
  --smooth           show the original with bilinear instead of nearest-neighbour scaling
  --offset S         add S seconds to T for the capture (to look for a time offset)
  --tick             T are capture ticks instead of music times (left = that record; right = the shot of the
                     music time it was drawn with, rounded to 3 decimals as --shots names it)
"""
import argparse
import os
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PIT_HZ = 1193182.0

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
ap.add_argument('--tick', action='store_true')
a = ap.parse_args()

pw, ph = map(int, a.panel.split('x'))
data = np.memmap(a.frames, dtype=np.uint8, mode='r')
assert bytes(data[:8]) == b'CLONFRM1', 'not a per-tick clone capture'
W, H, _bits, RECORD, HEADER, _mode = struct.unpack('<IIIIII', bytes(data[8:32]))
nrec = (len(data) - HEADER) // RECORD
base = os.path.splitext(a.frames)[0]
# .ticks: tick pit_count time_s order row timer part sync_index flips stage eip
ticks = np.loadtxt(base + '.ticks', comments='#', usecols=(0, 3, 4, 6, 8), dtype=np.int64)
tick_row = {int(r[0]): r for r in ticks}
# .flips: flip tick time_s timer t70 part order row
flip_timer = {}
for line in open(base + '.flips'):
    if line.startswith('#'):
        continue
    f = line.split()
    flip_timer[int(f[0])] = int(f[3])
NONE = -(1 << 40)


def drawn_time(k):
    """Music time the picture on screen at the end of tick k was drawn with (None before the first timer read)."""
    r = tick_row.get(k)
    if r is None:
        return None
    tm = flip_timer.get(int(r[4]))
    return None if tm is None or tm == NONE else tm / PIT_HZ


drawn = np.array([np.nan if drawn_time(k) is None else drawn_time(k) for k in range(nrec)])
crop = tuple(map(float, a.crop.split(','))) if a.crop else (0, 0, 320, 200)
try:
    font = ImageFont.truetype('DejaVuSans-Bold.ttf', 16)
except OSError:
    font = ImageFont.load_default()


def record(t):
    return int(np.nanargmin(np.abs(drawn - t)))


def original(k):
    rec = np.asarray(data[HEADER + k * RECORD:HEADER + (k + 1) * RECORD]).reshape(H, W, 3)
    return Image.fromarray(np.array(rec))


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
    if a.tick:
        k = int(t)
        t = round(drawn[k], 3)
    else:
        k = record(t + a.offset)
    o, r = original(k), shot(t)
    sheet.paste(cut(o, a.smooth), (0, i * ph))
    sheet.paste(cut(r, True), (pw, i * ph))
    row = tick_row[k]
    left = 'tick %d flip %d  %.3f s  %02x:%02x  part %d' % (k, row[4], drawn[k], row[1], row[2], row[3])
    for x, label in ((0, left), (pw, 'remake %.3f s' % t)):
        d.text((x + 7, i * ph + 5), label, fill=(0, 0, 0), font=font)
        d.text((x + 6, i * ph + 4), label, fill=(255, 255, 0), font=font)
    if a.score:
        rs = np.asarray(r.resize((320, 200), Image.BOX), dtype=np.float32)
        print('%.3f s (tick %d)  mean abs diff %.2f' % (t, k, np.abs(rs - np.asarray(o, dtype=np.float32)).mean()))
sheet.save(a.out, quality=90)
print('wrote', a.out)

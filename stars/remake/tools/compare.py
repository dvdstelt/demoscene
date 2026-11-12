"""Side-by-side check of remake frames against the original's capture.

    python3 tools/compare.py OUT.jpg --shots DIR T1 T2 ...      (run inside the toolbox: numpy + PIL)

T are music times in seconds (0 = the moment the player starts the GUS timers, as in the remake). Left = the record
of the original's capture (stars/re/full.frames, see stars/re/README.md and BRIEF.md: one record per displayed frame
at 70.0863 Hz, the 320x200 VGA page and the 6-bit DAC palette) that was on screen at T: the last record whose
start (full.ticks column 2, program time of the frame's line 0) is at or before T + 3.98976 s (the program time of
music time 0, notes-music.md 5.3). Right = DIR/shot_<T>.png from
`stars-remake --shots T1,T2,... --shotdir DIR`. One row per time; the label gives the capture frame, the music
position order:row and main's routine at that frame.
Options:
  --frames FILE      capture (default ../../re/full.frames relative to this script; full.ticks next to it)
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
MUSIC_START = 3.98976  # program time (s) of music time 0 in the capture

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
assert bytes(data[:8]) == b'STARFRM1', 'not a STARS capture'
W, H, _bits, RECORD, HEADER = np.frombuffer(bytes(data[8:28]), dtype='<u4')
W, H, RECORD, HEADER = int(W), int(H), int(RECORD), int(HEADER)
nrec = (len(data) - HEADER) // RECORD
ticks = np.loadtxt(os.path.splitext(a.frames)[0] + '.ticks', comments='#', usecols=(0, 1, 4, 5))
routines = np.loadtxt(os.path.splitext(a.frames)[0] + '.ticks', comments='#', usecols=(6,), dtype=str)
crop = tuple(map(float, a.crop.split(','))) if a.crop else (0, 0, 320, 200)
try:
    font = ImageFont.truetype('DejaVuSans-Bold.ttf', 16)
except OSError:
    font = ImageFont.load_default()


def record(t):
    k = int(np.searchsorted(ticks[:, 1], t + MUSIC_START, side='right')) - 1
    return max(0, min(nrec - 1, k))


def original(t):
    k = record(t)
    rec = np.frombuffer(bytes(data[HEADER + k * RECORD:HEADER + (k + 1) * RECORD]), dtype=np.uint8)
    idx = rec[:W * H].reshape(H, W)
    pal = (rec[W * H:W * H + 768].reshape(256, 3).astype(np.uint16) * 255 // 63).astype(np.uint8)
    return Image.fromarray(pal[idx]), k


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
    order, row = int(ticks[k, 2]), int(ticks[k, 3])
    left = 'original %.3f s  frame %d  %02x:%02x  %s' % (t + a.offset, k, order, row, routines[k])
    for x, label in ((0, left), (pw, 'remake %.3f s' % t)):
        d.text((x + 7, i * ph + 5), label, fill=(0, 0, 0), font=font)
        d.text((x + 6, i * ph + 4), label, fill=(255, 255, 0), font=font)
    if a.score:
        rs = np.asarray(r.resize((320, 200), Image.BOX), dtype=np.float32)
        print('%.3f s (frame %d)  mean abs diff %.2f' % (t, k, np.abs(rs - np.asarray(o, dtype=np.float32)).mean()))
sheet.save(a.out, quality=90)
print('wrote', a.out)

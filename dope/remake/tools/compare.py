"""Side-by-side check of remake frames against the original's capture.

    python3 tools/compare.py OUT.jpg --shots DIR T1 T2 ...      (run inside the toolbox: numpy + PIL)

T are music times in seconds, as the remake counts them (src/music.h: capture program time - 0.1104 s). Right =
DIR/shot_<T>.png from `dope-remake --shots T1,T2,... --shotdir DIR`. Left = the record of the original's capture
(dope/re/full.frames, see dope/re/README.md: one record per displayed frame at 70.0863 Hz, the visible VGA area and
the 6-bit DAC palette) that matches the remake at T: the remake writes it to DIR/shots.txt with every shot
(Ctx::captureFrame: the same frame grid up to p5's hand-over fade, then the capture's records of the three fades
that the emulator ran four times too fast, and the capture shifted by 48 / 74 / 266 records after them, see
src/timeline.cpp). Without shots.txt: the last record whose start is at or before T + --zero. One row per time; the
label gives the capture record, the music position order:row, the part and its drawn-frame count.
Options:
  --frames FILE      capture (default ../../re/full.frames relative to this script; full.ticks next to it)
  --panel WxH        size of each half (default 480x360)
  --crop X,Y,W,H     zoom both into this rectangle of VGA coordinates (320x200 space)
  --score            print the mean absolute RGB difference (0..255) per time, remake area-downscaled to 320x200
  --smooth           show the original with bilinear instead of nearest-neighbour scaling
  --offset R         add R records to the capture record (to look for a time offset)
  --zero S           capture program time of music time 0 when there is no shots.txt (default 0.1104)
"""
import argparse
import os
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
RATE = 25175000.0 / (800.0 * 449.0)  # records per second

ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('times', nargs='+', type=float)
ap.add_argument('--shots', required=True)
ap.add_argument('--frames', default=os.path.join(HERE, '..', '..', 're', 'full.frames'))
ap.add_argument('--panel', default='480x360')
ap.add_argument('--crop')
ap.add_argument('--score', action='store_true')
ap.add_argument('--smooth', action='store_true')
ap.add_argument('--offset', type=int, default=0)
ap.add_argument('--zero', type=float, default=0.1104)
a = ap.parse_args()

pw, ph = map(int, a.panel.split('x'))
data = np.memmap(a.frames, dtype=np.uint8, mode='r')
assert bytes(data[:8]) == b'DOPEFRM1', 'not a DOPE capture'
W, H, RECORD, HEADER, _, _ = struct.unpack('<IIIIII', bytes(data[8:32]))
nrec = (len(data) - HEADER) // RECORD
ticks = {}
for line in open(os.path.splitext(a.frames)[0] + '.ticks'):
    if line.startswith('#'):
        continue
    f = line.split()
    ticks[int(f[0])] = (float(f[1]), int(f[2]), int(f[3]), f[6], int(f[9]), f[10])
starts = np.array([ticks[k][0] for k in sorted(ticks)])
crop = tuple(map(float, a.crop.split(','))) if a.crop else (0, 0, 320, 200)
listed = {}
lp = os.path.join(a.shots, 'shots.txt')
if os.path.exists(lp):
    for line in open(lp):
        f = line.split()
        if len(f) >= 2:
            listed['%.3f' % float(f[0])] = int(f[1])
try:
    font = ImageFont.truetype('DejaVuSans-Bold.ttf', 16)
except OSError:
    font = ImageFont.load_default()


def record(t):
    k = listed.get('%.3f' % t)
    if k is None:
        k = int(np.searchsorted(starts, t + a.zero, side='right')) - 1
    return max(0, min(nrec - 1, k + a.offset))


def original(t):
    """(RGB 320x200 image or None for a text-mode record, record number)"""
    k = record(t)
    o = HEADER + k * RECORD
    w, h, flags, _ = struct.unpack('<HHHH', bytes(data[o:o + 8]))
    if not flags & 1 or not w or not h:
        return None, k
    pal = (np.frombuffer(bytes(data[o + 8:o + 776]), np.uint8).reshape(256, 3).astype(np.uint16) * 255 + 31) // 63
    idx = np.asarray(data[o + 776:o + 776 + W * H]).reshape(H, W)[:h, :w]
    im = Image.fromarray(pal[idx].astype(np.uint8))
    return (im if (w, h) == (320, 200) else im.resize((320, 200), Image.NEAREST)), k


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
    (o, k), r = original(t), shot(t)
    if o is not None:
        sheet.paste(cut(o, a.smooth), (0, i * ph))
    else:
        d.rectangle((0, i * ph, pw - 1, (i + 1) * ph - 1), fill=(24, 24, 40))
        d.text((pw // 2 - 40, i * ph + ph // 2), 'text mode', fill=(200, 200, 200), font=font)
    sheet.paste(cut(r, True), (pw, i * ph))
    tm, order, row, part, drawn, mode = ticks.get(k, (0, 0, 0, '?', 0, '?'))
    left = 'capture %d  %.3f s  %02x:%02x  %s' % (k, tm, order, row, part)
    for x, label in ((0, left), (pw, 'remake %.3f s' % t)):
        d.text((x + 7, i * ph + 5), label, fill=(0, 0, 0), font=font)
        d.text((x + 6, i * ph + 4), label, fill=(255, 255, 0), font=font)
    if a.score and o is not None:
        rs = np.asarray(r.resize((320, 200), Image.BOX), dtype=np.float32)
        print('%.3f s (record %d)  mean abs diff %.2f' % (t, k, np.abs(rs - np.asarray(o, dtype=np.float32)).mean()))
sheet.save(a.out, quality=90)
print('wrote', a.out)

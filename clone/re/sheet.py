"""Contact sheets of run_clone.py's .frames.

usage: python3 sheet.py full.frames out.jpg t1 t2 ...           these records (ticks; flips for a --flip-frames run)
       python3 sheet.py full.frames out.jpg --every 2.0 [--from-time S] [--to-time S] [--cols 6] [--scale 0.5]
                                                               one frame every 2 s of music time (labels: tick or
                                                               flip, music time, part, order:row)
       python3 sheet.py full.frames out.jpg --flips f1 f2 ...  the ticks at which these flips were on screen
                                                               (per-tick capture)

.frames: 32-byte header (b'CLONFRM1' = one record per timer tick, b'CLONFLP1' = one record per shown frame;
<IIIIII: width 320, height 200, bits 24, record bytes 192000, header bytes 32, video mode bits 32|15), then
320x200 RGB records. Per-tick capture: record i = tick i + tick0 (--tick0 for a run resumed from a snapshot).
Per-flip capture: record i = flip i + 1 + flip0 (flip0 from the .flips comment of a resumed run).
Music time = the player timer (PIT clocks since the music start) / 1193182: .ticks column 6 at the end of a tick,
.flips column 4 for the timer value a frame was drawn with.
"""
import argparse
import struct

import numpy as np
from PIL import Image, ImageDraw

PIT_HZ = 1193182
NONE = -(1 << 40)


def open_frames(path):
    """Returns raw, width, height, record bytes, header bytes, records, per_flip."""
    raw = np.memmap(path, dtype=np.uint8, mode='r')
    magic = bytes(raw[:8])
    assert magic in (b'CLONFRM1', b'CLONFLP1'), magic
    w, h, bits, rec, hdr, mode = struct.unpack('<IIIIII', bytes(raw[8:32]))
    n = (len(raw) - hdr) // rec
    return raw, w, h, rec, hdr, n, magic == b'CLONFLP1'


def to_rgb(raw, i, w, h, rec, hdr):
    return np.asarray(raw[hdr + i * rec:hdr + (i + 1) * rec]).reshape(h, w, 3)


def read_flips(path):
    """.flips rows: flip -> (tick, time, timer, t70, part, order, row); and the flip number before record 0."""
    rows, flip0 = {}, 0
    for line in open(path):
        if line.startswith('# resumed'):
            flip0 = int(line.split('= flip')[1]) - 1
        if line.startswith('#'): continue
        f = line.split()
        rows[int(f[0])] = (int(f[1]), float(f[2]), int(f[3]), float(f[4]), int(f[5]), int(f[6]), int(f[7]))
    return rows, flip0


def read_ticks(path):
    """.ticks rows: tick -> (time, order, row, timer, part, flips)."""
    rows = {}
    for line in open(path):
        if line.startswith('#'): continue
        f = line.split()
        rows[int(f[0])] = (float(f[2]), int(f[3]), int(f[4]), int(f[5]), int(f[6]), int(f[8]))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('frames')
    ap.add_argument('out')
    ap.add_argument('list', nargs='*', type=int)
    ap.add_argument('--flips', action='store_true', help='the numbers are flips (per-tick capture: show the first '
                    'tick at which each flip is on screen)')
    ap.add_argument('--every', type=float, help='seconds of music time between frames')
    ap.add_argument('--from-time', type=float, default=-1e9)
    ap.add_argument('--to-time', type=float, default=1e9)
    ap.add_argument('--tick0', type=int, default=0)
    ap.add_argument('--flip0', type=int)
    ap.add_argument('--cols', type=int, default=4)
    ap.add_argument('--scale', type=float, default=1.0)
    a = ap.parse_args()
    raw, w, h, rec, hdr, n, per_flip = open_frames(a.frames)
    base = a.frames.rsplit('.', 1)[0]
    flips, flip0 = read_flips(base + '.flips')
    if a.flip0 is not None:
        flip0 = a.flip0
    ticks = {} if per_flip else read_ticks(base + '.ticks')

    def label(k):
        if per_flip:
            r = flips.get(k)
            if not r: return 'f%d' % k
            tick, tm, timer, t70, part, order, row = r
            return 'f%d %.2fs p%d %02x:%02x' % (k, timer / PIT_HZ, part, order & 0xff, row & 0xff)
        r = ticks.get(k)
        if not r: return 't%d' % k
        tm, order, row, timer, part, fl = r
        return 't%d %.2fs p%d %02x:%02x' % (k, timer / PIT_HZ, part, order & 0xff, row & 0xff)

    if a.every:
        sel, nxt = [], None
        src = sorted(flips) if per_flip else sorted(ticks)
        for k in src:
            if per_flip:
                timer = flips[k][2]
            else:
                timer = ticks[k][3] if ticks[k][1] >= 0 else NONE
            if timer <= NONE // 2: continue
            mt = timer / PIT_HZ
            if mt < a.from_time or mt > a.to_time: continue
            if nxt is None: nxt = mt
            if mt >= nxt:
                sel.append(k)
                while nxt <= mt: nxt += a.every
    elif a.flips and not per_flip:
        first = {}
        for t in sorted(ticks):
            first.setdefault(ticks[t][5], t)
        sel = [first[f] for f in a.list if f in first]
    else:
        sel = a.list
    off = (lambda k: k - 1 - flip0) if per_flip else (lambda k: k - a.tick0)
    sel = [k for k in sel if 0 <= off(k) < n]
    cw, ch = int(w * a.scale), int(h * a.scale)
    sheet = Image.new('RGB', (cw * a.cols, ch * ((len(sel) + a.cols - 1) // a.cols)))
    d = ImageDraw.Draw(sheet)
    for i, k in enumerate(sel):
        im = Image.fromarray(to_rgb(raw, off(k), w, h, rec, hdr))
        if a.scale != 1.0: im = im.resize((cw, ch), Image.BILINEAR)
        x, y = (i % a.cols) * cw, (i // a.cols) * ch
        sheet.paste(im, (x, y))
        d.text((x + 3, y + 2), label(k), fill=(255, 255, 0))
    sheet.save(a.out, quality=85)
    print('wrote', a.out, len(sel), 'frames')


if __name__ == '__main__':
    main()

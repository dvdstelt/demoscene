"""Contact sheets of run_stars.py's .frames.

usage: python3 sheet.py full.frames out.jpg f1 f2 ...           these frames (retrace numbers)
       python3 sheet.py full.frames out.jpg --every 2.0 [--from-time S] [--to-time S] [--cols 6] [--scale 0.5]
                                                               one frame every 2 s (labels: frame, time, music, part)

.frames: 32-byte header (b'STARFRM1', <IIIIII: width 320, height 200, bits 8, record bytes 64768, header 32, 0),
then per vertical retrace 64000 bytes of mode 13h VGA memory + 768 bytes of 6-bit palette. Frame f starts at
.ticks column 2 (seconds); --frame0 is the frame number of the first record (a run resumed from a snapshot).
"""
import argparse
import struct

import numpy as np
from PIL import Image, ImageDraw


def open_frames(path):
    raw = np.memmap(path, dtype=np.uint8, mode='r')
    magic = bytes(raw[:8])
    assert magic == b'STARFRM1', magic
    w, h, bits, rec, hdr, _ = struct.unpack('<IIIIII', bytes(raw[8:32]))
    n = (len(raw) - hdr) // rec
    return raw, w, h, rec, hdr, n


def to_rgb(raw, f, w, h, rec, hdr):
    r = raw[hdr + f * rec:hdr + (f + 1) * rec]
    pal = (np.asarray(r[w * h:w * h + 768]).reshape(256, 3).astype(np.uint16) * 255 // 63).astype(np.uint8)
    return pal[np.asarray(r[:w * h])].reshape(h, w, 3)


def read_ticks(path):
    """Rows of .ticks: (frame, time, order, row, part)."""
    rows = []
    for line in open(path):
        if line.startswith('#'): continue
        f = line.split()
        rows.append((int(f[0]), float(f[1]), int(f[4]), int(f[5]), f[6]))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('frames')
    ap.add_argument('out')
    ap.add_argument('list', nargs='*', type=int)
    ap.add_argument('--every', type=float, help='seconds between frames (needs the .ticks file)')
    ap.add_argument('--ticksfile', help='default: frames path with .ticks')
    ap.add_argument('--frame0', type=int, default=0, help='frame number of the first record')
    ap.add_argument('--from-time', type=float, default=0.0)
    ap.add_argument('--to-time', type=float, default=1e9)
    ap.add_argument('--cols', type=int, default=4)
    ap.add_argument('--scale', type=float, default=1.0)
    a = ap.parse_args()
    raw, w, h, rec, hdr, n = open_frames(a.frames)
    tf = a.ticksfile or a.frames.rsplit('.', 1)[0] + '.ticks'
    rows = {}
    try:
        rows = {r[0]: r for r in read_ticks(tf)}
    except OSError:
        pass
    if a.every:
        sel, nxt = [], a.from_time
        for f in sorted(rows):
            tm = rows[f][1]
            if tm > a.to_time: break
            if tm >= nxt and 0 <= f - a.frame0 < n:
                sel.append(f)
                nxt += a.every
                while nxt <= tm: nxt += a.every
        fs = sel
    else:
        fs = [f for f in a.list if 0 <= f - a.frame0 < n]
    cw, ch = int(w * a.scale), int(h * a.scale * 1.2)          # 4:3
    sheet = Image.new('RGB', (cw * a.cols, ch * ((len(fs) + a.cols - 1) // a.cols)))
    d = ImageDraw.Draw(sheet)
    for i, f in enumerate(fs):
        im = Image.fromarray(to_rgb(raw, f - a.frame0, w, h, rec, hdr)).resize((cw, ch), Image.BILINEAR)
        x, y = (i % a.cols) * cw, (i // a.cols) * ch
        sheet.paste(im, (x, y))
        r = rows.get(f)
        lab = '%d %.1fs %02x:%02x %s' % (f, r[1], r[2], r[3], r[4]) if r else str(f)
        d.text((x + 3, y + 2), lab, fill=(255, 255, 0))
    sheet.save(a.out, quality=85)
    print('wrote', a.out, len(fs), 'frames')


if __name__ == '__main__':
    main()

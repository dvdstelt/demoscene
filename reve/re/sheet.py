"""Contact sheets of run_reve.py's .frames.

usage: python3 sheet.py full.frames out.jpg f1 f2 ...                 these records (screen refreshes)
       python3 sheet.py full.frames out.jpg --every 2.0 [--from-time S] [--to-time S] [--cols 6] [--scale 0.5]
                                                                     one frame every 2 s of program time

.frames: 24-byte header (b'REVEFRM1', <IIII: width 320, height 200, record bytes 64768, header bytes 24), then per
screen refresh (70.086 Hz) the 64,000 bytes of VGA memory and the 768-byte DAC (6 bits per entry).
In Python: raw, n = open_frames(path); rgb(raw, k) -> (200, 320, 3) uint8; read_ticks(path) -> {frame: row}.
"""
import argparse
import struct

import numpy as np
from PIL import Image, ImageDraw

W, H, REC, HDR = 320, 200, 64768, 24
FPS = 25175000 / (800 * 449)


def open_frames(path):
    raw = np.memmap(path, dtype=np.uint8, mode='r')
    assert bytes(raw[:8]) == b'REVEFRM1', bytes(raw[:8])
    w, h, rec, hdr = struct.unpack('<IIII', bytes(raw[8:24]))
    assert (w, h, rec, hdr) == (W, H, REC, HDR)
    return raw, (len(raw) - HDR) // REC


def indexed(raw, k):
    o = HDR + k * REC
    return (np.asarray(raw[o:o + 64000]).reshape(H, W), np.asarray(raw[o + 64000:o + REC]).reshape(256, 3))


def rgb(raw, k):
    px, pal = indexed(raw, k)
    lut = (pal.astype(np.uint32) * 255 + 31) // 63
    return lut[px].astype(np.uint8)


def read_ticks(path):
    rows = {}
    names = None
    for line in open(path):
        if line.startswith('#'):
            names = line[1:].split()
            continue
        f = line.split()
        rows[int(f[0])] = dict(zip(names, f))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('frames')
    ap.add_argument('out')
    ap.add_argument('list', nargs='*')
    ap.add_argument('--every', type=float)
    ap.add_argument('--from-time', type=float, default=0)
    ap.add_argument('--to-time', type=float, default=1e9)
    ap.add_argument('--frame0', type=int, default=0)
    ap.add_argument('--cols', type=int, default=4)
    ap.add_argument('--scale', type=float, default=1.0)
    a = ap.parse_args()
    raw, n = open_frames(a.frames)
    if a.every:
        sel = []
        t = a.from_time
        while t <= a.to_time and int(t * FPS) - a.frame0 < n:
            sel.append(int(t * FPS) - a.frame0)
            t += a.every
    else:
        sel = [int(x) - a.frame0 for x in a.list]
    sel = [k for k in sel if 0 <= k < n]
    cw, ch = int(W * a.scale * 2), int(H * a.scale * 2 * 1.2)
    cols = min(a.cols, len(sel))
    rows = (len(sel) + cols - 1) // cols
    img = Image.new('RGB', (cols * cw, rows * (ch + 14)), (40, 40, 40))
    d = ImageDraw.Draw(img)
    for i, k in enumerate(sel):
        im = Image.fromarray(rgb(raw, k)).resize((cw, ch), Image.NEAREST)
        x, y = (i % cols) * cw, (i // cols) * (ch + 14)
        img.paste(im, (x, y + 14))
        d.text((x + 2, y + 1), 'f%d  %.2fs' % (k + a.frame0, (k + a.frame0) / FPS), fill=(255, 255, 0))
    img.save(a.out, quality=90)
    print('wrote %s (%d frames)' % (a.out, len(sel)))


if __name__ == '__main__':
    main()

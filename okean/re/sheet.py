"""Contact sheets of run_okean.py's .frames.

usage: python3 sheet.py full.frames out.jpg F1 F2 ...                frames with these numbers
       python3 sheet.py full.frames out.jpg --every 2.0 [--from 30] [--to 60] [--cols 6] [--scale 0.5]
                                                                    one frame every 2 seconds (labels: frame, time,
                                                                    part, music order:row)

.frames: 32-byte header (b'OKEANFR1', <IIIIII: max width W, max height H, record bytes, header bytes, 0, 0>), then
per video frame: <HHHH width, height, flags, 0>, 768 bytes 6-bit palette, W*H pixel indices (visible area top left).
Frames are shown at 4:3 (320x200 and 320x240 both as 320x240).
"""
import argparse
import struct

import numpy as np
from PIL import Image, ImageDraw


def open_frames(path):
    raw = np.memmap(path, dtype=np.uint8, mode='r')
    assert bytes(raw[:8]) == b'OKEANFR1', bytes(raw[:8])
    w, h, rec, hdr, _, _ = struct.unpack('<IIIIII', bytes(raw[8:32]))
    n = (len(raw) - hdr) // rec
    return raw, w, h, rec, hdr, n


def frame(raw, i, W, H, rec, hdr):
    """(rgb array of the visible area, width, height)"""
    o = hdr + i * rec
    w, h, flags, _ = struct.unpack('<HHHH', bytes(raw[o:o + 8]))
    pal = np.frombuffer(bytes(raw[o + 8:o + 776]), np.uint8).reshape(256, 3).astype(np.uint16)
    pal = (pal * 255 + 31) // 63
    if not w or not h:
        return np.zeros((200, 320, 3), np.uint8), 320, 200
    idx = np.asarray(raw[o + 776:o + 776 + W * H]).reshape(H, W)[:h, :w]
    return pal[idx].astype(np.uint8), w, h


def to_43(rgb, out_w=320, out_h=240):
    im = Image.fromarray(rgb)
    return im.resize((out_w, out_h), Image.NEAREST)


def read_ticks(path):
    rows = []
    for line in open(path):
        if line.startswith('#'):
            continue
        f = line.split()
        rows.append((int(f[0]), float(f[1]), int(f[2]), int(f[3]), f[6]))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('frames')
    ap.add_argument('out')
    ap.add_argument('nums', nargs='*', type=int)
    ap.add_argument('--every', type=float, help='seconds between frames (needs the .ticks file)')
    ap.add_argument('--ticksfile', help='default: frames path with .ticks')
    ap.add_argument('--frame0', type=int, default=0, help='number of the first record (run resumed from a snapshot)')
    ap.add_argument('--from', dest='t0', type=float, default=0.0)
    ap.add_argument('--to', dest='t1', type=float, default=1e9)
    ap.add_argument('--cols', type=int, default=4)
    ap.add_argument('--scale', type=float, default=1.0)
    a = ap.parse_args()
    raw, W, H, rec, hdr, n = open_frames(a.frames)
    tf = a.ticksfile or a.frames.rsplit('.', 1)[0] + '.ticks'
    labels = {}
    try:
        rows = read_ticks(tf)
    except OSError:
        rows = []
    for f, tm, order, row, part in rows:
        labels[f] = 'f%d %.2fs %s %02x:%02x' % (f, tm, part, order, row)
    if a.every:
        sel, nxt = [], a.t0
        for f, tm, order, row, part in rows:
            if tm > a.t1:
                break
            if tm >= nxt and 0 <= f - a.frame0 < n:
                sel.append(f)
                nxt += a.every
                while nxt <= tm:
                    nxt += a.every
        nums = sel
    else:
        nums = [f for f in a.nums if 0 <= f - a.frame0 < n]
    cw, ch = int(320 * a.scale), int(240 * a.scale)
    sheet = Image.new('RGB', (cw * a.cols, ch * ((len(nums) + a.cols - 1) // a.cols)))
    d = ImageDraw.Draw(sheet)
    for i, f in enumerate(nums):
        rgb, w, h = frame(raw, f - a.frame0, W, H, rec, hdr)
        im = to_43(rgb, cw, ch)
        x, y = (i % a.cols) * cw, (i // a.cols) * ch
        sheet.paste(im, (x, y))
        d.text((x + 3, y + 2), labels.get(f, str(f)), fill=(255, 255, 0))
    sheet.save(a.out, quality=85)
    print('wrote', a.out, len(nums), 'frames')


if __name__ == '__main__':
    main()

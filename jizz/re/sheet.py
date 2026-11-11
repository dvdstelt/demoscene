"""Contact sheets of run_jizz.py's .frames.

usage: python3 sheet.py full.frames out.jpg t1 t2 ...          frames at these ticks
       python3 sheet.py full.frames out.jpg --every 2.0 [--music] [--cols 6] [--scale 0.5]
                                                               one frame every 2 seconds (labels: tick, time, part)

.frames: 32-byte header (b'JIZZFRM1', <IIIIII: width, height, bits 16|15, record bytes, header bytes, 0), then per
tick one 320x200 page of 16-bit pixels (5:6:5 for bits 16, 5:5:5 for bits 15).
"""
import argparse
import struct

import numpy as np
from PIL import Image, ImageDraw


def open_frames(path):
    raw = np.memmap(path, dtype=np.uint8, mode='r')
    magic = bytes(raw[:8])
    assert magic == b'JIZZFRM1', magic
    w, h, bits, rec, hdr, _ = struct.unpack('<IIIIII', bytes(raw[8:32]))
    n = (len(raw) - hdr) // rec
    return raw, w, h, bits, rec, hdr, n


def to_rgb(raw, t, w, h, bits, rec, hdr):
    px = np.frombuffer(bytes(raw[hdr + t * rec:hdr + (t + 1) * rec]), dtype='<u2').reshape(h, w).astype(np.uint32)
    if bits == 16:
        r, g, b = (px >> 11) & 31, (px >> 5) & 63, px & 31
        g = g * 255 // 63
    else:
        r, g, b = (px >> 10) & 31, (px >> 5) & 31, px & 31
        g = g * 255 // 31
    rgb = np.stack([r * 255 // 31, g, b * 255 // 31], axis=-1).astype(np.uint8)
    return rgb


def read_ticks(path):
    rows = []
    for line in open(path):
        if line.startswith('#'): continue
        f = line.split()
        rows.append((int(f[0]), float(f[2]), f[7], int(f[3])))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('frames')
    ap.add_argument('out')
    ap.add_argument('ticks', nargs='*', type=int)
    ap.add_argument('--every', type=float, help='seconds between frames (needs the .ticks file)')
    ap.add_argument('--ticksfile', help='default: frames path with .ticks')
    ap.add_argument('--tick0', type=int, default=0, help='tick of the first record (run resumed from a snapshot)')
    ap.add_argument('--from-time', type=float, default=0.0, help='seconds (program time, or music time with --music)')
    ap.add_argument('--music', action='store_true', help='with --every: count time from the music start (first tick '
                    'with a music order >= 0) and label frames with music time')
    ap.add_argument('--cols', type=int, default=4)
    ap.add_argument('--scale', type=float, default=1.0)
    a = ap.parse_args()
    raw, w, h, bits, rec, hdr, n = open_frames(a.frames)
    labels = {}
    if a.every:
        tf = a.ticksfile or a.frames.rsplit('.', 1)[0] + '.ticks'
        rows = read_ticks(tf)
        m0 = next((tm for t, tm, part, order in rows if order >= 0), 0.0)
        sel, nxt = [], (m0 if a.music else 0.0) + a.from_time
        for t, tm, part, order in rows:
            if tm >= nxt and 0 <= t - a.tick0 < n:
                sel.append(t)
                labels[t] = 't%d  %.1fs  %s' % (t, tm - m0 if a.music else tm, part)
                nxt += a.every
                while nxt <= tm: nxt += a.every
        ts = sel
    else:
        ts = [t for t in a.ticks if 0 <= t - a.tick0 < n]
    cw, ch = int(w * a.scale), int(h * a.scale)
    sheet = Image.new('RGB', (cw * a.cols, ch * ((len(ts) + a.cols - 1) // a.cols)))
    d = ImageDraw.Draw(sheet)
    for i, t in enumerate(ts):
        im = Image.fromarray(to_rgb(raw, t - a.tick0, w, h, bits, rec, hdr))
        if a.scale != 1.0: im = im.resize((cw, ch), Image.BILINEAR)
        x, y = (i % a.cols) * cw, (i // a.cols) * ch
        sheet.paste(im, (x, y))
        d.text((x + 3, y + 2), labels.get(t, str(t)), fill=(255, 255, 0))
    sheet.save(a.out, quality=85)
    print('wrote', a.out, len(ts), 'frames')


if __name__ == '__main__':
    main()

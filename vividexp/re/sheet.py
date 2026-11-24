"""Contact sheets of run_vivid.py's .frames.

usage: python3 sheet.py full.frames out.jpg f1 f2 ...            these records (screen refreshes)
       python3 sheet.py full.frames out.jpg --every 2.0 [--from-time S] [--to-time S] [--cols 6] [--scale 0.5]
                                                                one frame every 2 s of time since the music start
                                                                (labels: frame, time, order:row, frame routine)
       python3 sheet.py full.frames out.jpg --pos 3:0 5:32 ...  the first frame at or after these music positions

.frames: 24-byte header (b'VIVDFRM1', <IIII: width 320, height 200, record bytes 64768, header bytes 24), then per
screen refresh (70.086 Hz) the 64,000 bytes of VGA memory and the 768-byte DAC (6 bits per entry). Record k is
frame k of the .ticks (--frame0 for a run resumed from a snapshot).
In Python: raw, n = open_frames(path); rgb(raw, k) -> (200, 320, 3) uint8; read_ticks(path) -> {frame: row}.
"""
import argparse
import struct

import numpy as np
from PIL import Image, ImageDraw

W, H, REC, HDR = 320, 200, 64768, 24


def open_frames(path):
    raw = np.memmap(path, dtype=np.uint8, mode='r')
    assert bytes(raw[:8]) == b'VIVDFRM1', bytes(raw[:8])
    w, h, rec, hdr = struct.unpack('<IIII', bytes(raw[8:24]))
    assert (w, h, rec, hdr) == (W, H, REC, HDR)
    return raw, (len(raw) - HDR) // REC


def indexed(raw, k):
    """(pixels 200x320 uint8, palette 256x3 uint8 6-bit)"""
    o = HDR + k * REC
    return (np.asarray(raw[o:o + 64000]).reshape(H, W), np.asarray(raw[o + 64000:o + REC]).reshape(256, 3))


def rgb(raw, k):
    px, pal = indexed(raw, k)
    lut = (pal.astype(np.uint32) * 255 + 31) // 63
    return lut[px].astype(np.uint8)


class Tick:
    __slots__ = ('frame', 'time', 'order', 'row', 'ticks', 'part_ticks', 'script', 'frame_fn', 'tick_fn', 'shown',
                 'irqs', 'eip')


def read_ticks(path):
    rows = {}
    for line in open(path):
        if line.startswith('#'):
            continue
        f = line.split()
        t = Tick()
        t.frame, t.time, t.order, t.row = int(f[0]), float(f[1]), int(f[2]), int(f[3])
        t.ticks, t.part_ticks, t.script = float(f[4]), int(f[5]), int(f[6])
        t.frame_fn, t.tick_fn, t.shown, t.irqs, t.eip = int(f[7], 16), int(f[8], 16), float(f[9]), int(f[10]), \
            int(f[11], 16)
        rows[t.frame] = t
    return rows


def music_start(ticks):
    return next(t.time for k, t in sorted(ticks.items()) if t.order >= 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('frames')
    ap.add_argument('out')
    ap.add_argument('list', nargs='*')
    ap.add_argument('--every', type=float)
    ap.add_argument('--pos', action='store_true', help='the list holds music positions order:row')
    ap.add_argument('--from-time', type=float, default=-1e9)
    ap.add_argument('--to-time', type=float, default=1e9)
    ap.add_argument('--frame0', type=int, default=0)
    ap.add_argument('--cols', type=int, default=4)
    ap.add_argument('--scale', type=float, default=1.0)
    a = ap.parse_args()
    raw, n = open_frames(a.frames)
    ticks = read_ticks(a.frames.rsplit('.', 1)[0] + '.ticks')
    t0 = music_start(ticks)

    def label(k):
        t = ticks.get(k)
        if not t:
            return 'f%d' % k
        return 'f%d %.2fs %d:%02d %x' % (k, t.time - t0, t.order, t.row, t.frame_fn)

    if a.every:
        sel, nxt = [], None
        for k in sorted(ticks):
            mt = ticks[k].time - t0
            if ticks[k].order < 0 or mt < a.from_time or mt > a.to_time:
                continue
            if nxt is None:
                nxt = mt
            if mt >= nxt:
                sel.append(k)
                while nxt <= mt:
                    nxt += a.every
    elif a.pos:
        sel = []
        for p in a.list:
            o, r = (int(x) for x in p.split(':'))
            sel.append(next(k for k in sorted(ticks) if (ticks[k].order, ticks[k].row) >= (o, r)))
    else:
        sel = [int(x) for x in a.list]
    sel = [k for k in sel if 0 <= k - a.frame0 < n]
    cw, ch = int(W * a.scale), int(H * 1.2 * a.scale)
    sheet = Image.new('RGB', (cw * a.cols, ch * ((len(sel) + a.cols - 1) // a.cols)))
    d = ImageDraw.Draw(sheet)
    for i, k in enumerate(sel):
        im = Image.fromarray(rgb(raw, k - a.frame0)).resize((cw, ch), Image.BILINEAR)
        x, y = (i % a.cols) * cw, (i // a.cols) * ch
        sheet.paste(im, (x, y))
        d.text((x + 3, y + 2), label(k), fill=(255, 255, 0))
    sheet.save(a.out, quality=85)
    print('wrote', a.out, len(sel), 'frames')


if __name__ == '__main__':
    main()

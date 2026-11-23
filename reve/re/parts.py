"""The timeline of the capture: when each script entry starts, how many frames it draws, what it shows.

usage: python3 parts.py full [--sheet parts.jpg]

Reads <prefix>.log (run_reve.py's "entry N: init at frame F" and "retrace routine set at frame F" lines),
<prefix>.ticks (the drawn-frame count per refresh) and the script table from unpacked/reve_img.bin, and prints one
row per entry: its routines, end position, the refresh at which its init ran and at which its retrace routine was set,
the refreshes it lasted, the frames it drew and its frame rate. --sheet writes a contact sheet with the first, middle
and last record of every entry.
"""
import argparse
import re

import rep
from sheet import open_frames, read_ticks, rgb, FPS

SCRIPT = 0x205ed0


def script():
    out = []
    a = SCRIPT
    while rep.rd32(a):
        out.append([rep.rd32(a + 4 * k) for k in range(5)])
        a += 0x14
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('prefix')
    ap.add_argument('--sheet')
    a = ap.parse_args()
    ent = script()
    init, setf = {}, {}
    for line in open(a.prefix + '.log'):
        m = re.match(r'entry (\d+): (init|retrace routine set) at frame (\d+)', line)
        if m:
            (init if m.group(2) == 'init' else setf)[int(m.group(1))] = int(m.group(3))
    ticks = read_ticks(a.prefix + '.ticks')
    last = max(ticks)
    print('| entry | init | done | retrace | frame | ends at | init at | starts at | refreshes | frames drawn | fps |')
    print('|---|---|---|---|---|---|---|---|---|---|---|')
    rows = []
    for i, (fi, fd, fr, ff, end) in enumerate(ent):
        s = setf.get(i)
        e = setf.get(i + 1, init.get(i + 1, last))
        if s is None:
            continue
        n = e - s
        drawn = int(ticks[min(e, last)]['drawn']) - int(ticks[s]['drawn'])
        rows.append((i, s, e))
        print('| %d | %06x | %06x | %06x | %06x | %d:%02d | %d | %d | %d | %d | %.1f |' % (
            i, fi, fd, fr, ff, end >> 8, end & 0xff, init.get(i, -1), s, n, drawn, drawn * FPS / max(n, 1)))
    if a.sheet:
        from PIL import Image, ImageDraw
        raw, nrec = open_frames(a.prefix + '.frames')
        cw, ch = 320, 240
        img = Image.new('RGB', (3 * cw, len(rows) * (ch + 16)), (40, 40, 40))
        d = ImageDraw.Draw(img)
        for j, (i, s, e) in enumerate(rows):
            for k, f in enumerate((s + 2, (s + e) // 2, e - 1)):
                if 0 <= f < nrec:
                    img.paste(Image.fromarray(rgb(raw, f)).resize((cw, ch)), (k * cw, j * (ch + 16) + 16))
                d.text((k * cw + 3, j * (ch + 16) + 2), 'entry %d  f%d  %.2fs' % (i, f, f / FPS), fill=(255, 255, 0))
        img.save(a.sheet, quality=88)
        print('wrote', a.sheet)


if __name__ == '__main__':
    main()

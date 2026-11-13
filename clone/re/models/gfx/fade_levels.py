"""The blend levels of the faded picture overlays, replayed from the capture's frame timings and compared with the
level that fits the captured pixels (overlay_check.py).

usage (inside the toolbox, from clone/re): python3 models/gfx/fade_levels.py

Per shown frame (full.flips): dt = t70 - t70 of the previous frame. The sync events run before the part draws,
at the first frame whose position *before drawing* (= the previous flip's position column) has reached the entry.
  part 1 title    [0x422514] = 63.0 (static); after event 106 (08:00): -= 0.45 dt, >= 0; level = trunc
  part 1 credits  [0x42250c] = 63.0 (static); after event 107 (3c:10): -= 0.75 dt, >= 0; level = trunc
  part 11 title   [0x428464] = 1.0 (static); after event 1101 (0b:00): -= 0.0065 dt, >= 0; level = trunc((1 - f) 63)
  part 3          [0x4224fc] = 0 at event 3 (0x405b3f); per frame: v < 5000 and no event 301: v = trunc(v + 5 dt),
                  <= 5000; after event 301 (30:00): v = trunc(v - 50 dt), >= 0; level = max(0, trunc(63 - 0.05 v))
(the float variables are float32 in the original; this replay uses doubles, which can move a level by one at an
integer boundary).
"""
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import overlay_check as oc  # noqa: E402


def replay():
    fl = np.loadtxt(os.path.join(RE, 'full.flips'), comments='#')
    title, credits, f11, v3 = 63.0, 63.0, 1.0, 0
    lv = {}
    prev_t70, prev_pos = None, -1
    for r in fl:
        n, t70, part = int(r[0]), r[4], int(r[5])
        pos = prev_pos                      # the position main saw before this frame was drawn
        prev_pos = int(r[6]) * 64 + int(r[7])
        dt = 0.0 if prev_t70 is None else t70 - prev_t70
        prev_t70 = t70
        if part == 1:
            if 0x08 * 64 <= pos < 0x3a * 64 and title > 0: title = max(0.0, title - 0.45 * dt)
            if pos >= 0x3c * 64 + 0x10 and credits > 0: credits = max(0.0, credits - 0.75 * dt)
            lv[n] = (math.trunc(title), math.trunc(credits))
        elif part == 11:
            if pos >= 0x0b * 64 and f11 > 0: f11 = max(0.0, f11 - 0.0065 * dt)
            lv[n] = (math.trunc((1 - f11) * 63),)
        elif part == 3:
            if pos >= 0x30 * 64:
                if v3 != 0: v3 = max(0, math.trunc(v3 - 50 * dt))
            elif v3 < 5000:
                v3 = min(5000, math.trunc(v3 + 5 * dt))
            lv[n] = (max(0, math.trunc(63 - 0.05 * v3)),)
    return lv


def main():
    lv = replay()
    raw, rec, hdr, ticks = oc.load()
    flip_at = {int(r[0]): int(r[8]) for r in ticks}
    for t in (1560, 1600, 1640, 1700, 2124, 2160, 2200, 2250, 7900, 7950, 8000, 8500, 9290, 9300, 11590, 11620,
              11650, 11700):
        print(f'tick {t}: flip {flip_at[t]}, replayed level(s) {lv.get(flip_at[t])}')
        oc.check(raw, rec, hdr, ticks, t)


if __name__ == '__main__':
    main()

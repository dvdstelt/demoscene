"""Model of the end loop 0x18c25 (end picture [0x657a] + 0x17999 white fade started by 0x17979(0x200b20)), checked
against full.frames with the timer values in full.flips (0x17999 is the only timer read in this loop).

usage: python3 end_model.py
"""
import numpy as np

import common as c
from jizz_model import darken
from zoom_model import fade_level


def main():
    s = c.load_snap(9045)
    m = s['mem']
    b = c.u32(m, 0x657a)
    pic = np.frombuffer(m[b:b + 128000], '<u2').reshape(200, 320)
    base = c.i32(m, 0x6542)
    t0 = c.i32(m, 0x179d6)
    K = c.u32(m, 0x179de)
    print('fade start (timer)', (t0 - base) & 0xffffffff, 'K', K, 'duration s', 1023 * 2**32 / K / 1193182)
    fl = [f for f in c.flips() if f[4] == '18c25']
    last = {}
    for n, tk, tm, t, part in c.flips(): last[tk] = n
    active = True
    stats = {}
    for n, tk, tm, t, part in fl:
        page = pic
        if active and t > -(1 << 39):
            lv = fade_level((t + base) & 0xffffffff, t0, K)
            page = darken(pic, lv >> 5, (31, 31, 31))
            if lv == 0: active = False
        if last[tk] != n: continue
        e = int((page != c.frame565(tk)).sum())
        stats[e == 0] = stats.get(e == 0, 0) + 1
        if e: print('flip', n, 'tick', tk, 'diff', e)
    print('captured frames exact / not:', stats)


if __name__ == '__main__':
    main()

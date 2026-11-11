"""fade.py: models of 0x1131b (fade tables), 0x179f6 (fade the page by a level 0..31), 0x17979 / 0x17999 (timer
fade), checked (1) against the tables in snapshots and (2) against the captured end-picture fade (ticks 9039..9160):
captured page = 0x179f6(end picture [0x657a], level(timer))."""
import sys, struct
import numpy as np
sys.path.insert(0, '/home/dennis/src/dvdstelt/demoscene/jizz/re/models/core')
from snap import load, rd32, rds32, rdb
import frames

def fade_tables(C, shift):
    """0x11358: 32 rows (row r = 31 - b) of 64 dwords (32 used): ((a*(31-r) + C*r) >> 5) << shift"""
    t = np.zeros((32, 64), np.uint32)
    for r in range(32):
        b = 31 - r
        for a in range(32):
            t[r, a] = ((a * b + C * (31 - b)) >> 5) << shift
    return t

def fade_page(page, level, tabs):
    """0x179f6 on a 320x200 page of 5:6:5 words (shifts of the 5:6:5 mode: R 11, G 6 (top 5 bits), B 0)"""
    R, G, B = tabs
    p = page.astype(np.uint32)
    return (R[level][(p & 0xf800) >> 11] | G[level][(p & 0x7c0) >> 6] | B[level][p & 0x1f]).astype(np.uint16)

def level(t, rate):
    """0x17999: t = PIT clocks since 0x17979; level 0..0x3ff (0 = done)"""
    if t < 0: return 0
    v = 0x3ff - ((t * rate) >> 32)
    return max(v, 0)

if __name__ == '__main__':
    for tick in (1365, 4050, 9045):
        s = load(tick)
        C = [rd32(s, a) for a in (0x11303, 0x11307, 0x1130b)]
        bad = 0
        for k, (ptr, sh) in enumerate(((0x1130f, 11), (0x11313, 6), (0x11317, 0))):
            want = np.frombuffer(rdb(s, rd32(s, ptr), 0x2000), np.uint32).reshape(32, 64)
            got = fade_tables(C[k], sh)
            bad += np.count_nonzero(got[:, :32] != want[:, :32])
        print(f't{tick}: C = {C}, fade tables 0x1131b: {bad} of 3072 entries differ')
    # end picture fade
    s = load(9045)
    tabs = [fade_tables(rd32(s, a), sh) for a, sh in ((0x11303, 11), (0x11307, 6), (0x1130b, 0))]
    pic = np.frombuffer(rdb(s, rd32(s, 0x657a), 128000), '<u2').reshape(200, 320)
    off6542 = rds32(s, 0x6542); start = rds32(s, 0x179d6); rate = rd32(s, 0x179de)
    print(f'end loop: [0x6542] = {off6542}, fade start [0x179d6] = {start}, rate [0x179de] = {rate:#x}')
    fl = [f for f in frames.flips() if f[1] >= 9039]
    ok = n = 0
    # [0x179da] per flip: 0x17999 applies 0x179f6(new >> 5) whenever the old value was non-zero
    state, per_flip = 0x3ff, {}
    for f in fl:
        if state:
            state = level(f[3] + off6542 - start, rate)
            per_flip[f[0]] = state >> 5
        else:
            per_flip[f[0]] = None
    for t in range(9040, 9170):
        f = [x for x in fl if x[1] <= t]
        if not f: continue
        f = f[-1]
        lv = level(f[3] + off6542 - start, rate) if f[3] > -(1 << 39) else 0
        got = fade_page(pic, per_flip[f[0]], tabs) if per_flip[f[0]] is not None else pic
        cap = frames.raw(t)
        d = np.count_nonzero(got != cap)
        n += 1; ok += d == 0
        if t % 10 == 0 or d: print(f'tick {t}: flip {f[0]} timer {f[3]} level {lv} ({lv >> 5}): {d} pixels differ')
    print(f'{ok} of {n} captured frames reproduced exactly')

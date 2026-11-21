"""The script table with the capture's timing, as a Markdown table for notes-core.md.

Per entry: music position, song row R (rows played from 0), c_R = the timer callback in which that row starts
(M = [0x4248c4] after the callback's increment; DSMI time = 35 M), M_entry = [0x4248c4] when the entry ran
(exact for entries that reset the part tick counter: M(k) - part_ticks(k); for palette entries the first DAC step
pins it to before / after the retrace callback; otherwise the range given by the records), the delay M_entry - c_R,
and the first retrace callback after the entry (M and the record that first shows its effect).
Entries in one main-loop pass (same position; 88 runs with 87) share their time.
"""
import os
import sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
sys.path.insert(0, RE)
import rep
from sheet import read_ticks, open_frames, indexed

T = read_ticks(os.path.join(RE, 'full.ticks'))
ks = sorted(T)
raw, nrec = open_frames(os.path.join(RE, 'full.frames'))
LOST = 3045


def M_ret(k):
    return 4 * k - 150 - (1 if k >= LOST else 0)


def M_rec(k):
    return T[k].irqs - 90 - (1 if k >= LOST else 0)


def c_row(R):
    if R <= 2304:
        return (1200 + 768 * R) // 35 + 1
    return (1200 + 768 * 2304 + 2304 * (R - 2304)) // 35 + 1


SETS = {0x4038c8, 0x4038dc, 0x403164, 0x403184, 0x4073c8, 0x4073e8, 0x4057bc, 0x405ba0, 0x404728, 0x4098c4,
        0x409e90, 0x402c28, 0x402c7c, 0x407f90, 0x408014, 0x40585c, 0x40571c, 0x40a678, 0x40a738, 0x40461c,
        0x404668}
PAL = {0x400498, 0x400454, 0x400438, 0x403164, 0x4073c8, 0x405f1c, 0x4057bc, 0x405ba0, 0x4098c4, 0x409358,
       0x4093f0, 0x402c28, 0x407f90, 0x40585c, 0x40571c, 0x40a678, 0x40a098, 0x40461c}
WHAT = {
    0x4038c8: 'intro start: tick 0x403214, frame 0x403748',
    0x4031a0: 'intro: light drops on ([0x425570] = 1), 130 drops at rand() % 320, rand() % 200 (260 rand calls)',
    0x4038dc: 'intro end: routines 0, frees the intro memory (tag [0x42555c])',
    0x403164: 'logo start: palette logo/logo.pal, tick 0x402e30, frame 0x402f1c',
    0x400498: 'white flash (15)',
    0x400454: 'inverse flash (20)',
    0x400438: 'fade to black (40)',
    0x402f10: 'logo: [0x42553c] = 1 (frame routine also draws bitmap [0x425534] at (34, 13))',
    0x403184: 'logo end: routines 0, frees tag [0x425500]',
    0x4073c8: 'credits start: tick 0x405f30, frame 0x406794, palette credits/tunnel.pal',
    0x405f1c: 'credits: white flash, [0x426438] += 1 (next name)',
    0x405f10: 'credits: [0x426430] = 1',
    0x4073e8: 'credits end: routines 0, frees tag [0x426480]',
    0x4057bc: 'sprite picture 2 start ([0x4227fc] = 2): tick 0x404738, frame 0x4047a0, palette nigga/nigga2.pal',
    0x404680: 'sprites: 15 float parameters 0x422810..0x422848',
    0x405ba0: 'sprite picture 4 ([0x4227fc] = 4): routines set again, palette nigga/nigga4.pal',
    0x405f00: 'sprites: [0x422800] = 30',
    0x404728: 'sprites end: routines 0',
    0x4098c4: 'flower start: builds the scene (SurRender, 732 rand calls), tick 0x40940c, frame 0x4095f8, '
              'palette flower/jeesus.pal',
    0x409358: 'flower: [0x42294c] = ([0x42294c] + 1) % 9, turns [0x4b6620], hides it; even: white flash, odd: '
              'inverse flash',
    0x4093f0: 'flower: shows [0x4b6620] again, [0x422950] = 0, white flash',
    0x409e90: 'flower end: routines 0',
    0x402c28: 'flight start: clears both buffers, palette flight/map.pal, fade in over 60, tick 0x4021f8, '
              'frame 0x4028e4',
    0x402c7c: 'flight end: routines 0, frees the map heightfield [0x424ff8]',
    0x407f90: 'water start: clears both buffers, palette water/water.pal, "water frame 1/2", tick 0x407c78, '
              'frame 0x407d78',
    0x408014: 'water end: routines 0, frees tag [0x4265f0]',
    0x40585c: 'sprite picture 3 ([0x4227fc] = 3): routines, palette nigga/nigga9.pal',
    0x40571c: 'sprite picture 1 ([0x4227fc] = 1): routines, palette nigga/nigga1.pal',
    0x40a678: 'twirl start: white flash, palette flower/jeesus.pal, tick 0x40a20c, frame 0x40a264',
    0x40a098: 'twirl: next picture ([0x42296c] = +1 mod 5), its palette with entry 0 black, white flash',
    0x40a738: 'twirl end: routines 0, frees tag [0x4b6d30]',
    0x40461c: 'temple start: clears both buffers, white flash, palette temple/temple.pal, tick 0x40407c, '
              'frame 0x404424',
    0x404668: 'temple end: routines 0 (the last frame stays, black palette)',
    0x400228: '`ret`; the next entry (1000:0, routine 0) ends the demo',
}

rows = []
prev = None
for k in ks:
    t = T[k]
    if t.order >= 0 and (t.order, t.row) != prev:
        prev = (t.order, t.row)
        rows.append(prev)
entries = []
a = 0x4222e4
while True:
    o, r, f = rep.rd32(a), rep.rd32(a + 4), rep.rd32(a + 8)
    entries.append((o, r, f))
    a += 12
    if f == 0:
        break


def dac(k):
    return indexed(raw, k)[1]


import json
PSTEP = json.load(open(os.path.join(HERE, 'palette_steps.json')))   # palette_model.py: entry -> first step record

# group entries into passes
passes = []
for i, (o, r, f) in enumerate(entries[:-1]):
    if passes and (entries[passes[-1][0]][:2] == (o, r) or (i == 88)):
        passes[-1].append(i)
    else:
        passes.append([i])

out = []
for ps in passes:
    o, r, _ = entries[ps[0]]
    pos = o * 64 + r
    R = next((j for j, p in enumerate(rows) if p[0] * 64 + p[1] >= pos), len(rows))
    c = c_row(R)
    kd = [next((kk for kk in ks if T[kk].script > i), None) for i in ps]
    if kd[0] is None:
        for i in ps:
            out.append((i, entries[i], R, c, None, None, None, None))
        continue
    k = kd[0]
    lo, hi = max(M_ret(k - 1), c), M_ret(k)
    exact = None
    for i, kk in zip(ps, kd):
        if entries[i][2] in SETS and kk == k:
            exact = M_rec(k) - T[k].part_ticks
    steps = [PSTEP[str(i)] for i in ps if str(i) in PSTEP]
    if exact is None and steps:
        if steps[0] == k:            # first fade step at the retrace callback of record k: ran before it
            hi = hi - 1
        else:                        # at record k + 1: ran after the retrace callback of record k
            lo = hi
    if exact is not None:
        lo = hi = exact
    for i, kk in zip(ps, kd):
        m_lo, m_hi = lo, hi
        if entries[i][2] in SETS and kk != k:          # 0x4098c4 finished after the record
            m_lo = m_hi = M_rec(kk) - T[kk].part_ticks
        nxt = lambda m: next(x for x in range(m + 1, m + 6) if (x - M_ret(k)) % 4 == 0)
        out.append((i, entries[i], R, c, m_lo, m_hi, nxt(m_lo), nxt(m_hi)))

print('| # | pos | routine | what it does | R | c_R | M_entry | delay | first retrace M (record) |')
print('|---|---|---|---|---|---|---|---|---|')
for i, (o, r, f), R, c, m_lo, m_hi, f0, f1 in out:
    if m_lo is None:
        print('| %d | %d:%02d | 0x%x | %s | %d | %d | (exit; last record 13080) | | |' % (i, o, r, f, WHAT[f], R, c))
        continue
    me = '%d' % m_lo if m_lo == m_hi else '%d..%d' % (m_lo, m_hi)
    de = '%d' % (m_lo - c) if m_lo == m_hi else '%d..%d' % (m_lo - c, m_hi - c)
    kf = lambda m: next(kk for kk in ks if M_ret(kk) >= m)
    fr = '%d (%d)' % (f0, kf(f0)) if f0 == f1 else '%d (%d) or %d (%d)' % (f0, kf(f0), f1, kf(f1))
    print('| %d | %d:%02d | 0x%x | %s | %d | %d | %s | %s | %s |' % (i, o, r, f, WHAT[f], R, c, me, de, fr))

"""The strobe of script entries 20 (GENE) and 27 (BOUGE), then 21 / 28 (the picture held), and the negative table.

0x25a5ff reads the DAC (256 x 3 six-bit values, to 0x20613c) and builds the 256-byte table 0x20643c: for every
index c the first index j (lowest) whose colour is nearest to (63 - r, 63 - g, 63 - b) of c, by the sum of squared
differences. The frame routines 0x205950 / 0x205b04 run, per drawn frame:

    count -= step;                      // [0x2059f4] / [0x205ba8] = 16000, [0x2059f8] / [0x205bac] = 1000
    if (count > 0) clear the screen to 0;
    else { count = 16000; step += 5; flag ^= 1;   // [0x2059f0] / [0x205ba4] = 0
           flag ? copy the picture : copy the picture through 0x20643c; }

usage (from reve/re, in the toolbox):
  python3 models/parts2d-b/strobe.py table             the negative tables of GENE and BOUGE and how far they are from
                                                        the true negative
  python3 models/parts2d-b/strobe.py check [20 27]     compare the model sequence with the capture, record by record
"""
import sys

import numpy as np

from common import raw, pal6, frames, indexed, entry_starts, read_ticks, FULL


def negative_table(p6):
    """0x25a5ff on the 6-bit palette p6 (256, 3)."""
    p = p6.astype(np.int64)
    tgt = 63 - p                                                    # (256, 3)
    # the code subtracts in 8 bits and sign-extends: values 0..63, so the difference is exact
    d = ((p[None, :, :] - tgt[:, None, :]) ** 2).sum(-1)           # d[c, j]
    return d.argmin(1).astype(np.uint8)                             # argmin = the first minimum, as 'jae' keeps it


def sequence(nframes):
    """Per drawn frame of the entry (1, 2, ...): 0 = black, 1 = the picture, 2 = the negative."""
    count, step, flag = 16000, 1000, 0
    out = []
    for _ in range(nframes):
        count -= step
        if count > 0:
            out.append(0)
        else:
            count, step, flag = 16000, step + 5, flag ^ 1
            out.append(1 if flag else 2)
    return out


def table():
    for name in ('GENE', 'BOUGE'):
        px, pal = raw(name)
        p6 = pal6(pal)
        t = negative_table(p6)
        used = np.unique(px)
        err = np.abs(p6[t[used]].astype(int) - (63 - p6[used].astype(int)))
        # weighted by how many pixels use each index
        cnt = np.bincount(px.reshape(-1), minlength=256)[used]
        mean = (err.sum(1) * cnt).sum() / cnt.sum() / 3
        print('%s: %d indices used; |table colour - true negative| per channel: mean %.2f (pixel-weighted), '
              'max %d (6-bit units)' % (name, len(used), mean, err.max()))


def check(entries):
    raw_, nrec = frames()
    init, setf = entry_starts()
    ticks = read_ticks(FULL + '.ticks')
    for e in entries:
        name = {20: 'GENE', 27: 'BOUGE'}[e]
        if e + 1 not in setf and e not in setf:
            print('entry %d: capture not there yet' % e)
            continue
        px, pal = raw(name)
        p6 = pal6(pal)
        t = negative_table(p6)
        neg = t[px]
        s = setf[e]
        end = min(init.get(e + 2, nrec), nrec)
        kinds = []
        for k in range(init[e], end):
            scr, dac = indexed(raw_, k)
            if (scr == px).all():
                c = 1
            elif (scr == neg).all():
                c = 2
            elif (scr == 0).all():
                c = 0
            else:
                c = 'other(%d px)' % (scr != 0).sum()
            kinds.append((k, int(ticks[k]['drawn']) if k in ticks else -1, c, (dac == p6).all()))
        # the counter steps per drawn frame. With the drawn-frame counter of full.ticks: n = frames drawn by the
        # entry when record k was taken (+ off); without it (ticks not written yet): n = k - s (+ off)
        seq = sequence(5000)
        e1 = setf.get(e + 1, nrec)
        best = None
        for off in range(-3, 4):
            mism = []
            for k, d, c, palok in kinds:
                if k < s or k >= e1:
                    continue
                if s in ticks and d >= 0:
                    n = d - int(ticks[s]['drawn']) + off
                else:
                    n = k - s + off
                want = seq[n - 1] if n >= 1 else 0
                if c != want:
                    mism.append((k, n, c, want))
            if best is None or len(mism) < len(best[1]):
                best = (off, mism)
        off, mism = best
        basis = 'drawn frames' if s in ticks else 'refreshes'
        flashes = [(k, c) for k, d, c, p in kinds if c in (1, 2)]
        palfrom = next((k for k, d, c, p in kinds if p), None)
        print('entry %d (%s): records %d..%d; the DAC holds the picture\'s palette from record %s to the end: %s; '
              '%d flash records' % (e, name, init[e], end - 1, palfrom,
                                    all(p for k, d, c, p in kinds if k >= palfrom), len(flashes)))
        print('   first flashes:', flashes[:12])
        print('   n = %s since record %d %+d: %d records differ from the model %s' % (
            basis, s, off, len(mism), mism[:8]))
        held = [(k, c) for k, d, c, p in kinds if k >= setf.get(e + 1, nrec)]
        print('   entry %d: records %d..%d: %s' % (e + 1, held[0][0], held[-1][0],
                                                 sorted(set((c, k) for k, c in held if c != 1))[:3] or 'all the picture'))


if __name__ == '__main__':
    if sys.argv[1] == 'table':
        table()
    else:
        check([int(x) for x in sys.argv[2:]] or [20, 27])

"""Check the DAC during the bitmap sections: the effect's palette (Init) and the InitFADE fades inside them.
usage: python3 models/bitmap/palcheck.py full|part2"""
import os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bitmap import Effect, find
RE = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, RE)
from sheet import open_frames, indexed, read_ticks
from verify import script_lines

cap = sys.argv[1]
raw, nrec = open_frames(os.path.join(RE, cap + '.frames'))
ticks = read_ticks(os.path.join(RE, cap + '.ticks'))
lines = script_lines(os.path.join(RE, cap + '.script'))
first = {}
for k in sorted(ticks):
    first.setdefault((ticks[k].part, ticks[k].line), k)
eff = None
for i, (frame, part, ln, text) in enumerate(lines):
    w = text.split()
    if w[2] == 'InitBITMAPEFEK':
        eff = Effect(w[3], part)
    if w[2] == 'RunBITMAPEFEK' and (part, ln) in first:
        k0 = first[(part, ln)]
        k1 = next((k for k in range(k0, nrec) if (ticks[k].part, ticks[k].line) != (part, ln)), nrec)
        same = [k for k in range(k0, k1) if (indexed(raw, k)[1] == eff.palette).all()]
        print('line %d %s: frames %d..%d, DAC == the effect palette in %d of %d' % (ln, w[3], k0, k1 - 1, len(same),
                                                                                  k1 - k0))
    if w[2] == 'InitFADE':
        a = np.frombuffer(open(find(w[3], part), 'rb').read(), np.uint8).reshape(256, 3).astype(np.int64)
        b = np.frombuffer(open(find(w[4], part), 'rb').read(), np.uint8).reshape(256, 3).astype(np.int64)
        steps = int(w[5])
        cur = a << 16
        delta = np.trunc(((b - a) << 16) / steps).astype(np.int64)   # idiv truncates toward zero
        F0 = ticks[frame].frames
        res = []
        for k in range(frame, min(frame + steps + 10, nrec)):
            dac = indexed(raw, k)[1].astype(np.int64)
            n = None
            for s in range(0, steps + 1):
                if ((cur + s * delta) >> 16 == dac).all():
                    n = s
                    break
            res.append((k, ticks[k].frames - F0, n))
        print('line %d InitFADE %s -> %s %d: (record, counter - counter at InitFADE record, fade step shown)' % (
            ln, w[3], w[4], steps), res[:6], '...', res[-4:])

"""When is a drawn frame shown? Match the dumped final buffers (run_inside.py --dump-buf every:40) with the
records of full.frames, and relate them to the number of ticks (pos / 47) the frame was drawn with.
usage: python3 latency.py full.pkl"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..', '..'))
from dumpread import records, F
from sheet import open_frames, indexed
full, nf = open_frames(os.path.join(HERE, '..', '..', 'full.frames'))
ticks = {}
for r in records(sys.argv[1]):
    if r['tag'] == 'render':
        ticks[r['n']] = round(F(r['vars'], 0x114) / 47)
    if r['tag'] == 'buf':
        b = np.frombuffer(r['buf'], np.uint8).reshape(200, 320)
        shown = [k for k in range(r['refresh'] - 2, r['refresh'] + 4) if (indexed(full, k)[0] == b).all()]
        t = ticks.get(r['n'])
        print('frame %d: ticks %s, drawing ended in refresh %d, shown in records %s; record - ticks = %s' % (
            r['n'], t, r['refresh'], shown, [k - t for k in shown] if t is not None else '-'))

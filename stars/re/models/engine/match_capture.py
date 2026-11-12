"""Find the full.frames record that shows the screen buffer a dump's draw call produced (rows 1..197, which 0x78905
copies to VGA on the part's next loop iteration), and report how many of those pixels the model reproduces.

usage: python3 match_capture.py DUMP.npz [...]
"""
import os
import sys
import numpy as np
import engine

HERE = os.path.dirname(os.path.abspath(__file__))
FRAMES = os.path.join(HERE, '..', '..', 'full.frames')
REC = 64000 + 768


def record(f):
    with open(FRAMES, 'rb') as fh:
        fh.seek(32 + REC * f)
        return np.frombuffer(fh.read(64000), np.uint8).reshape(200, 320)


for path in sys.argv[1:]:
    d = np.load(path)
    m = engine.Mem(bytearray(d['pre'].tobytes()))
    scr = m.selbase(m.u16(0x558d0))
    if int(d['entry']) == 0x79483:
        engine.draw_list(m, int(d['regs'][4]))
    else:
        engine.draw_object(m, int(d['regs'][6]))
    model = np.frombuffer(bytes(m.b[scr:scr + 64000]), np.uint8).reshape(200, 320)
    f0 = int(d['frame'])
    best = None
    for f in range(f0, f0 + 8):
        rec = record(f)
        same = int((rec[1:198] == model[1:198]).sum())
        if best is None or same > best[1]:
            best = (f, same)
    print('%s: model buffer rows 1..197 = full.frames record %d in %d of %d pixels' % (
        os.path.basename(path), best[0], best[1], 197 * 320))

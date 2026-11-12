"""Which capture frame shows the frame buffer copied at hookrun row k? (compare fb dumps with full.frames)"""
import sys, os
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import sheet
d = np.load(sys.argv[1])
raw, W, H, rec, hdr, n = sheet.open_frames(sys.argv[2])
for name in d.files:
    if not name.startswith('fb_'):
        continue
    k = int(name[3:]); fb = d[name].reshape(200, 320)
    fc = int(d['frame_copy'][k])
    res = []
    for f in range(fc - 1, fc + 3):
        o = hdr + f * rec
        idx = np.asarray(raw[o + 776:o + 776 + W * H]).reshape(H, W)[:200, :320]
        res.append((f, int((idx != fb).sum())))
    print('row', k, 'transform frame', d['frame'][k], 'copy frame', fc, 'capture diffs', res)

"""Model frame next to the capture refresh that shows it (RGB through the captured DAC), as runs/cmp_<scene>_<refresh>.png.

usage: python3 compare_capture.py gen/gen.ini 5951 runs/landall 6500 7400
       (ini, the capture refresh at which GenericEffect started, the --gen-dump prefix, capture refreshes)
"""
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, '../..')
from sheet import open_frames, indexed
import render
from gen import idiv

ini, start, prefix = sys.argv[1], int(sys.argv[2]), sys.argv[3]
sc = render.Scene(ini)
draws, shows = [], []
for line in open(prefix + '.gentxt'):
    f = line.split()
    if f[0] == 'draw':
        draws.append((int(f[3]), f[9], [[int(x) for x in s.split(':')[1].split(',')] for s in f[10:]]))
    else:
        shows.append((int(f[2]), '%x' % int(f[8], 16)))
raw, _ = open_frames('../../part2.frames')
for R in map(int, sys.argv[4:]):
    r, page = [s for s in shows if s[0] <= R][-1]
    k = [i for i, d in enumerate(draws) if d[1] == page and d[0] <= r][-1]
    ts = [sc.t_from_pos(o, draws[k][2][i][0]) for i, o in enumerate(sc.objs)]
    model = render.render_frame(sc, ts)
    px, pal = indexed(raw, R)
    lut = ((pal.astype(np.uint32) * 255 + 31) // 63).astype(np.uint8)
    print('refresh %d shows draw %d (t = %d, lag %d): model == capture: %s' % (
        R, k, ts[0], R - start - ts[0], (model == px).all()))
    img = np.concatenate([lut[model.astype(np.uint8)], np.zeros((200, 4, 3), np.uint8), lut[px]], 1)
    Image.fromarray(img).resize((2 * img.shape[1], 400), Image.NEAREST).save('runs/cmp_%s_%d.png' % (ini.split('/')[0], R))

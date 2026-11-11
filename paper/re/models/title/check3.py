import pickle, struct, sys
import numpy as np
sys.path.insert(0, '.')
import model
import math
# exact sine table model
x = 0.0; st = struct.unpack('<f', struct.pack('<f', 0.003067961661145091))[0]; Sx = []
for k in range(1024):
    Sx.append(int(round(math.sin(x)*65536))); x += st; x += st
model.S = np.array(Sx)
tex = model.tex(); t1, t2 = model.shade_tables()
for run, t, shx in [('r1', 400, 7), ('r1', 900, 7), ('r1', 1000, 7), ('r2', 11000, 5), ('r2', 11100, 5), ('r2', 11300, 5)]:
    d = f'{run}_dumps/'
    o = np.frombuffer(open(d + f'fs_osc_t{t}.bin', 'rb').read(), '<i4').reshape(72, 3)
    osc = [tuple(int(v) for v in r) for r in o]
    g = np.frombuffer(open(d + f'aw_grid_t{t}.bin', 'rb').read(), '<i4').reshape(26, 41, 3)
    P, X, Y, Ag, Bg = model.warp_grid(osc, shx=shx)
    okA = np.array_equal(g[:, :, 0], Ag); okB = np.array_equal(g[:, :, 1], Bg)
    img = model.render_blocks(g[:, :, 0].astype(np.int64), g[:, :, 1].astype(np.int64), tex, t2[10])
    bb = np.frombuffer(open(d + f'aw_bb_t{t}.bin', 'rb').read(), np.uint8).reshape(200, 320)
    print(run, t, 'grid A', okA, 'grid B', okB, 'render match', np.array_equal(img, bb), (img != bb).sum(),
          'X range', X.min(), X.max(), 'Y range', Y.min(), Y.max(), 'bb max', bb.max())

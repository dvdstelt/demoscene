import pickle, struct, sys
import numpy as np
sys.path.insert(0, '.')
import model
snap = pickle.load(open(sys.argv[1], 'rb')) if False else None
g, _ = model.font(); sp = model.sprites(g)
for run, t in [('r1', 400), ('r1', 600), ('r1', 900), ('r1', 1000), ('r2', 11000), ('r2', 11100), ('r2', 11200), ('r2', 11300)]:
    d = f'{run}_dumps/'
    count, ang, lvl, ctr = struct.unpack('<4I', open(d + f'fs_vars_t{t}.bin', 'rb').read())
    rec = open(d + f'al_rec_t{t}.bin', 'rb').read()
    rec2 = open(d + f'aw_rec_t{t}.bin', 'rb').read()
    bb0 = np.frombuffer(open(d + f'aw_bb_t{t}.bin', 'rb').read(), np.uint8).astype(np.int64).reshape(-1)
    bb1 = np.frombuffer(open(d + f'al_bb_t{t}.bin', 'rb').read(), np.uint8).reshape(-1)
    pad = 20000
    buf = np.zeros(64000 + 2*pad, np.int64); buf[pad:pad+64000] = bb0
    # positions in rec are absolute pointers: recover the back-buffer base from record 0 (x=5,y=50)
    base = struct.unpack_from('<I', rec, 4)[0] - (50*320 + 5)
    n = 0
    for i in range(count):
        ch = rec[24*i]
        if ch == 0: break
        if ch == 32: continue
        n += 1
        pos = struct.unpack_from('<I', rec, 24*i+4)[0] - base
        offs = struct.unpack_from('<4i', rec, 24*i+8)
        s = sp[ch - 97][:, 0::2].astype(np.int64)
        for k, o in enumerate(offs):
            p = pad + pos + o
            for r in range(40):
                seg = buf[p + r*320: p + r*320 + 40]
                if k == 0: seg[:] = np.maximum(0, seg - s[r])
                else: seg[:] = np.minimum(255, seg + (s[r] >> 1))
    out = buf[pad:pad+64000].astype(np.uint8)
    print(run, t, 'count', count, 'drawn', n, 'counter', ctr, 'angle', ang, 'level', hex(lvl), 'match', np.array_equal(out, bb1), (out != bb1).sum(), 'recs changed during frame', rec != rec2)

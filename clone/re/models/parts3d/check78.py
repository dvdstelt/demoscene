"""Check the model of parts 7 and 8 against run_clone.py --p3d dumps: state recurrences, matrices, deformed
vertices, the tile band in the shown frames (--flip-frames run).

usage: python3 check78.py P.pkl P.frames P.flips
"""
import os, sys, pickle, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import numpy as np
import model
import rep
from sheet import open_frames, to_rgb, read_flips

recs = pickle.load(open(sys.argv[1], 'rb'))
raw, w, h, rec, hdr, n, _ = open_frames(sys.argv[2])
flips, flip0 = read_flips(sys.argv[3])
T = [struct.unpack('<i', rep.rd(0x428164 + 4 * i, 4))[0] for i in range(192)] + [0] * 64
mem = None
part = recs[0]['part']
orig = None
# the geosphere as the set-up converts it: int16 -> float
import pickle as pk, unicorn  # noqa
snap = pk.load(open(os.path.join(os.path.dirname(__file__), '..', '..', 'snaps', 't11.snap'), 'rb'))['mem']
nv = struct.unpack('<i', snap[0xf72d44:0xf72d48])[0]
orig = np.frombuffer(snap[0xf61404:0xf61404 + 12 * nv], '<i2').reshape(nv, 6)[:, :3].astype(np.float32).astype(float)

maxerr_state, maxerr_v, tile_bad, tile_n = 0, 0, 0, 0
tiles_checked = [0, 0]
t70 = []
for r in recs:
    if 'after' not in r: continue
    b, a, dt = r['before'], r['after'], r['dt']
    if part == 7:
        s = {'angle': np.float32(b[0xf61400]), 'phase': np.float32(b[0xf613f4]), 'fade': np.float32(b[0x427f80]),
             'n701': r['ints'][0x427f74]}
        light = model.part7_step(s, dt)
        M = model.matrix(float(s['angle']), float(s['angle']) * 1.1, float(s['angle']) * 0.8, 4 / 3)
        rot = model.transform(M, orig).astype(np.float32).astype(float)
        out = model.part7_deform(s, dt, orig, rot)
        e = max(abs(s['angle'] - a[0xf61400]), abs(s['phase'] - a[0xf613f4]), abs(s['fade'] - a[0x427f80]),
                abs(light - a[0x427f70]))
    else:
        s = {'tilet': np.float32(b[0xf72d70]), 'Z': np.float32(b[0x427fe4]), 'fade': np.float32(b[0x427fec]),
             'angle': np.float32(b[0xf72d6c]), 'phase': np.float32(b[0x427ffc]), 'light': np.float32(b[0x427f70]),
             'e802': b[0x427fe8], 'cols': r['ints'][0x427ff0]}
        model.part8_step(s, dt)
        M = model.matrix(float(s['angle']), float(s['angle']) * 1.1, float(s['angle']) * 0.8, float(np.float32(1.4814814)))
        rot = model.transform(M, orig).astype(np.float32).astype(float)
        out = model.part8_deform(s, dt, orig, rot)
        e = max(abs(s['tilet'] - a[0xf72d70]), abs(s['Z'] - a[0x427fe4]), abs(s['fade'] - a[0x427fec]),
                abs(s['angle'] - a[0xf72d6c]), abs(s['phase'] - a[0x427ffc]), abs(s['light'] - a[0x427f70]))
    maxerr_state = max(maxerr_state, e)
    dst = np.frombuffer(r['dst'], '<f4').reshape(-1, 13)[:, :3].astype(float)
    ev = np.abs(dst - out).max()
    maxerr_v = max(maxerr_v, ev)
    # tiles: the shown frame of this flip (record = flip - 1 - flip0)
    i = r['flip'] - 1 - flip0
    if 0 <= i < n:
        img = to_rgb(raw, i, w, h, rec, hdr).astype(np.int64)
        if part == 7:
            ts = {'fade': s['fade'], 'n701': s['n701']}
        else:
            ts = {'tilet': s['tilet'], 'fade': s['fade'], 'cols': s['cols']}
        mt, wr = model.tiles(T, part, ts)
        # pixels the object does not cover: compare where the frame equals one of the model's flat tile colours
        same = (img == mt).all(2) & wr
        tile_n += wr.sum(); tile_bad += (wr & ~same).sum()
        r['tile_same'] = same.sum() / wr.sum()
        # per 40x24 tile: the most common colour of the frame region must be the model's when it covers > 25 %
        for y0 in range(4, 196, 24):
            for x0 in range(0, 320, 40):
                reg = img[y0:y0 + 24, x0:x0 + 40].reshape(-1, 3)
                cols, cnt = np.unique(reg, axis=0, return_counts=True)
                j = cnt.argmax()
                if cnt[j] > 240 and not (cols[j] == 255).all():
                    tiles_checked[0] += 1
                    if not (cols[j] == mt[y0, x0]).all():
                        tiles_checked[1] += 1
                        if tiles_checked[1] < 5: print('tile mismatch flip', r['flip'], y0, x0, cols[j], mt[y0, x0])
    r['ev'] = ev
    t70.append((r['timer'] * 70 / 1193182 if r['timer'] is not None else None, dt, r['flip']))
print('part %d: %d frames; max state error %.3g; max vertex error %.3g (coordinates up to ~%d)' % (
    part, len([r for r in recs if 'after' in r]), maxerr_state, maxerr_v, np.abs(dst).max()))
same = [r.get('tile_same', np.nan) for r in recs if 'after' in r]
print('tile pixels equal to the model: min %.4f median %.4f of the written pixels per frame (the rest is the object)'
      % (np.nanmin(same), np.nanmedian(same)))
print('tiles whose dominant colour was checked: %d, mismatches: %d' % tuple(tiles_checked))

"""How much the truncating feedback of 0x12111 (H = (7H + C) >> 3, C = work >> 3) differs from a continuous EMA,
on the work buffers recorded over the whole TBL part (out/tblall.tbl.pkl). Prints the mean displayed level of
(a) the original integer recurrence, (b) a float EMA of the float input work/8, (c) the float EMA with a bias
of -7/16 per step (the mean of the truncation), clamped at 0.

usage: python3 ema_bias.py out/tblall.tbl.pkl
"""
import pickle
import sys

import numpy as np

F = pickle.load(open(sys.argv[1], 'rb'))[1:]
H = np.frombuffer(F[0]['hist0'], np.uint8)[:64000].astype(np.int64)
Hf = H.astype(np.float64)
Hb = H.astype(np.float64)
for k, f in enumerate(F):
    w = np.frombuffer(f['preshr'], np.uint8).astype(np.int64)
    H = (7 * H + (w >> 3)) >> 3
    Hf = (7 * Hf + w / 8.0) / 8
    Hb = np.maximum((7 * Hb + w / 8.0) / 8 - 7 / 16, 0)
    if k % 40 == 0 or k == len(F) - 1:
        rec = np.frombuffer(f['buf'], np.uint8)[0x10000:0x10000 + 64000].astype(np.int64)
        assert (rec == H).all()
        print(f"frame {k:3d} tick {f['tick']}: mean H int {H.mean():6.3f}  float {Hf.mean():6.3f}  float-7/16 "
              f"{Hb.mean():6.3f}  |int - float| {np.abs(H - Hf).mean():5.3f}  |int - (float-7/16)| {np.abs(H - Hb).mean():5.3f}")

# best constant bias b for Hb = max(0, (7 Hb + w/8)/8 - b) against the integer recurrence
for b in (0.25, 0.3, 0.33, 0.36, 0.4, 7 / 16):
    H = np.frombuffer(F[0]['hist0'], np.uint8)[:64000].astype(np.int64)
    Hb = H.astype(np.float64)
    err, n, md = 0.0, 0, 0.0
    for k, f in enumerate(F):
        w = np.frombuffer(f['preshr'], np.uint8).astype(np.int64)
        H = (7 * H + (w >> 3)) >> 3
        Hb = np.maximum((7 * Hb + w / 8.0) / 8 - b, 0)
        if k >= 40:
            err += np.abs(H - Hb).mean(); md += (Hb - H).mean(); n += 1
    print(f'bias {b:.4f}: mean |int - float| {err / n:.3f}, mean (float - int) {md / n:+.3f}')

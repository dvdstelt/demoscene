"""Do the background spheres always cover the whole 320x200 viewport? (The parts never clear the page, and the engine
drops every face with a vertex at z <= 16.) Samples each part every 0.25 s with the model camera and counts pixels
not covered by any front face. usage: python3 coverage.py"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, camera, objects
PIT = 1193182
parts = [('tube', 0x157fa, 1, 0, 56226289, 3278, 4046), ('spiky', 0x15973, 3, 1000000, 78958082, 4430, 5199),
         ('blobs', 0x159cb, 4, 0, 139649690, 7502, 9038)]
for name, scr, path, t0, timer0, tick_a, tick_b in parts:
    ts, dist, objs = objects.run(scr)
    bg = objs[0]
    tri = np.array(bg.F)
    worst = (0, None)
    dur = (tick_b - tick_a) * 19759
    for T in range(0, dur, PIT // 4):
        Tc = np.float32(np.float32(T + t0) * np.float32(ts * 1e-9))
        eye, tgt, M, trans, roll = camera.camera(Tc, path, dist)
        c = (M.astype(np.float64) @ (bg.V.astype(np.float64) - tgt).T).T
        z = c[:, 2] + dist
        sx = 160 + c[:, 0] * dist / np.maximum(z, 1e-6); sy = 100 - c[:, 1] * dist / np.maximum(z, 1e-6)
        cov = np.zeros((200, 320), bool)
        Y, X = np.mgrid[0:200, 0:320] + 0.5
        for i0, i1, i2 in tri:
            if min(z[i0], z[i1], z[i2]) <= 16: continue
            x0, y0, x1, y1, x2, y2 = sx[i0], sy[i0], sx[i1], sy[i1], sx[i2], sy[i2]
            area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
            if area <= 0: continue
            w0 = (x1 - X) * (y2 - Y) - (x2 - X) * (y1 - Y); w1 = (x2 - X) * (y0 - Y) - (x0 - X) * (y2 - Y)
            cov |= (w0 >= 0) & (w1 >= 0) & (area - w0 - w1 >= 0)
        miss = (~cov).sum()
        if miss > worst[0]: worst = (miss, T / PIT)
    print(f'{name}: worst uncovered pixels {worst[0]} at {worst[1]} s after the part start')

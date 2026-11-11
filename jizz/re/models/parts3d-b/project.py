"""Check the camera + projection + env-map coordinates against the engine's per-vertex results in a dump taken
after rendering (run_jizz_b.py --dump-at 0x15c3a / 0x11130).

usage: python3 project.py DUMP.pkl
Projection (engine 0x19118 / 0x19022 / 0x18ea1, 320x200 viewport, [0x18d64] = 320, [0x18d68] = 200):
  (xc, yc, zc) = M (p - target'),  z = zc + dist,  sx = 160 + xc * dist / z,  sy = 100 - yc * dist / z
  env u = 128 + 128 * (M row 0 . n/|n|), env v = 128 + 128 * (M row 1 . n/|n|)   (texels, 16.16 in memory)
"""
import sys, os, pickle, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import camera

d = pickle.load(open(sys.argv[1], 'rb')); m = d['mem']
rd = lambda a: struct.unpack_from('<I', m, a)[0]
rf = lambda a: struct.unpack_from('<f', m, a)[0]
T, k, dist = rf(0x159f7), rd(0x157a0), rf(0x154b4)
eye, tgt, M, trans, roll = camera.camera(T, k, dist)
O, VB = rd(0x18d50), rd(0x18d4c)
print('dump tick', d['tick'], 'T', T, 'path', k, 'objects', rd(0x18d58))
for oi in range(rd(0x18d58)):
    o = O + 0x50 * oi
    v0, nv = rd(o + 8), rd(o + 12)
    raw = np.frombuffer(m[VB + 0x40 * v0: VB + 0x40 * (v0 + nv)], np.float32).reshape(nv, 16)
    flags = raw[:, 15].view(np.uint32)
    p = raw[:, 0:3].astype(np.float64)
    c = (M.astype(np.float64) @ (p - tgt).T).T
    z = c[:, 2] + dist
    ok = (flags >> 8 & 0xff) == 1            # +0x3d = 1: in front of the near plane (z > 16) and on screen
    sx = 160 + c[:, 0] * dist / z; sy = 100 - c[:, 1] * dist / z
    ex, ey = raw[:, 6] / 65536, raw[:, 7]          # +0x18 is 16.16, +0x1c plain pixels
    n = raw[:, 8:11].astype(np.float64); n /= np.linalg.norm(n, axis=1, keepdims=True)
    eu = 128 + 128 * (n @ M[0]); ev = 128 + 128 * (n @ M[1])
    eue, eve = raw[:, 13] / 65536, raw[:, 14] / 65536
    zz = raw[:, 5]
    vis = z > 16
    print(f' obj {oi}: {nv} verts, {vis.sum()} with z > 16: max|dsx| {np.abs(sx - ex)[vis].max():.4f} max|dsy| {np.abs(sy - ey)[vis].max():.4f} px, '
          f'max|dz| {np.abs(z - zz).max():.4f}, max|d env u| {np.abs(eu - eue)[vis].max():.4f} v {np.abs(ev - eve)[vis].max():.4f} texels')

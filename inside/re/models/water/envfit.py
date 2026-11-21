"""Fit the per-vertex texture coordinates the rasterizers get (vertex words +0x28 / +0x2a) against the vertex
normals turned into camera space: the environment mapping of the obuke and the fish (run1.pkl, full frames)."""
import pickle
import numpy as np

D = pickle.load(open('run1.pkl', 'rb'))
for k in (1, 2, 3):
    f, g = D['frames'][k], D['frames'][k + 1]
    rows = {'obuke': [], 'fish': []}
    seen = set()
    for name, addr, face, vs, uv in f['faces']:
        for v, t in zip(vs, uv):
            key = (name, tuple(v[2:5]), tuple(v[13:16]), v[5], v[6])
            if key in seen:
                continue
            seen.add(key)
            rows[name].append((v, t))
    for name in ('obuke', 'fish'):
        R = rows[name]
        n = np.array([v[13:16] for v, t in R], float)
        uv = np.array([t for v, t in R], float)
        if name == 'obuke':
            M = np.array(f['obj_mat']).reshape(3, 3)
            nc = n @ M.T                                 # camera = identity: n_cam = M n
            A = np.c_[nc, np.ones(len(nc))]
            for j, lab in enumerate('uv'):
                c, res, *_ = np.linalg.lstsq(A, uv[:, j], rcond=None)
                err = np.abs(A @ c - uv[:, j]).max()
                print('frame %d %s %s = %s . n_cam + %.2f   (max err %.2f, %d vertices, |n| %.1f..%.1f)' % (
                    k, name, lab, np.round(c[:3], 4), c[3], err, len(R), np.linalg.norm(n, axis=1).min(),
                    np.linalg.norm(n, axis=1).max()))
        else:
            # which fish: match the vertex's camera z (field 7 / 128) against each fish's transformed vertices
            best = []
            for v, t in R:
                cand = []
                for i in range(8):
                    M = np.array(g['fish'][i]['mat']).reshape(3, 3)
                    p = M @ np.array(v[2:5], float) + np.array(g['fish'][i]['pos'], float) + [0, 0, 1600]
                    sx = 160 + 256 * p[0] / p[2]
                    cand.append((abs(sx * 16 - v[5]) + abs(v[7] / 128 - p[2]), i, M))
                e, i, M = min(cand, key=lambda q: q[0])
                best.append((M @ np.array(v[13:16], float), t, e))
            nc = np.array([b[0] for b in best])
            A = np.c_[nc, np.ones(len(nc))]
            for j, lab in enumerate('uv'):
                c, res, *_ = np.linalg.lstsq(A, uv[:, j], rcond=None)
                err = np.abs(A @ c - uv[:, j]).max()
                print('frame %d %s %s = %s . n_cam + %.2f   (max err %.2f, %d vertices, match err max %.1f)' % (
                    k, name, lab, np.round(c[:3], 4), c[3], err, len(R), max(b[2] for b in best)))

print('--- modulo 4096 (the texture wraps every 256 texels = 4096 in 1/16 texel)')
f = D['frames'][1]
M = np.array(f['obj_mat']).reshape(3, 3)
seen = set()
pts = []
for name, addr, face, vs, uv in f['faces']:
    if name != 'obuke':
        continue
    for v, t in zip(vs, uv):
        key = (tuple(v[2:5]), tuple(v[13:16]))
        if key in seen:
            continue
        seen.add(key)
        pts.append((M @ np.array(v[13:16], float), t, v))
for su, sv in ((-2.5, 2.5), (2.5, -2.5), (-2.5, -2.5), (2.5, 2.5)):
    eu = [((t[0] - su * n[0] - 2048 + 2048) % 4096) - 2048 for n, t, v in pts]
    ev = [((t[1] - sv * n[1] - 2048 + 2048) % 4096) - 2048 for n, t, v in pts]
    print('u = 2048 %+.1f nx: err %.1f..%.1f   v = 2048 %+.1f ny: err %.1f..%.1f' % (
        su, min(eu), max(eu), sv, min(ev), max(ev)))
for n, t, v in pts[:12]:
    print(np.round(n, 1), t, v[5] / 16, v[6] / 16, v[7] / 128)

print('--- 0x41acc4: u = -((trunc(2048 + 2n.r0)) & 0xfff) - px/64, v = (trunc(2048 + 2n.r1) & 0xfff) + py/64')


def tdiv64(x):
    return int(x / 64) if x >= 0 else -int(-x / 64)


for k in (1, 2, 3):
    f, g = D['frames'][k], D['frames'][k + 1]
    bad = tot = 0
    for name, addr, face, vs, uv in f['faces']:
        for v, t in zip(vs, uv):
            if name == 'obuke':
                M = np.array(f['obj_mat']).reshape(3, 3)
            else:
                cand = []
                for i in range(8):
                    Mi = np.array(g['fish'][i]['mat']).reshape(3, 3)
                    p = Mi @ np.array(v[2:5], float) + np.array(g['fish'][i]['pos'], float) + [0, 0, 1600]
                    cand.append((abs(160 + 256 * p[0] / p[2] - v[5] / 16) + abs(p[2] - v[7] / 128), i))
                M = np.array(g['fish'][min(cand)[1]]['mat']).reshape(3, 3)
            n2 = 2 * np.array(v[13:16], float)
            a = (n2[1] * M[0][1] + n2[0] * M[0][0]) + n2[2] * M[0][2]
            b = (n2[1] * M[1][1] + n2[0] * M[1][0]) + n2[2] * M[1][2]
            u = -(int(np.trunc(a + 2048)) & 0xfff) - tdiv64(v[8])
            w = (int(np.trunc(b + 2048)) & 0xfff) + tdiv64(v[9])
            u = ((u + 0x8000) & 0xffff) - 0x8000
            w = ((w + 0x8000) & 0xffff) - 0x8000
            tot += 1
            if (u, w) != tuple(t):
                bad += 1
                if bad <= 3:
                    print('   ', name, (u, w), t)
    print('frame %d: %d of %d vertex u,v differ' % (k, bad, tot))

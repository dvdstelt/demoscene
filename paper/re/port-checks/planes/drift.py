import numpy as np, pickle
fr = pickle.load(open('flock_frames.pkl', 'rb'))
for f in (1, 100, 500, 1000, 1050):
    ms = [np.array(m).reshape(3, 3) / 65536 for m, wp in fr[f][0]]
    norms = np.array([np.linalg.norm(m, axis=0) for m in ms])
    dets = [np.linalg.det(m) for m in ms]
    V = np.array(fr[f][1]).reshape(3, 3) / 65536
    print(f, 'col norms %.4f..%.4f' % (norms.min(), norms.max()), 'det %.4f..%.4f' % (min(dets), max(dets)), 'V norms', np.linalg.norm(V, axis=1).round(4), 'Vdet %.4f' % np.linalg.det(V))
# max per-step rotation angle of planes and camera
mx = 0; mxc = 0
for f in range(2, 1060):
    for (a, _), (b, _) in zip(fr[f - 1][0], fr[f][0]):
        A = np.array(a).reshape(3, 3) / 65536; B = np.array(b).reshape(3, 3) / 65536
        R = A.T @ B; ang = np.degrees(np.arccos(np.clip((np.trace(R) - 1) / 2, -1, 1))); mx = max(mx, ang)
    A = np.array(fr[f - 1][1]).reshape(3, 3) / 65536; B = np.array(fr[f][1]).reshape(3, 3) / 65536
    R = A @ B.T; mxc = max(mxc, np.degrees(np.arccos(np.clip((np.trace(R) - 1) / 2, -1, 1))))
print('max plane turn per step %.1f deg, camera %.1f deg' % (mx, mxc))

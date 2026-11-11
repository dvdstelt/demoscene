import numpy as np
RE = '/home/dennis/Downloads/acme_303/paper/re/'
col5 = {}
for l in open(RE + 'full.ticks'):
    a = l.split(); col5[int(a[0])] = int(a[4])
ts = np.arange(8818, 10968); G = np.cumsum([col5[t] for t in ts]) - 2
print('frames by 10967:', G[-1], 'first frame ticks', [(t, g) for t, g in zip(ts[:12], G[:12])])
# least squares G ~ r (t - t0), over the part, excluding the first ticks
for lo in (8818, 8830):
    m = ts >= lo
    A = np.vstack([ts[m], np.ones(m.sum())]).T
    (r, b), *_ = np.linalg.lstsq(A, G[m], rcond=None)
    res = G[m] - (r * ts[m] + b)
    print('fit from', lo, 'rate/tick %.6f = %.3f fps' % (r, r * 70.0018), 't0 = %.3f' % (-b / r), 'resid min %.2f max %.2f rms %.2f' % (res.min(), res.max(), res.std()))
# fixed 34 fps
r = 34 / 70.0018
for t0 in (8816.0, 8817.0):
    res = G - r * (ts - t0)
    print('34 fps t0', t0, 'resid', res.min(), res.max(), 'end', res[-1])
# windowed fps
for a in range(8818, 10968, 215):
    m = (ts >= a) & (ts < a + 215)
    print(a, '%.1f fps' % ((G[m][-1] - G[m][0]) / (m.sum() - 1) * 70.0018))
r = 34 / 70.0018
res = G - (1 + r * (ts - 8818))
print('natural start: resid min %.2f max %.2f mean %.2f' % (res.min(), res.max(), res.mean()))
# minimax phase for 34 fps: sigma = r (t - t0)
d = G - r * ts
t0 = -(d.max() + d.min()) / 2 / r
res = G - r * (ts - t0)
print('minimax t0 %.3f resid %.2f..%.2f, sigma(8818)=%.2f' % (t0, res.min(), res.max(), r * (8818 - t0)))
for t in range(8818, 10968, 100): print(t, G[t - 8818], '%.1f' % (1 + r * (t - 8818)), '%.1f' % (r * (t - t0)))

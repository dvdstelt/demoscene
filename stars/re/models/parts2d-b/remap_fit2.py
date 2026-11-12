import numpy as np
from face_data import file35
f, pal, tables = file35()
P = pal.astype(np.float64)
tb = tables[15].astype(int)
print('table15', tb.tolist())
print('table16', tables[16].tolist())
def score(target, metric, first=True):
    if metric == 'sq': d = ((target[:, None, :] - P[None]) ** 2).sum(-1)
    elif metric == 'abs': d = np.abs(target[:, None, :] - P[None]).sum(-1)
    elif metric == 'max': d = np.abs(target[:, None, :] - P[None]).max(-1)
    elif metric == 'lum': d = ((target[:, None, :] - P[None]) ** 2 * np.array([0.3, 0.59, 0.11])).sum(-1)
    exact = (d.argmin(1) == tb).sum() if first else None
    tie = (d[np.arange(256), tb] <= d.min(1) + 1e-9).sum()
    return exact, tie
for metric in ('sq', 'abs', 'max', 'lum'):
    for name, tgt in (('x15/16', P * 15 / 16), ('floor x15/16', np.floor(P * 15 / 16)), ('round', np.round(P * 15 / 16)),
                      ('x15>>4 int', (pal.astype(int) * 15) >> 4)):
        print(metric, name, score(np.asarray(tgt, float), metric))
# how does table 15 act on (ramp level i, block b)?  c = 32*b + i
for b in range(8):
    print(b, [(int(t) // 32, int(t) % 32) for t in tb[32 * b:32 * b + 32]])

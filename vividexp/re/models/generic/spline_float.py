"""Compare the fixed-point spline (0x413870 / 0x413b90, then >> 8) with a double-precision natural cubic spline."""
import numpy as np
import gen


def natural(y):
    n = len(y)
    y = np.asarray(y, float)
    y2 = np.zeros(n); u = np.zeros(n)
    for i in range(1, n - 1):
        sig = 0.5
        p = sig * y2[i - 1] + 2
        y2[i] = (sig - 1) / p
        d = (y[i + 1] - y[i]) - (y[i] - y[i - 1])
        u[i] = (6 * d / 2 - sig * u[i - 1]) / p
    for k in range(n - 2, -1, -1):
        y2[k] = y2[k] * y2[k + 1] + u[k]
    return y2


def ev(y, y2, x):
    lo = min(int(np.floor(x)), len(y) - 2)
    b = x - lo; a = 1 - b
    return a * y[lo] + b * y[lo + 1] + ((a ** 3 - a) * y2[lo] + (b ** 3 - b) * y2[lo + 1]) / 6


for spl in ('gen/land.spl', 'gen2/path.spl'):
    cols = gen.read_spl(spl)
    for c in range(6):
        rec = gen.spline_prep(cols[c])
        y2 = natural(cols[c])
        n = len(cols[c])
        worst = 0
        for pos in range(0, (n - 1) << 16, 997):
            fx = gen.spline_eval(rec, pos) >> 8
            fl = ev(cols[c], y2, pos / 65536)
            worst = max(worst, abs(fx - fl))
        print(spl, 'col', c, 'max |fixed - double| =', round(worst, 3))

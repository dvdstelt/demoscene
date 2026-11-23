"""flare.shd as an additive colour per row: fit A_r (6-bit RGB) so that clamp(pal[c] + A_r, 63) ~ pal[shd[r][c]] over
all 256 background colours c, and report the error, for the rows the temple uses (flare.raw's values, 32..69 for the
pipes). Also temple.int against the plain average of the two colours."""
import numpy as np
from tables import pal, shd, tint, fl

P = pal.astype(float)


def fit_row(r):
    target = P[shd[r]]
    A = np.clip((target - P).mean(0), 0, 63)
    grid = np.arange(0, 64, 0.25)
    for it in range(3):
        for ch in range(3):
            errs = [np.abs(np.minimum(P[:, ch] + a, 63) - target[:, ch]).mean() for a in grid]
            A[ch] = grid[int(np.argmin(errs))]
    err = np.abs(np.minimum(P + A, 63) - target)
    return A, err.mean(), np.percentile(err, 95)


if __name__ == '__main__':
    used = sorted(set(np.unique(fl).tolist()))
    print('rows of flare.raw texels:')
    for r in used:
        A, e, e95 = fit_row(r)
        print('  %3d  A = (%5.2f %5.2f %5.2f)  mean |err| %.2f  95%% %.1f   onto black %s' % (r, *A, e, e95, pal[shd[r, 0]].tolist()))
    print('rows of the pipes (32 + level):')
    for r in range(32, 70):
        A, e, e95 = fit_row(r)
        print('  %3d  A = (%5.2f %5.2f %5.2f)  mean |err| %.2f  95%% %.1f' % (r, *A, e, e95))
    avg = (P[:, None, :] + P[None, :, :]) / 2
    err = np.abs(P[tint] - avg)
    print('temple.int vs (pal[a] + pal[b]) / 2: mean |err| %.2f per channel (6-bit), 95%% %.1f' % (err.mean(), np.percentile(err, 95)))

"""Analyse the credits blend tables (tunnel.int, wtrmask.int) and shade tables (tunnel.shd, smoke.shd) against
tunnel.pal: for each table row (level), fit the output colour as a function of the input colour."""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile
d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
pal = np.frombuffer(d.get('credits/tunnel.pal'), np.uint8).reshape(256, 3).astype(float)


def nearest(rgb):
    dd = ((pal[None, :, :] - rgb[:, None, :]) ** 2).sum(-1)
    return dd.argmin(1), dd.min(1)


def fit(name, rows, cols=256):
    t = np.frombuffer(d.get(name), np.uint8).reshape(rows, cols)
    print(name, t.shape)
    for L in list(range(0, rows, max(1, rows // 16))) + [rows - 1]:
        out = pal[t[L]]
        src = pal[np.arange(cols)] if cols == 256 else None
        # model a: out = src * k ; model b: out = src + (63 - src) * k ; model c: out = src + k
        ks = []
        for model in ('mul', 'towhite', 'add'):
            best = None
            for k in np.linspace(-1, 2, 601) if model != 'add' else np.linspace(-63, 63, 253):
                if model == 'mul': m = src * k
                elif model == 'towhite': m = src + (63 - src) * k
                else: m = src + k
                m = np.clip(m, 0, 63)
                e = np.abs(pal[nearest(m)[0]] - out).sum(1).mean()
                if best is None or e < best[0]: best = (e, k)
            ks.append('%s k=%.3f err=%.2f' % (model, best[1], best[0]))
        print('  L=%3d' % L, ' | '.join(ks), ' identity rows=%d' % (t[L] == np.arange(cols)).sum())


if __name__ == '__main__':
    fit('credits/tunnel.int', 256)
    fit('credits/wtrmask.int', 256)


def linfit(name, levels=32):
    """Least-squares out = a * src + b per level (6-bit units), over the 256 input colours."""
    t = np.frombuffer(d.get(name), np.uint8).reshape(256, 256)
    print(name, 'rows >= %d all equal to' % levels, np.unique(t[levels:]))
    src = pal.reshape(-1)
    for L in range(levels):
        out = pal[t[L]].reshape(-1)
        A = np.stack([src, np.ones_like(src)], 1)
        (a, b), *_ = np.linalg.lstsq(A, out, rcond=None)
        res = np.abs(A @ [a, b] - out)
        # alternative: nearest colour of the model, compare
        m = np.clip(pal * a + b, 0, 63)
        idx, _ = nearest(m)
        same = (idx == t[L]).mean()
        print('  L=%2d a=%.3f b=%6.2f  mean |res|=%.2f  nearest(model)==table %.0f%%' % (L, a, b, res.mean(), same * 100))


def modelfit(name, levels=32):
    """Per level, the best k for out = nearest(clip(src + k)) and out = nearest(clip(src * k)); score = mean RGB
    distance (6-bit units) between the table's colour and the model's exact (unquantised) colour, and how often the
    nearest palette colour to the model is the table's entry."""
    t = np.frombuffer(d.get(name), np.uint8).reshape(256, 256)
    for L in range(levels):
        out = pal[t[L]]
        res = []
        for model, ks in (('add', np.arange(-63, 64, 0.5)), ('mul', np.arange(0, 3.0, 0.01))):
            best = None
            for k in ks:
                m = np.clip(pal + k if model == 'add' else pal * k, 0, 63)
                e = np.abs(m - out).mean()
                if best is None or e < best[0]: best = (e, k, m)
            idx, _ = nearest(best[2])
            res.append('%s k=%6.2f err=%.2f hit=%3.0f%%' % (model, best[1], best[0], (idx == t[L]).mean() * 100))
        print('  L=%2d  ' % L + ' | '.join(res))


def addk(name, levels, rows=256):
    """Per level the additive offset k (6-bit units) of out = clip(src + k), from the unclipped entries."""
    t = np.frombuffer(d.get(name), np.uint8).reshape(rows, 256)
    ks = []
    for L in range(levels):
        diff = pal[t[L]] - pal
        k0 = np.median(diff)
        m = (pal + k0 > 2) & (pal + k0 < 61)
        ks.append(np.median(diff[m]) if m.any() else k0)
    return ks


def shadefit(name, rows):
    """Shade table rows: out = nearest(src * k); print k and the hit rate per row."""
    t = np.frombuffer(d.get(name), np.uint8).reshape(rows, 256)
    for L in range(0, rows, max(1, rows // 32)):
        out = pal[t[L]]
        best = None
        for k in np.arange(0, 2.5, 0.005):
            e = np.abs(np.clip(pal * k, 0, 63) - out).mean()
            if best is None or e < best[0]: best = (e, k)
        idx, _ = nearest(np.clip(pal * best[1], 0, 63))
        bestadd = None
        for k in np.arange(-63, 64, 0.5):
            e = np.abs(np.clip(pal + k, 0, 63) - out).mean()
            if bestadd is None or e < bestadd[0]: bestadd = (e, k)
        print('  row %3d mul k=%.3f err=%.2f hit=%3.0f%%   add k=%.1f err=%.2f   distinct=%d' % (
            L, best[1], best[0], (idx == t[L]).mean() * 100, bestadd[1], bestadd[0], len(np.unique(t[L]))))


def mulk(name, rows):
    """Per row the factor k of out = src * k (least squares over all colours, 6-bit units)."""
    t = np.frombuffer(d.get(name), np.uint8).reshape(rows, 256)
    ks = []
    for L in range(rows):
        out = pal[t[L]].reshape(-1); src = pal.reshape(-1)
        ks.append((src @ out) / (src @ src))
    return ks

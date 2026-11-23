"""How well the engine's colour blend reproduces the tables that read the background (src/inside/engine_gl.cpp).

    python3 tools/blend_fit.py            (in the toolbox: numpy)

The rasterizers 0x41669c (table[texel][background]) and 0x416dc8 (table[level][background]), the blended billboards
(table[texel][background]) and the type 2 / 4 bitmaps (table[src][background]) give a palette index from the index
already on screen. The remake draws them with dual-source blending: each table row r is fitted per colour channel as
    colour(table[r][j]) ~ clamp(b_r * colour(j) + s_r, 0, 1)   (least squares over the palette's 256 colours j)
with b and s in 0..1, which is what Renderer's fitRows computes (the clamp: entries where the table saturates at white
or black and the line is past it are left out of the fit; the remake's 16-bit normalised target clamps every draw,
and clamps the blend's source and factor to 0..1, hence the box). This prints, per table and the palette it is used with, the error of that fit
in 6-bit units (0..63: one unit is one step of the VGA DAC): the mean over rows of the row's RMS error, the worst
row's RMS, the largest single error, and the share of entries the fit puts within half a unit of the table's colour.
For tables indexed by a source colour (.int) it also fits one blend for the whole table, out = a src + b dst + c,
to say what the table does.
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..', 're')
sys.path.insert(0, RE)
import datfile  # noqa: E402

# (table, palette, rows used, what reads it)
TABLES = [
    ('logo/inmask.shd', 'logo/logo.pal', None, 'logo: mask bitmap, type 4 (row = mask level)'),
    ('credits/tunnel.int', 'credits/tunnel.pal', None, 'credits: name bitmaps, type 4 (row = src)'),
    ('credits/wtrmask.int', 'credits/tunnel.pal', None, 'credits: mask bitmap, type 2 (row = src)'),
    ('credits/smoke.shd', 'credits/tunnel.pal', None, 'credits: smoke billboards (row = texel)'),
    ('nigga/shadow4.sdw', 'nigga/nigga4.pal', None, 'shadow scene 4: 0x41669c (row = shadow texel)'),
    ('nigga/shadow9.sdw', 'nigga/nigga9.pal', None, 'shadow scene 3: 0x41669c (row = shadow texel)'),
    ('water/water.int', 'water/water.pal', None, 'water: 0x41669c (row = texel)'),
    ('temple/temple.int', 'temple/temple.pal', None, 'temple: pillars 0x41669c (row = texel)'),
    ('temple/flare.shd', 'temple/temple.pal', (32, 70), 'temple: pipes 0x416dc8 (rows 32 + glow level)'),
    ('temple/flare.shd', 'temple/temple.pal', None, 'temple: flare billboards (row = texel)'),
]


def box_line(x, y):
    """The least-squares line y ~ b x + s with b and s in 0..1: the free solution if it is inside, else the best of
    the four edges' own minima (as Renderer's boxLine)."""
    n = len(x)
    if n < 1:
        return 0.0, 0.0
    sx, sy, sxx, sxy = x.sum(), y.sum(), (x * x).sum(), (x * y).sum()

    def q(b, s):
        return b * b * sxx + 2 * b * s * sx + n * s * s - 2 * b * sxy - 2 * s * sy
    var = sxx - sx * sx / n
    if var > 1e-12:
        b = (sxy - sx * sy / n) / var
        s = (sy - b * sx) / n
        if 0 <= b <= 1 and 0 <= s <= 1:
            return b, s
    c = lambda v: min(max(v, 0.0), 1.0)  # noqa: E731
    cands = [(c(sxy / sxx) if sxx > 1e-12 else 0.0, 0.0), (c((sxy - sx) / sxx) if sxx > 1e-12 else 0.0, 1.0),
             (0.0, c(sy / n)), (1.0, c((sy - sx) / n))]
    return min(cands, key=lambda bs: q(*bs))


def fit_row(x, y):
    """y ~ clamp(b x + s, 0, 1) with b, s in 0..1 (the remake's target clamps the blend's source and factor): least
    squares, then again without the entries the clamp already explains (the table saturates at white or black where
    the line passes it), as Renderer's fitRows"""
    use = np.ones(len(x), bool)
    b = s = 0.0
    for _ in range(4):
        if use.sum() < 1:
            break
        b, s = box_line(x[use], y[use])
        p = b * x + s
        nu = ~(((y >= 1 - 1e-6) & (p >= y)) | ((y <= 1e-6) & (p <= y)))
        if (nu == use).all():
            break
        use = nu
    return b, s


def main():
    d = datfile.Dat(os.path.join(RE, '..', 'original', 'INSIDE.DAT'))
    for name, palname, rows_used, what in TABLES:
        t = np.frombuffer(d.get(name), np.uint8)
        rows = len(t) // 256
        t = t[:rows * 256].reshape(rows, 256)
        pal = np.minimum(np.frombuffer(d.get(palname), np.uint8)[:768].reshape(256, 3).astype(np.float64), 63) / 63
        r0, r1 = rows_used or (0, rows)
        x = pal                                   # background colours (256, 3)
        rms, mx, near = [], 0.0, 0
        for r in range(r0, min(r1, rows)):
            y = pal[t[r]]                         # the table's colours (256, 3)
            e = np.empty_like(y)
            for k in range(3):
                b, s = fit_row(x[:, k], y[:, k])
                e[:, k] = np.clip(b * x[:, k] + s, 0, 1) - y[:, k]
            rms.append(np.sqrt((e ** 2).mean()) * 63)
            mx = max(mx, np.abs(e).max() * 63)
            near += int((np.abs(e) * 63 <= 0.5).all(axis=1).sum())
        n = len(rms)
        line = '%-20s %-20s rows %3d..%3d: fit RMS %.2f (worst row %.2f), max %.1f, %.1f %% within 0.5' % (
            name, palname, r0, r0 + n - 1, float(np.mean(rms)), float(np.max(rms)), mx, 100.0 * near / (n * 256))
        if rows == 256 and rows_used is None:   # one blend for all: out = a src + b dst + c
            src = np.repeat(pal[:, None, :], 256, axis=1).reshape(-1, 3)
            dst = np.repeat(pal[None, :, :], 256, axis=0).reshape(-1, 3)
            out = pal[t].reshape(-1, 3)
            coef = []
            err = []
            for k in range(3):
                A = np.stack([src[:, k], dst[:, k], np.ones(len(out))], axis=1)
                c, *_ = np.linalg.lstsq(A, out[:, k], rcond=None)
                coef.append(c)
                err.append(A @ c - out[:, k])
            c = np.mean(coef, axis=0)
            line += '; whole table: out = %.2f src + %.2f dst + %.2f (RMS %.2f)' % (
                c[0], c[1], c[2] * 63, float(np.sqrt(np.mean(np.square(err)))) * 63)
        print(line + '  [' + what + ']')


if __name__ == '__main__':
    main()

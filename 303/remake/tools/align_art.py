"""Fits a high-resolution redraw of one of 303's MUSTARD.PAK pictures onto the original and writes art/<file>.

    python3 tools/align_art.py NAME REDRAW.png [REDRAW2.png] [--match]   fit, keep the fit, rebuild art/<file>
    python3 tools/align_art.py --upload NAME OUT.png [--part K]          the nearest-neighbour picture to upload
    python3 tools/align_art.py --sheet OUT.jpg NAME [NAME ...]           comparison sheet: original | redraw
    python3 tools/align_art.py --assemble FILE                           rebuild art/<file> from the kept fits
(inside the toolbox: numpy + PIL). Adapted from dope/remake/tools/align_art.py.

NAME is a picture of re/dump/pictures/ (the PAK file's pixels with the palette the demo shows them with, grey for
files without a palette, see re/notes-pictures.md), or one tile of a stacked file: "tex3d#1" is tile 1 of
tex3d.raw. See `SPECS` for how each is uploaded and fitted:

- tileable textures (tex3d#1, neuron, weird, blobby#0, tunnel#0) are uploaded repeated 2x2 (square texels); the
  fit is made against the repeated original, and the tile is cut out of the middle of the result with its edges
  made to wrap: across each seam the two copies of the same texels (one period apart) are joined along the path
  where they differ least, feathered over a few pixels (`make_periodic`);
- environment maps (tex3d#0, sphere, phong, tunnel#1) keep their layout (the parts read only a disc around the
  centre), square texels;
- screen pictures are shown with screen pixels 1.2 times as tall as wide; those whose shape the generator does not
  make go on a larger canvas of a shape it makes (3:1, 3:2, square), see `canvas`;
- grey pictures (final, opening1, 303b, credits) are uploaded and written as 8-bit grey;
- `parts` are row ranges uploaded and fitted on their own (final: the lettering and icons, the photo);
- `elements` are rectangles the exe reads by row and column (the lines and icons of final, the 11 glyphs of 303b,
  the 6 words of credits): after the fit of the whole, each is fitted again on its own and written exactly into
  its rectangle, so each element stays at its pixels even where the generator moved it a little.

The fit maps redraw pixel coordinates (x, y) to original pixel coordinates (u, v): u = sx * x + tx,
v = sy * y + ty. It compares both pictures in the original's grid (the redraw area-averaged into it, both blurred
so that detail the original does not have does not count): a brute-force search over scale and offset on a coarse
grid, then Nelder-Mead on (scale, aspect, tx, ty) with less and less blur. It prints the scale (relative to "the
redraw fills the upload's frame") and offset and the residual (RMS of the RGB difference 0..255 in the original's
grid, plain and with a 1-pixel blur, the share of pixels where one picture is background and the other is not,
and the correlation of the two pictures' detail, see `Fit.residual`).

Each fitted picture is kept in re/art-raw/fit/<NAME>.png (at the redraw's own resolution, covering exactly the
original's frame or tile). art/<file>.png (or .jpg for the large colour files) is then assembled from the kept
fits of all its tiles; tiles without a fit (tex3d's generated tiles 2..5, blobby's mask) are the original scaled
up bilinearly (the texture filter the remake uses on the original, so they look the same).

Options:
  --range R         search offsets up to R original pixels and scales up to 1 +- R/100 (default per picture)
  --uniform         one scale for x and y
  --match           after the fit, give the redraw the original's colour distribution: per-channel monotone tone
                    curves from quantile matching in the original's grid (`colour_curves`); shapes are kept
  --match-elements  the same for each element on its own (its rectangle against the original's)
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PICTURES = os.path.join(HERE, '..', '..', 're', 'dump', 'pictures')
FITS = os.path.join(HERE, '..', '..', 're', 'art-raw', 'fit')
ART = os.path.join(HERE, '..', 'art')
SCREEN_ASPECT = 1.2  # an original screen pixel is 1.2 times as tall as wide on a 4:3 screen

# Stacked files: tile count and size. Every other file is one picture.
STACKS = {'tex3d': 6, 'tunnel': 2, 'blobby': 2, 'bd2': 3}
# Files written as JPEG (q95): large colour pictures with fine noise.
JPEG = {'tex3d', 'neuron', 'weird', 'blobby', 'tunnel', 'bd2', 'sphere', 'phong'}

TILE = dict(aspect=1.0, up=(3, 3), tile=2, range=12)        # tileable texture: 2x2 repeat, 1536x1536 upload
ENVMAP = dict(aspect=1.0, up=(5, 5), range=12)              # 1280x1280
SCREEN = dict(aspect=SCREEN_ASPECT, up=(5, 6), range=16)    # 320x200 -> 1600x1200


def _icons():
    # final.raw icons: source columns 0x57fd4, widths 0x57ff4 (read width rounded up to 4), rows 70..109
    x = [3, 48, 99, 140, 177, 228, 272, 293]
    w = [37, 43, 34, 29, 42, 37, 14, 25]
    return [(xi, 70, min(320, xi + (wi + 3) // 4 * 4), 110) for xi, wi in zip(x, w)]


SPECS = {
    'tex3d#1': TILE,
    'neuron': TILE,
    'weird': TILE,
    'blobby#0': TILE,
    'tunnel#0': TILE,
    'tex3d#0': ENVMAP,
    'sphere': ENVMAP,
    'phong': ENVMAP,
    'tunnel#1': ENVMAP,
    # final.raw (grey): lines at rows 0x58014 {row, height}: 1+31, 32+32, 124+26, 150+200 (the photo)
    'final': dict(SCREEN, grey=True, parts=[(0, 150, 1.5), (150, 350, None)],
                  elements=[(0, 1, 320, 32), (0, 32, 320, 64)] + _icons() + [(0, 124, 320, 150)]),
    # opening1.raw (grey): only rows 0..199 are shown (screen x - 32, y)
    'opening1': dict(SCREEN, grey=True, parts=[(0, 200, 1.0)]),
    # 303b.raw (grey): glyph columns first 0x52eb8 .. last 0x52ee4 (inclusive)
    '303b': dict(SCREEN, grey=True, canvas=3.0, range=8, elements=[
        (a, 0, b + 1, 38) for a, b in zip([1, 21, 42, 60, 75, 140, 160, 180, 199, 219, 240],
                                          [15, 34, 55, 73, 138, 153, 173, 192, 213, 233, 252])]),
    # credits.raw (grey): words {x, y, source x, width} at 0x52f20: code, music, vocals, statix, vic, emma
    'credits': dict(SCREEN, grey=True, canvas=3.0, range=8, elements=[
        (a, 0, a + w, 32) for a, w in [(0, 73), (73, 96), (169, 96), (265, 91), (356, 44), (400, 106)]]),
    'bd2#0': SCREEN,
    'bd2#1': SCREEN,
    'bd2#2': SCREEN,
}


def spec(name):
    if name not in SPECS:
        sys.exit('unknown picture %s (one of %s)' % (name, ', '.join(SPECS)))
    return SPECS[name]


def split(name):
    """('tex3d', 1) for 'tex3d#1', ('final', None) for 'final'."""
    if '#' in name:
        f, k = name.split('#')
        return f, int(k)
    return name, None


def load_file(file):
    """The whole dump as RGB float (h, w, 3) and whether it is grey."""
    im = Image.open(os.path.join(PICTURES, file + '.png'))
    if im.mode == 'L':
        g = np.asarray(im, np.float64)
        return np.repeat(g[..., None], 3, axis=2), True
    return np.asarray(im.convert('RGB'), np.float64), False


def tile_rows(file, h):
    n = STACKS.get(file, 1)
    return h // n


def load_original(name):
    """The picture (or tile) as RGB float and its background colour (the commonest border colour)."""
    file, k = split(name)
    rgb, _ = load_file(file)
    if k is not None:
        th = tile_rows(file, rgb.shape[0])
        rgb = rgb[k * th:(k + 1) * th]
    border = np.concatenate([rgb[0], rgb[-1], rgb[:, 0], rgb[:, -1]])
    vals, counts = np.unique(border, axis=0, return_counts=True)
    return rgb, vals[counts.argmax()].copy()


def parts(name, rgb):
    """The pieces uploaded and fitted on their own: list of (first row, rows RGB, canvas shape or None)."""
    s = spec(name)
    if 'parts' in s:
        return [(a, rgb[a:b], c) for a, b, c in s['parts']]
    return [(0, rgb, s.get('canvas'))]


def canvas(name, w, h, shape):
    """The upload canvas in original pixels (CW, CH) and the frame's position (cx, cy) in it."""
    s = spec(name)
    if shape is None:
        return w, h, 0, 0
    ch = int(round(w / shape / s['aspect']))
    if ch >= h:
        return w, ch, 0, (ch - h) // 2
    cw = int(round(h * s['aspect'] * shape))
    return cw, h, (cw - w) // 2, 0


def repeat(rgb, n):
    return np.tile(rgb, (n, n, 1))


def gauss_kernel(sigma):
    r = max(1, int(np.ceil(3 * sigma)))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    return k / k.sum()


def blur(img, sigma):
    """Separable Gaussian blur of an (h, w, c) array, edges extended."""
    if sigma <= 0:
        return img
    k = gauss_kernel(sigma)
    r = len(k) // 2
    p = np.pad(img, ((r, r), (0, 0), (0, 0)), mode='edge')
    img = sum(k[i] * p[i:i + img.shape[0]] for i in range(len(k)))
    p = np.pad(img, ((0, 0), (r, r), (0, 0)), mode='edge')
    return sum(k[i] * p[:, i:i + img.shape[1]] for i in range(len(k)))


def box_down(img, f):
    h, w = img.shape[0] // f * f, img.shape[1] // f * f
    return img[:h, :w].reshape(h // f, f, w // f, f, -1).mean(axis=(1, 3))


def bilinear(img, x, y, fill):
    """Samples img (h, w, 3; pixel i covers [i, i+1)) at continuous coordinates x, y; outside gives fill."""
    h, w = img.shape[:2]
    xs, ys = x - 0.5, y - 0.5
    x0, y0 = np.floor(xs).astype(int), np.floor(ys).astype(int)
    fx, fy = (xs - x0)[..., None], (ys - y0)[..., None]
    pad = np.empty((h + 2, w + 2, 3))
    pad[:] = fill
    pad[1:-1, 1:-1] = img
    x0c, y0c = np.clip(x0 + 1, 0, w + 1), np.clip(y0 + 1, 0, h + 1)
    x1c, y1c = np.clip(x0 + 2, 0, w + 1), np.clip(y0 + 2, 0, h + 1)
    out = (pad[y0c, x0c] * (1 - fx) * (1 - fy) + pad[y0c, x1c] * fx * (1 - fy) +
           pad[y1c, x0c] * (1 - fx) * fy + pad[y1c, x1c] * fx * fy)
    outside = (x < 0) | (y < 0) | (x >= w) | (y >= h)
    out[outside] = fill
    return out


class Fit:
    def __init__(self, orig, bg, redraw):
        self.orig, self.bg = orig, bg
        self.H, self.W = orig.shape[:2]
        self.redraw = redraw
        self.cache = {}

    def prepare(self, sx):
        # Pre-reduce the redraw so that about two of its pixels fall on one original pixel.
        f = max(1, int(np.floor(1.0 / sx / 2.0)))
        if getattr(self, 'f', None) != f:
            self.f = f
            self.small = box_down(self.redraw, f)

    def sample(self, sx, sy, tx, ty, u, v):
        x = (u - tx) / sx / self.f
        y = (v - ty) / sy / self.f
        return bilinear(self.small, x[None, :].repeat(len(v), 0), y[:, None].repeat(len(u), 1), self.bg)

    def warp(self, p, ss=2):
        """The redraw in the original's grid for p = (sx, sy, tx, ty), area-averaged over ss x ss samples."""
        u = (np.arange(self.W * ss) + 0.5) / ss
        v = (np.arange(self.H * ss) + 0.5) / ss
        return box_down(self.sample(*p, u, v), ss)

    def orig_blurred(self, sigma):
        if sigma not in self.cache:
            self.cache[sigma] = blur(self.orig, sigma)
        return self.cache[sigma]

    def cost(self, p, sigma):
        d = blur(self.warp(p), sigma) - self.orig_blurred(sigma)
        return float(np.mean(d * d))

    def residual(self, p):
        warped = self.warp(p, ss=4)
        rms = np.sqrt(np.mean((warped - self.orig) ** 2))
        rms1 = np.sqrt(np.mean((blur(warped, 1.0) - blur(self.orig, 1.0)) ** 2))
        fg_o = np.abs(self.orig - self.bg).max(axis=2) > 24
        fg_r = np.abs(warped - self.bg).max(axis=2) > 24
        # Correlation of the detail band (Gaussian 2 minus Gaussian 12 pixels; 1 and 4 for small elements) of the
        # brightness: 1 when every shape is where the original has it, whatever the colours.
        s1, s2 = (2.0, 12.0) if min(self.W, self.H) >= 64 else (1.0, 4.0)
        lo, lw = self.orig.mean(axis=2, keepdims=True), warped.mean(axis=2, keepdims=True)
        do, dw = blur(lo, s1) - blur(lo, s2), blur(lw, s1) - blur(lw, s2)
        corr = float(np.corrcoef(do.ravel(), dw.ravel())[0, 1]) if do.std() > 0 and dw.std() > 0 else 0.0
        return rms, rms1, float(np.mean(fg_o != fg_r)) * 100, corr


def coarse_search(fit, p0, rng, uniform):
    """Brute force over scale (and aspect) around p0 (scaled about the frame's centre) and offset, blurred."""
    W, H = fit.W, fit.H
    q = 4 if min(W, H) >= 100 else 2  # coarse cell in original pixels
    ob = box_down(blur(fit.orig, 0.75 * q), q)
    best = (np.inf, None)
    scales = 1 + np.arange(-rng, rng + 1, 2.5 if rng > 4 else 1.0) / 100.0
    aspects = [1.0] if uniform else 1 + np.arange(-4, 4.1, 2) / 100.0
    m = int(np.ceil(rng / q))
    sx0, sy0, tx0, ty0 = p0
    xc, yc = (W / 2 - tx0) / sx0, (H / 2 - ty0) / sy0  # redraw point on the frame's centre
    fit.prepare(sx0)
    h2 = q // 2
    for s in scales:
        for a in aspects:
            sx, sy = sx0 * s, sy0 * s * a
            tx, ty = W / 2 - sx * xc, H / 2 - sy * yc
            # The redraw on a canvas larger than the frame by m cells on every side, sampled every q/2 pixels.
            u = (np.arange((W + 2 * m * q) // h2) + 0.5) * h2 - m * q
            v = (np.arange((H + 2 * m * q) // h2) + 0.5) * h2 - m * q
            img = box_down(blur(fit.sample(sx, sy, tx, ty, u, v), 0.75 * 2), 2)  # one cell = q pixels
            for dy in range(0, 2 * m + 1):
                for dx in range(0, 2 * m + 1):
                    win = img[dy:dy + H // q, dx:dx + W // q]
                    c = np.mean((win - ob[:win.shape[0], :win.shape[1]]) ** 2)
                    if c < best[0]:
                        # Shifting the window right by dx cells moves the redraw left.
                        best = (c, (sx, sy, tx - (dx - m) * q, ty - (dy - m) * q))
    return best[1]


def nelder_mead(f, x0, steps, iters=300, tol=1e-6):
    n = len(x0)
    pts = [np.array(x0, float)]
    for i in range(n):
        p = np.array(x0, float)
        p[i] += steps[i]
        pts.append(p)
    vals = [f(p) for p in pts]
    for _ in range(iters):
        order = np.argsort(vals)
        pts, vals = [pts[i] for i in order], [vals[i] for i in order]
        if abs(vals[-1] - vals[0]) <= tol * (abs(vals[0]) + 1e-12):
            break
        c = np.mean(pts[:-1], axis=0)
        xr = c + (c - pts[-1])
        fr = f(xr)
        if fr < vals[0]:
            xe = c + 2 * (c - pts[-1])
            fe = f(xe)
            pts[-1], vals[-1] = (xe, fe) if fe < fr else (xr, fr)
        elif fr < vals[-2]:
            pts[-1], vals[-1] = xr, fr
        else:
            xc = c + 0.5 * (pts[-1] - c)
            fc = f(xc)
            if fc < vals[-1]:
                pts[-1], vals[-1] = xc, fc
            else:
                for i in range(1, len(pts)):
                    pts[i] = pts[0] + 0.5 * (pts[i] - pts[0])
                    vals[i] = f(pts[i])
    i = int(np.argmin(vals))
    return pts[i]


def refine(fit, p, uniform, sigmas=(3.0, 1.5, 0.75)):
    sx, sy, tx, ty = p
    fit.prepare(sx)
    for sigma in sigmas:
        if uniform:
            def f(q):
                return fit.cost((sx * np.exp(q[0]), sy * np.exp(q[0]), tx + q[1], ty + q[2]), sigma)
            q = nelder_mead(f, [0, 0, 0], [0.01, 1.0, 1.0])
            sx, sy, tx, ty = sx * np.exp(q[0]), sy * np.exp(q[0]), tx + q[1], ty + q[2]
        else:
            def f(q):
                return fit.cost((sx * np.exp(q[0]), sy * np.exp(q[0] + q[1]), tx + q[2], ty + q[3]), sigma)
            q = nelder_mead(f, [0, 0, 0, 0], [0.01, 0.005, 1.0, 1.0])
            sx, sy = sx * np.exp(q[0]), sy * np.exp(q[0] + q[1])
            tx, ty = tx + q[2], ty + q[3]
    return sx, sy, tx, ty


def fit_picture(orig, bg, red, p0, rng, uniform):
    fit = Fit(orig, bg, red)
    p = refine(fit, coarse_search(fit, p0, rng, uniform), uniform)
    return p, fit.residual(p), fit.residual(p0)


def fit_element(orig, bg, red, p, rect):
    """Fits the redraw again on one rectangle (x0, y0, x1, y1) of orig, starting from the whole's fit p; returns the
    fit in orig's coordinates, its residual on the rectangle and the residual of p there."""
    x0, y0, x1, y1 = rect
    sub = orig[y0:y1, x0:x1]
    sx, sy, tx, ty = p
    pe0 = (sx, sy, tx - x0, ty - y0)
    fit = Fit(sub, bg, red)
    small = min(sub.shape[:2]) < 48
    pe = coarse_search(fit, pe0, 3, False) if not small else pe0
    pe = refine(fit, pe, small, sigmas=(1.5, 0.75) if not small else (1.0, 0.5))
    if fit.cost(pe, 0.75) > fit.cost(pe0, 0.75):
        pe = pe0
    return (pe[0], pe[1], pe[2] + x0, pe[3] + y0), fit.residual(pe), fit.residual(pe0)


def colour_curves(orig, red, p, bg):
    """Per-channel tone curves that give the redraw the original's colour distribution: quantile matching of the
    redraw in the original's grid (area-averaged) against the original, both blurred by 1 pixel so that the
    original's dithering and colour steps do not count. Monotone, so every shape and edge stays."""
    fit = Fit(orig, bg, red)
    fit.prepare(p[0])
    w, o = blur(fit.warp(p, ss=4), 1.0), blur(orig, 1.0)
    q = np.linspace(0, 100, 257)
    curves = []
    for c in range(3):
        xr, xo = np.percentile(w[..., c], q), np.percentile(o[..., c], q)
        xr = np.maximum.accumulate(xr) + np.arange(len(xr)) * 1e-6  # strictly increasing for interp
        curves.append((np.concatenate([[min(0.0, xr[0] - 1)], xr, [max(255.0, xr[-1] + 1)]]),
                       np.concatenate([[xo[0] - xr[0] + min(0.0, xr[0] - 1)], xo,
                                       [xo[-1] + max(255.0, xr[-1] + 1) - xr[-1]]])))

    def apply(a):
        out = np.empty_like(a, dtype=np.float64)
        for c, (xp, fp) in enumerate(curves):
            out[..., c] = np.interp(a[..., c], xp, np.clip(fp, 0, 255))
        return out
    return apply


def render(img, size, p, scale_uv, fill, origin=(0.0, 0.0)):
    """Resamples img (PIL) so that output pixel (X, Y) shows original coordinates (u0 + X su, v0 + Y sv) under fit p."""
    sx, sy, tx, ty = p
    su, sv = scale_uv
    u0, v0 = origin
    return img.transform(size, Image.AFFINE, (su / sx, 0, (u0 - tx) / sx, 0, sv / sy, (v0 - ty) / sy),
                         resample=Image.BICUBIC, fillcolor=fill)


def report(name, p, res, res0, p0, aspect):
    sx, sy, tx, ty = p
    rel, rely = sx / p0[0], sy / p0[1]
    print('  scale %.4f x %.4f of frame-filling (sx %.5f, sy %.5f original px per redraw px, pixel aspect %.4f), '
          'offset (%.2f, %.2f) original px (frame-filling: %.2f, %.2f)'
          % (rel, rely, sx, sy, sy * aspect / sx, tx, ty, p0[2], p0[3]))
    print('  residual: RMS %.1f, blurred 1 px %.1f, background mismatch %.2f %%, detail correlation %.3f   '
          '(redraw just stretched to the frame: %.1f, %.1f, %.2f %%, %.3f)' % (res + res0))
    print('  | %s | %.4f x %.4f | %+.1f, %+.1f | %.1f / %.1f / %.1f %% / %.2f |'
          % ((name, rel, rely, tx - p0[2], ty - p0[3]) + res))


# ---- seamless tiles

def min_cut(err):
    """Vertical path of least total error through err (h, w): the column per row, moving at most 1 per row."""
    h, w = err.shape
    cost = err.copy()
    back = np.zeros((h, w), np.int64)
    for y in range(1, h):
        prev = cost[y - 1]
        left = np.concatenate([[np.inf], prev[:-1]])
        right = np.concatenate([prev[1:], [np.inf]])
        stack = np.stack([left, prev, right])
        k = stack.argmin(axis=0)
        cost[y] += stack[k, np.arange(w)]
        back[y] = np.arange(w) + k - 1
    path = np.zeros(h, np.int64)
    path[-1] = int(cost[-1].argmin())
    for y in range(h - 1, 0, -1):
        path[y - 1] = back[y, path[y]]
    return path


def stitch_x(img, P, S, band, feather):
    """img (h, w >= S + P + band): returns (h, P), periodic in x, column j showing img's column S + j. Around the
    seam the copies one period apart (columns S - band .. S + band and S + P - band .. S + P + band) are joined
    along the path where they differ least."""
    A = img[:, S + P - band:S + P + band]      # the right end of the window and beyond
    B = img[:, S - band:S + band]              # the same texels one period earlier
    err = blur(((A - B) ** 2).sum(axis=2, keepdims=True), 2.0)[..., 0]
    err[:, :feather + 1] = err[:, -feather - 1:] = np.inf   # keep the feather inside the band
    path = min_cut(err)
    cols = np.arange(2 * band)[None, :]
    wB = np.clip((cols - path[:, None] + feather) / (2.0 * feather), 0, 1)[..., None]
    merged = A * (1 - wB) + B * wB             # A left of the path, B right of it
    return np.concatenate([merged[:, band:], img[:, S + band:S + P - band], merged[:, :band]], axis=1)


def make_periodic(rep, P, n):
    """rep: the fitted redraw of the n x n repeat, (n P, n P, 3). Returns one period (P, P, 3) aligned with the
    original tile (texel 0 at pixel 0) whose edges wrap."""
    S = (n * P - P) // 2                        # the window in the middle
    band = max(8, P // 12)
    feather = max(2, P // 160)
    t = stitch_x(rep, P, S, band, feather)                                          # (n P, P)
    t = stitch_x(t.transpose(1, 0, 2), P, S, band, feather).transpose(1, 0, 2)     # (P, P)
    # column j shows texel S + j (mod P): roll so that column 0 shows texel 0
    return np.roll(t, (S % P, S % P), axis=(0, 1))


def seam_error(t):
    """RMS difference across the wrap edges against the RMS between neighbouring columns / rows inside."""
    ex = np.sqrt(np.mean((t[:, 0] - t[:, -1]) ** 2))
    ey = np.sqrt(np.mean((t[0] - t[-1]) ** 2))
    ix = np.sqrt(np.mean((t[:, 1:] - t[:, :-1]) ** 2))
    iy = np.sqrt(np.mean((t[1:] - t[:-1]) ** 2))
    return ex, ix, ey, iy


# ---- align, assemble

def fit_path(name):
    return os.path.join(FITS, name.replace('#', '_') + '.png')


def align(a):
    name = a.name
    s = spec(name)
    rgb, bg = load_original(name)
    pieces = parts(name, rgb)
    if len(a.redraws) != len(pieces):
        sys.exit('%s needs %d redraws (one per part)' % (name, len(pieces)))
    aspect = s['aspect']
    n = s.get('tile', 1)
    rng = a.range or s.get('range', 16)
    fits = []
    for k, ((row0, orig, shape), path) in enumerate(zip(pieces, a.redraws)):
        if n > 1:
            orig = repeat(orig, n)
        img = Image.open(path).convert('RGB')
        red = np.asarray(img, np.float64)
        rw, rh = red.shape[1], red.shape[0]
        H, W = orig.shape[:2]
        CW, CH, cx, cy = canvas(name, W, H, shape)
        p0 = (CW / rw, CH / rh, -cx, -cy)
        p, res, res0 = fit_picture(orig, bg, red, p0, rng, a.uniform)
        label = name + ('.%d' % k if len(pieces) > 1 else '')
        print('%s: redraw %dx%d of a %dx%d canvas (frame at %d, %d)%s'
              % (label, rw, rh, CW, CH, cx, cy, ' (%dx%d repeat)' % (n, n) if n > 1 else ''))
        report(label, p, res, res0, p0, aspect)
        if a.match:
            red = np.clip(colour_curves(orig, red, p, bg)(red), 0, 255)
            fm = Fit(orig, bg, red)
            fm.prepare(p[0])
            print('  after the colour match: | %.1f / %.1f / %.1f %% / %.2f |' % fm.residual(p))
            img = Image.fromarray(np.round(red).astype(np.uint8))
        els = []
        for e in s.get('elements', []):
            x0, y0, x1, y1 = e
            if y0 < row0 or y1 > row0 + orig.shape[0]:
                continue
            r = (x0, y0 - row0, x1, y1 - row0)
            pe, rese, rese0 = fit_element(orig, bg, red, p, r)
            # how far the element's own fit moves the redraw at the element's centre, in original pixels
            uc, vc = (r[0] + r[2]) / 2.0, (r[1] + r[3]) / 2.0
            du = pe[0] * (uc - p[2]) / p[0] + pe[2] - uc
            dv = pe[1] * (vc - p[3]) / p[1] + pe[3] - vc
            print('  element %3d..%3d x %3d..%3d: moved %+.2f, %+.2f px, scaled %.4f x %.4f; '
                  'residual %.1f / %.1f / %.1f %% / %.2f (with the whole\'s fit %.1f / %.1f / %.1f %% / %.2f)'
                  % ((x0, x1, y0, y1, du, dv, pe[0] / p[0], pe[1] / p[1]) + rese + rese0))
            eimg = img
            if a.match_elements:
                # the element's own tone curve: its greys as in the original (303b's glyphs differ in grey)
                ered = np.clip(colour_curves(orig[r[1]:r[3], r[0]:r[2]], red, (pe[0], pe[1], pe[2] - r[0],
                                                                               pe[3] - r[1]), bg)(red), 0, 255)
                fe = Fit(orig[r[1]:r[3], r[0]:r[2]], bg, ered)
                fe.prepare(pe[0])
                print('    after its tone match: %.1f / %.1f / %.1f %% / %.2f'
                      % fe.residual((pe[0], pe[1], pe[2] - r[0], pe[3] - r[1])))
                eimg = Image.fromarray(np.round(ered).astype(np.uint8))
            els.append((r, pe, eimg))
        fits.append((img, orig, p, row0, els))
    # Output: the frame at the redraws' own horizontal pixel size (one scale for all parts).
    k_out = float(np.median([1.0 / f[2][0] for f in fits]))  # output pixels per original pixel, horizontally
    fill = tuple(int(round(c)) for c in bg)
    W = rgb.shape[1]
    ow = int(round(W * k_out))
    if n > 1:
        img, orig, p, _, _ = fits[0]
        P = ow
        oh = int(round(rgb.shape[0] * aspect * k_out))
        assert oh == P, 'tiles are square'
        rep = np.asarray(render(img, (n * P, n * P), p, (W / P, W / P), fill), np.float64)
        t = make_periodic(rep, P, n)
        ex, ix, ey, iy = seam_error(t)
        oex, oix, oey, oiy = seam_error(rgb)
        print('  tile %dx%d: RMS across the wrap edges %.1f (x), %.1f (y); between neighbours inside %.1f, %.1f '
              '(the original tile: %.1f, %.1f; %.1f, %.1f)' % (P, P, ex, ey, ix, iy, oex, oey, oix, oiy))
        out = Image.fromarray(np.clip(np.round(t), 0, 255).astype(np.uint8))
    else:
        oh = int(round(rgb.shape[0] * aspect * k_out))
        su, sv = W / ow, rgb.shape[0] / oh
        out = original_scaled(rgb, ow, oh, s.get('grey', False))
        for img, orig, p, row0, els in fits:
            Y0 = int(round(row0 / sv))
            Y1 = int(round((row0 + orig.shape[0]) / sv))
            part = render(img, (ow, Y1 - Y0), p, (su, sv), fill, (0.0, Y0 * sv - row0))
            for (x0, y0, x1, y1), pe, eimg in els:
                X0, X1 = int(round(x0 / su)), int(round(x1 / su))
                YA, YB = int(round((y0 + row0) / sv)) - Y0, int(round((y1 + row0) / sv)) - Y0
                piece = render(eimg, (X1 - X0, YB - YA), pe, (su, sv), fill, (X0 * su, (YA + Y0) * sv - row0))
                part.paste(piece, (X0, YA))
            out.paste(part, (0, Y0))
    if s.get('grey'):
        out = out.convert('L')
    os.makedirs(FITS, exist_ok=True)
    out.save(fit_path(name))
    print('  -> %s %dx%d' % (fit_path(name), out.size[0], out.size[1]))
    assemble(split(name)[0])


def original_scaled(rgb, ow, oh, grey, wrap=False):
    """The original scaled up bilinearly (pixel centres aligned), as the remake's texture filter shows it."""
    h, w = rgb.shape[:2]
    x = (np.arange(ow) + 0.5) * w / ow
    y = (np.arange(oh) + 0.5) * h / oh
    if wrap:
        big = np.tile(rgb, (3, 3, 1))
        out = bilinear(big, x[None, :].repeat(oh, 0) + w, y[:, None].repeat(ow, 1) + h, np.zeros(3))
    else:
        xs, ys = np.clip(x, 0.5, w - 0.5), np.clip(y, 0.5, h - 0.5)
        out = bilinear(rgb, xs[None, :].repeat(oh, 0), ys[:, None].repeat(ow, 1), np.zeros(3))
    im = Image.fromarray(np.clip(np.round(out), 0, 255).astype(np.uint8))
    return im.convert('L').convert('RGB') if grey else im


def assemble(file):
    """art/<file>: the kept fits of its tiles, the other tiles the original scaled up."""
    rgb, grey = load_file(file)
    n = STACKS.get(file, 1)
    names = ['%s#%d' % (file, k) for k in range(n)] if n > 1 else [file]
    have = {nm: Image.open(fit_path(nm)) for nm in names if os.path.exists(fit_path(nm))}
    if not have:
        print('%s: no fits' % file)
        return
    th = rgb.shape[0] // n
    tw = rgb.shape[1]
    ow = max(im.size[0] for im in have.values())
    tile_aspect = SPECS[next(iter(have))]['aspect']
    toh = int(round(th * tile_aspect * ow / tw))
    widest = [im for im in have.values() if im.size[0] == ow][0]
    if abs(widest.size[1] - toh) <= 1:
        toh = widest.size[1]  # keep the fit's own rounding (no resampling by a pixel)
    out = Image.new('RGB', (ow, toh * n))
    for k, nm in enumerate(names):
        if nm in have:
            im = have[nm].convert('RGB')
            if im.size != (ow, toh):
                im = im.resize((ow, toh), Image.LANCZOS)
        else:
            im = original_scaled(rgb[k * th:(k + 1) * th], ow, toh, grey, wrap=True)
        out.paste(im, (0, k * toh))
    if grey:
        out = out.convert('L')
    os.makedirs(ART, exist_ok=True)
    for ext in ('.png', '.jpg'):
        if os.path.exists(os.path.join(ART, file + ext)):
            os.remove(os.path.join(ART, file + ext))
    if file in JPEG:
        path = os.path.join(ART, file + '.jpg')
        out.save(path, quality=95, subsampling=0)
    else:
        path = os.path.join(ART, file + '.png')
        out.save(path, optimize=True)
    print('%s: %dx%d %s from %s (%.0f kB)' % (os.path.relpath(path), out.size[0], out.size[1], out.mode,
                                             ', '.join(sorted(have)), os.path.getsize(path) / 1000))


def upload(a):
    """The picture to give the image generator: nearest-neighbour (see SPECS 'up'), on its canvas."""
    name = a.name
    s = spec(name)
    rgb, bg = load_original(name)
    row0, orig, shape = parts(name, rgb)[a.part]
    n = s.get('tile', 1)
    if n > 1:
        orig = repeat(orig, n)
    H, W = orig.shape[:2]
    CW, CH, cx, cy = canvas(name, W, H, shape)
    c = np.empty((CH, CW, 3))
    c[:] = bg
    c[cy:cy + H, cx:cx + W] = orig
    fx, fy = s['up']
    img = Image.fromarray(np.round(c).astype(np.uint8))
    if s.get('grey'):
        img = img.convert('L')
    img.resize((CW * fx, CH * fy), Image.NEAREST).save(a.redraws[0])
    print('%s: %dx%d' % (a.redraws[0], CW * fx, CH * fy))


# ---- comparison sheet

def art_picture(name):
    """The redraw of name as written to art/ (the tile cut out of a stacked file), RGB float, or None."""
    file, k = split(name)
    for ext in ('.png', '.jpg'):
        path = os.path.join(ART, file + ext)
        if os.path.exists(path):
            im = np.asarray(Image.open(path).convert('RGB'), np.float64)
            if k is not None:
                th = im.shape[0] // STACKS[file]
                im = im[k * th:(k + 1) * th]
            return im
    return None


def sheet(a):
    try:
        font = ImageFont.truetype('DejaVuSans-Bold.ttf', 16)
    except OSError:
        font = ImageFont.load_default()
    rows = []
    for name in a.nums:
        rgb, _ = load_original(name)
        s = spec(name)
        H, W = rgb.shape[:2]
        pw = 800 if W * 1.0 / H > 1.5 or W > 256 else 600
        ph = int(round(pw * H * s['aspect'] / W))
        left = Image.fromarray(rgb.astype(np.uint8)).resize((pw, ph), Image.NEAREST)
        r = art_picture(name)
        pair = Image.new('RGB', (2 * pw, ph))
        pair.paste(left, (0, 0))
        if r is not None:
            right = Image.fromarray(np.clip(np.round(r), 0, 255).astype(np.uint8)).resize((pw, ph), Image.LANCZOS)
            label = '%s: original | redraw' % name
        else:
            right = left
            label = '%s: original | kept' % name
        pair.paste(right, (pw, 0))
        d = ImageDraw.Draw(pair)
        d.line([(pw, 0), (pw, ph)], fill=(80, 80, 80), width=2)
        d.text((6, 4), label, fill=(255, 255, 0), font=font, stroke_width=2, stroke_fill=(0, 0, 0))
        rows.append(pair)
    width = max(r.size[0] for r in rows)
    out = Image.new('RGB', (width, sum(r.size[1] + 4 for r in rows)), (40, 40, 40))
    y = 0
    for r in rows:
        out.paste(r, (0, y))
        y += r.size[1] + 4
    out.save(a.sheet, quality=88)
    print('%s: %d rows, %dx%d' % (a.sheet, len(rows), out.size[0], out.size[1]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('name', nargs='?')
    ap.add_argument('redraws', nargs='*')
    ap.add_argument('--uniform', action='store_true')
    ap.add_argument('--range', type=float, default=0)
    ap.add_argument('--part', type=int, default=0)
    ap.add_argument('--match', action='store_true')
    ap.add_argument('--match-elements', action='store_true')
    ap.add_argument('--sheet')
    ap.add_argument('--upload', action='store_true')
    ap.add_argument('--assemble', action='store_true')
    a = ap.parse_args()
    if a.sheet:
        a.nums = [x for x in [a.name] + a.redraws if x]
        sheet(a)
    elif a.assemble and a.name:
        assemble(a.name)
    elif a.upload and a.name and len(a.redraws) == 1:
        upload(a)
    elif a.name and a.redraws:
        align(a)
    else:
        ap.print_help()
        sys.exit(1)


if __name__ == '__main__':
    main()

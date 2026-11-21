"""Fits a high-resolution redraw of one of clone meets clone's ten pictures onto the original and writes art/NAME.png.

    python3 tools/align_art.py NAME REDRAW.png [--match] [--no-elements]   fit and write art/NAME.png
    python3 tools/align_art.py --upload NAME OUT.png                       the nearest-neighbour picture to upload
    python3 tools/align_art.py --sheet OUT.jpg NAME [NAME ...]             comparison sheet: original | redraw
(inside the toolbox: numpy + PIL). Adapted from dope/remake/tools/align_art.py and 303/remake/tools/align_art.py.

NAME is a picture of clone/re/dump/pictures/ (written by clone/re/models/gfx/pictures.py: RGBA, the 6-bit grey
value << 2, transparent pixels alpha 0), see clone/re/notes-gfx.md, section 7. All ten are grey and drawn 1:1 at
fixed positions on the 320x200 screen, which is shown at 4:3: one original pixel is 1.2 times as tall as wide. A
6-bit grey v is read as 255 * v / 63 (white 63 is 255, as the remake shows it).

- texts (meet_us, get_them_clones, connect_the_dots, tunnels_meet_more_tunnels, title_clone_meets_clone,
  credits_aap_echo_simon): white anti-aliased letters on black (the transparent pixels are drawn on a black box;
  the anti-aliasing towards black is opaque). Uploaded grey on a black canvas of a shape the generator makes (3:1;
  the credits square), written as 8-bit grey, white text on black;
- glyphs (glyph1..4): a white brush shape with a thick black outline on transparency (a hard 1-bit edge). Uploaded
  on a flat magenta key (255, 0, 255) on a square canvas; the key is turned into alpha afterwards (`unmix`: a
  pixel is a * grey + (1 - a) * key, so a follows from how far it is from grey towards the key; the redraw's own
  key colour is measured on its border, `normalise_key`) and the picture is written as 8-bit grey with alpha.

The fit maps redraw pixel coordinates (x, y) to original pixel coordinates (u, v): u = sx * x + tx,
v = sy * y + ty. It compares both pictures in the original's grid (the redraw area-averaged into it, both blurred
so that detail the original does not have does not count; glyphs on the key, so the silhouette counts): a
brute-force search over scale and offset on a coarse grid, then Nelder-Mead on (scale, aspect, tx, ty) with less
and less blur. It prints the scale (relative to "the redraw fills the upload's canvas") and offset and the residual
(RMS of the difference 0..255 in the original's grid, plain and with a 1-pixel blur, the share of pixels where one
picture is background (black, or the key) and the other is not, and the correlation of the two pictures' detail,
see `Fit.residual`).

Texts are then fitted again per element (`elements`: the words, split at gaps of 3 or more empty columns; the
credits' three lines, split at empty rows), each starting from the whole's fit, and each element is written
exactly into its rectangle, so every word stays at its pixels even where the generator spaced the words a little
differently. The rectangles meet in the middle of the gaps, where both pictures are black.

The output covers exactly the original's frame at the redraw's own pixel size (width = W / sx, height from the
pixel aspect); where the redraw does not reach it is black (texts) or transparent (glyphs).

Options:
  --range R         search offsets up to R original pixels and scales up to 1 +- R/100 (default 12)
  --uniform         one scale for x and y
  --match           after the fit, give the redraw the original's grey distribution: a monotone tone curve from
                    quantile matching in the original's grid (`tone_curve`; glyphs: inside both silhouettes)
  --no-elements     texts: keep the whole's fit for every word
  --out FILE        output (default ../art/NAME.png next to this script)
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PICTURES = os.path.join(HERE, '..', '..', 're', 'dump', 'pictures')
ART = os.path.join(HERE, '..', 'art')
SCREEN_ASPECT = 1.2  # an original screen pixel is 1.2 times as tall as wide on a 4:3 screen
KEY = np.array([255.0, 0.0, 255.0])  # the glyphs' transparency on the upload
BLACK = np.zeros(3)

# kind: 'text' (grey on black) or 'glyph' (grey with alpha). canvas: the upload's display shape (width / height).
# margin: the canvas's margin around the frame, as a share of the frame's width. split: how the text is cut into
# elements ('cols': at runs of at least n empty columns, 'rows': at runs of empty rows).
TEXT = dict(kind='text', canvas=3.0, margin=0.15, split=('cols', 3))
GLYPH = dict(kind='glyph', canvas=1.0, margin=0.12)
SPECS = {
    'meet_us': TEXT,
    'get_them_clones': TEXT,
    'connect_the_dots': TEXT,
    'tunnels_meet_more_tunnels': TEXT,
    'title_clone_meets_clone': TEXT,
    'credits_aap_echo_simon': dict(TEXT, canvas=1.0, split=('rows', 1)),
    'glyph1': GLYPH,
    'glyph2': GLYPH,
    'glyph3': GLYPH,
    'glyph4': GLYPH,
}


def spec(name):
    if name not in SPECS:
        sys.exit('unknown picture %s (one of %s)' % (name, ', '.join(SPECS)))
    return SPECS[name]


def load_original(name):
    """The picture as grey (h, w) 0..255 (255 * v / 63) and alpha (h, w) 0 or 1."""
    a = np.asarray(Image.open(os.path.join(PICTURES, name + '.png')).convert('RGBA'), np.float64)
    grey = a[..., 0] / 4.0 * 255.0 / 63.0
    alpha = (a[..., 3] > 127).astype(np.float64)
    return grey * alpha, alpha


def background(name):
    return KEY if spec(name)['kind'] == 'glyph' else BLACK


def composite(name, grey, alpha):
    """RGB (h, w, 3) as uploaded: texts grey on black, glyphs grey over the key."""
    g = np.repeat(grey[..., None], 3, axis=2)
    a = alpha[..., None]
    return g * a + background(name) * (1 - a)


def unmix(rgb, key=KEY):
    """Glyph redraw (h, w, 3) on the key -> grey (h, w) and alpha (h, w). A pixel is a * g + (1 - a) * key: its
    distance from grey along (R + B) / 2 - G is (1 - a) times the key's, a grey pixel's is 0."""
    dk = (key[0] + key[2]) / 2.0 - key[1]
    d = (rgb[..., 0] + rgb[..., 2]) / 2.0 - rgb[..., 1]
    a = np.clip(1.0 - d / dk, 0.0, 1.0)
    a = np.clip((a - 0.02) / 0.96, 0.0, 1.0)  # the generator's noise on the flat key and on the grey
    g = np.where(a > 1e-3, (rgb[..., 1] - (1 - a) * key[1]) / np.maximum(a, 1e-3), 0.0)
    return np.clip(g, 0, 255), a


def normalise_key(red):
    """The generator's key is not exactly KEY (ChatGPT: about 249, 4, 249): measure it on the redraw's border (the
    canvas margin) and put the picture on the exact KEY."""
    h, w = red.shape[:2]
    b = max(2, min(h, w) // 50)
    border = np.concatenate([red[:b].reshape(-1, 3), red[-b:].reshape(-1, 3), red[:, :b].reshape(-1, 3),
                             red[:, -b:].reshape(-1, 3)])
    key = np.median(border, axis=0)
    g, a = unmix(red, key)
    return np.repeat(g[..., None], 3, axis=2) * a[..., None] + KEY * (1 - a[..., None]), key


def elements(name, grey):
    """The rectangles (x0, y0, x1, y1) fitted on their own: the words (or lines), meeting in the middle of the gaps."""
    s = spec(name)
    if 'split' not in s:
        return []
    axis, n = s['split']
    used = (grey > 0).any(axis=0 if axis == 'cols' else 1)
    cuts, run = [], 0
    for i, u in enumerate(used):
        if not u:
            run += 1
            continue
        if run >= n and i - run > 0:
            cuts.append(i - run + run // 2)  # the middle of the gap
        run = 0
    edges = [0] + cuts + [len(used)]
    h, w = grey.shape
    if axis == 'cols':
        return [(a, 0, b, h) for a, b in zip(edges[:-1], edges[1:])]
    return [(0, a, w, b) for a, b in zip(edges[:-1], edges[1:])]


def canvas(name, w, h):
    """The upload canvas in original pixels (CW, CH) and the frame's position (cx, cy) in it."""
    s = spec(name)
    m = max(4, int(round(s['margin'] * w)))
    cw, ch = w + 2 * m, h + 2 * m
    if cw / (ch * SCREEN_ASPECT) > s['canvas']:
        ch = int(round(cw / s['canvas'] / SCREEN_ASPECT))
    else:
        cw = int(round(ch * SCREEN_ASPECT * s['canvas']))
    return cw, ch, (cw - w) // 2, (ch - h) // 2


def upload_scale(cw):
    """Nearest-neighbour factors (5k, 6k): screen pixels at 4:3, about 2000 pixels wide."""
    k = max(1, int(round(2000.0 / (5 * cw))))
    return 5 * k, 6 * k


# ---- fitting (as DOPE's and 303's)

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
        # Correlation of the detail band (Gaussian 1 minus Gaussian 4 pixels: the pictures are small) of the
        # brightness: 1 when every shape is where the original has it.
        lo, lw = self.orig.mean(axis=2, keepdims=True), warped.mean(axis=2, keepdims=True)
        do, dw = blur(lo, 1.0) - blur(lo, 4.0), blur(lw, 1.0) - blur(lw, 4.0)
        corr = float(np.corrcoef(do.ravel(), dw.ravel())[0, 1]) if do.std() > 0 and dw.std() > 0 else 0.0
        return rms, rms1, float(np.mean(fg_o != fg_r)) * 100, corr


def coarse_search(fit, p0, rng, uniform):
    """Brute force over scale (and aspect) around p0 (scaled about the frame's centre) and offset, blurred."""
    W, H = fit.W, fit.H
    q = 2  # coarse cell in original pixels
    ob = box_down(blur(fit.orig, 0.75 * q), q)
    best = (np.inf, None)
    scales = 1 + np.arange(-rng, rng + 1, 2.0) / 100.0
    aspects = [1.0] if uniform else 1 + np.arange(-6, 6.1, 2) / 100.0
    m = int(np.ceil(rng / q))
    sx0, sy0, tx0, ty0 = p0
    xc, yc = (W / 2 - tx0) / sx0, (H / 2 - ty0) / sy0  # redraw point on the frame's centre
    fit.prepare(sx0)
    for s in scales:
        for a in aspects:
            sx, sy = sx0 * s, sy0 * s * a
            tx, ty = W / 2 - sx * xc, H / 2 - sy * yc
            # The redraw on a canvas larger than the frame by m cells on every side, one sample per pixel.
            u = np.arange(W + 2 * m * q) + 0.5 - m * q
            v = np.arange(H + 2 * m * q) + 0.5 - m * q
            img = box_down(blur(fit.sample(sx, sy, tx, ty, u, v), 0.75 * q), q)  # one cell = q pixels
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


def refine(fit, p, uniform, sigmas=(2.0, 1.0, 0.6)):
    sx, sy, tx, ty = p
    fit.prepare(sx)
    for sigma in sigmas:
        if uniform:
            def f(q):
                return fit.cost((sx * np.exp(q[0]), sy * np.exp(q[0]), tx + q[1], ty + q[2]), sigma)
            q = nelder_mead(f, [0, 0, 0], [0.01, 0.5, 0.5])
            sx, sy, tx, ty = sx * np.exp(q[0]), sy * np.exp(q[0]), tx + q[1], ty + q[2]
        else:
            def f(q):
                return fit.cost((sx * np.exp(q[0]), sy * np.exp(q[0] + q[1]), tx + q[2], ty + q[3]), sigma)
            q = nelder_mead(f, [0, 0, 0, 0], [0.01, 0.005, 0.5, 0.5])
            sx, sy = sx * np.exp(q[0]), sy * np.exp(q[0] + q[1])
            tx, ty = tx + q[2], ty + q[3]
    return sx, sy, tx, ty


def fit_picture(orig, bg, red, p0, rng, uniform):
    fit = Fit(orig, bg, red)
    p = refine(fit, coarse_search(fit, p0, rng, uniform), uniform)
    return p, fit.residual(p), fit.residual(p0)


def fit_element(orig, bg, red, p, rect):
    """Fits the redraw again on one rectangle (x0, y0, x1, y1) of orig, starting from the whole's fit p: the
    horizontal scale and both offsets, small blurs; the vertical scale stays the whole's, so that every word keeps
    the same letter height. Returns the fit in orig's coordinates, its residual on the rectangle and the residual of
    p there."""
    x0, y0, x1, y1 = rect
    sub = orig[y0:y1, x0:x1]
    sx, sy, tx, ty = p
    pe0 = (sx, sy, tx - x0, ty - y0)
    fit = Fit(sub, bg, red)
    fit.prepare(sx)
    pe = pe0
    for sigma in (1.0, 0.5):
        def f(q, pe=pe):
            return fit.cost((pe[0] * np.exp(q[0]), sy, pe[2] + q[1], pe[3] + q[2]), sigma)
        q = nelder_mead(f, [0, 0, 0], [0.01, 0.3, 0.3])
        pe = (pe[0] * np.exp(q[0]), sy, pe[2] + q[1], pe[3] + q[2])
    if fit.cost(pe, 0.5) > fit.cost(pe0, 0.5):
        pe = pe0
    return (pe[0], pe[1], pe[2] + x0, pe[3] + y0), fit.residual(pe), fit.residual(pe0)


def tone_curve(name, orig_grey, orig_alpha, red, p):
    """A monotone tone curve that gives the redraw's grey the original's distribution: quantile matching of the
    redraw in the original's grid (area-averaged) against the original, both blurred by half a pixel. Glyphs: only
    where both are opaque. Returns a function of grey 0..255."""
    bg = background(name)
    fit = Fit(composite(name, orig_grey, orig_alpha), bg, red)
    fit.prepare(p[0])
    w = fit.warp(p, ss=4)
    if spec(name)['kind'] == 'glyph':
        wg, wa = unmix(w)
        mask = (wa > 0.99) & (orig_alpha > 0.5)
        o = orig_grey
    else:
        wg, mask, o = w.mean(axis=2), np.ones(orig_grey.shape, bool), orig_grey
    wg = blur(wg[..., None], 0.5)[..., 0][mask]
    o = blur(o[..., None], 0.5)[..., 0][mask]
    q = np.linspace(0, 100, 257)
    xr, xo = np.percentile(wg, q), np.percentile(o, q)
    xr = np.maximum.accumulate(xr) + np.arange(len(xr)) * 1e-6  # strictly increasing for interp
    xp = np.concatenate([[min(0.0, xr[0] - 1)], xr, [max(255.0, xr[-1] + 1)]])
    fp = np.concatenate([[xo[0] - xr[0] + xp[0]], xo, [xo[-1] + xp[-1] - xr[-1]]])
    return lambda g: np.interp(g, xp, np.clip(fp, 0, 255))


def render(img, size, p, scale_uv, fill, origin=(0.0, 0.0)):
    """Resamples img (PIL) so that output pixel (X, Y) shows original coordinates (u0 + X * su, v0 + Y * sv)."""
    sx, sy, tx, ty = p
    su, sv = scale_uv
    u0, v0 = origin
    return img.transform(size, Image.AFFINE, (su / sx, 0, (u0 - tx) / sx, 0, sv / sy, (v0 - ty) / sy),
                         resample=Image.BICUBIC, fillcolor=fill)


def report(p, res, res0, p0):
    sx, sy, tx, ty = p
    rel, rely = sx / p0[0], sy / p0[1]
    print('  scale %.4f x %.4f of canvas-filling (sx %.5f, sy %.5f original px per redraw px, pixel aspect %.4f), '
          'offset (%.2f, %.2f) original px (canvas-filling: %.2f, %.2f)'
          % (rel, rely, sx, sy, sy * SCREEN_ASPECT / sx, tx, ty, p0[2], p0[3]))
    print('  residual: RMS %.1f, blurred 1 px %.1f, background mismatch %.2f %%, detail correlation %.3f   '
          '(redraw just stretched to the canvas: %.1f, %.1f, %.2f %%, %.3f)' % (res + res0))


def align(a):
    name = a.name
    s = spec(name)
    grey, alpha = load_original(name)
    orig = composite(name, grey, alpha)
    bg = background(name)
    fill = tuple(int(c) for c in bg)
    H, W = grey.shape
    img = Image.open(a.redraw).convert('RGB')
    red = np.asarray(img, np.float64)
    if s['kind'] == 'text':
        red = np.repeat(red.mean(axis=2, keepdims=True), 3, axis=2)  # grey only
    else:
        red, key = normalise_key(red)
        print('%s: the redraw\'s key colour %.1f, %.1f, %.1f' % ((name,) + tuple(key)))
    rw, rh = red.shape[1], red.shape[0]
    CW, CH, cx, cy = canvas(name, W, H)
    p0 = (CW / rw, CH / rh, -cx, -cy)
    p, res, res0 = fit_picture(orig, bg, red, p0, a.range, a.uniform)
    print('%s: redraw %dx%d of a %dx%d canvas (frame %dx%d at %d, %d)' % (name, rw, rh, CW, CH, W, H, cx, cy))
    report(p, res, res0, p0)
    if a.match:
        curve = tone_curve(name, grey, alpha, red, p)
        if s['kind'] == 'glyph':
            g, al = unmix(red)
            g = curve(g)
            red = np.repeat(g[..., None], 3, axis=2) * al[..., None] + KEY * (1 - al[..., None])
        else:
            red = np.repeat(curve(red[..., :1]), 3, axis=2)
        fm = Fit(orig, bg, red)
        fm.prepare(p[0])
        print('  after the tone match: residual %.1f / %.1f / %.2f %% / %.3f' % fm.residual(p))
    img = Image.fromarray(np.clip(np.round(red), 0, 255).astype(np.uint8))
    els = []
    if not a.no_elements:
        for r in elements(name, grey):
            pe, rese, rese0 = fit_element(orig, bg, red, p, r)
            uc, vc = (r[0] + r[2]) / 2.0, (r[1] + r[3]) / 2.0
            du = pe[0] * (uc - p[2]) / p[0] + pe[2] - uc
            dv = pe[1] * (vc - p[3]) / p[1] + pe[3] - vc
            print('  element %3d..%3d x %2d..%2d: moved %+.2f, %+.2f px, scaled %.4f; residual %.1f / %.1f / '
                  '%.1f %% / %.2f (with the whole\'s fit %.1f / %.1f / %.1f %% / %.2f)'
                  % ((r[0], r[2], r[1], r[3], du, dv, pe[0] / p[0]) + rese + rese0))
            els.append((r, pe))
    # Output: the frame at the redraw's own horizontal pixel size.
    k_out = 1.0 / p[0]
    ow, oh = int(round(W * k_out)), int(round(H * SCREEN_ASPECT * k_out))
    su, sv = W / ow, H / oh
    out = render(img, (ow, oh), p, (su, sv), fill)
    for (x0, y0, x1, y1), pe in els:
        X0, X1 = int(round(x0 / su)), int(round(x1 / su))
        Y0, Y1 = int(round(y0 / sv)), int(round(y1 / sv))
        out.paste(render(img, (X1 - X0, Y1 - Y0), pe, (su, sv), fill, (X0 * su, Y0 * sv)), (X0, Y0))
    if els:
        whole = Fit(orig, bg, np.asarray(out, np.float64))
        ps = (su, sv, 0.0, 0.0)
        whole.prepare(ps[0])
        print('  written (with the elements): residual %.1f / %.1f / %.2f %% / %.3f' % whole.residual(ps))
    if s['kind'] == 'glyph':
        g, al = unmix(np.asarray(out, np.float64))
        la = np.stack([g, al * 255.0], axis=2)
        out = Image.fromarray(np.clip(np.round(la), 0, 255).astype(np.uint8), 'LA')
    else:
        out = out.convert('L')
    out_path = a.out or os.path.join(ART, name + '.png')
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    out.save(out_path, optimize=True)
    print('  -> %s %dx%d %s (%.0f kB)' % (os.path.relpath(out_path), out.size[0], out.size[1], out.mode,
                                         os.path.getsize(out_path) / 1000))


def upload(a):
    """The picture to give the image generator: nearest-neighbour at 4:3 on its canvas."""
    name = a.name
    grey, alpha = load_original(name)
    H, W = grey.shape
    CW, CH, cx, cy = canvas(name, W, H)
    c = np.empty((CH, CW, 3))
    c[:] = background(name)
    c[cy:cy + H, cx:cx + W] = composite(name, grey, alpha)
    fx, fy = upload_scale(CW)
    img = Image.fromarray(np.round(c).astype(np.uint8))
    if spec(name)['kind'] == 'text':
        img = img.convert('L')
    img.resize((CW * fx, CH * fy), Image.NEAREST).save(a.redraw)
    print('%s: %dx%d (canvas %dx%d original px, frame at %d, %d; %dx%d per pixel)'
          % (a.redraw, CW * fx, CH * fy, CW, CH, cx, cy, fx, fy))


# ---- comparison sheet

SHEET_BG = (70, 70, 110)  # what the glyphs are shown on (the outline is black)


def shown(name, grey, alpha):
    """RGB uint8 as the sheet shows it: texts on black, glyphs over SHEET_BG."""
    g = np.repeat(grey[..., None], 3, axis=2)
    a = alpha[..., None]
    if spec(name)['kind'] == 'text':
        a = np.ones_like(a)
    return Image.fromarray(np.clip(np.round(g * a + np.array(SHEET_BG) * (1 - a)), 0, 255).astype(np.uint8))


def sheet(a):
    try:
        font = ImageFont.truetype('DejaVuSans-Bold.ttf', 16)
    except OSError:
        font = ImageFont.load_default()
    WIDTH = 1600
    rows, pending = [], []

    def flush():
        if pending:
            row = Image.new('RGB', (WIDTH, max(p.size[1] for p in pending)), (40, 40, 40))
            x = 0
            for p in pending:
                row.paste(p, (x, 0))
                x += p.size[0]
            rows.append(row)
            pending.clear()

    for name in a.names:
        grey, alpha = load_original(name)
        H, W = grey.shape
        art = os.path.join(a.art, name + '.png')
        if os.path.exists(art):
            r = np.asarray(Image.open(art).convert('LA'), np.float64)
            right_src = shown(name, r[..., 0], r[..., 1] / 255.0)
            label = '%s: original | redraw' % name
        else:
            right_src = None
            label = '%s: original | none' % name
        if W / (H * SCREEN_ASPECT) > 2.0:
            # wide text: original above, redraw below, full width
            pw = WIDTH
            ph = int(round(pw * H * SCREEN_ASPECT / W))
            pair = Image.new('RGB', (pw, 2 * ph + 4), (40, 40, 40))
            pair.paste(shown(name, grey, alpha).resize((pw, ph), Image.NEAREST), (0, 0))
            if right_src is not None:
                pair.paste(right_src.resize((pw, ph), Image.LANCZOS), (0, ph + 4))
            flush()
            rows.append(pair)
        else:
            pw = WIDTH // 4
            ph = int(round(pw * H * SCREEN_ASPECT / W))
            pair = Image.new('RGB', (2 * pw, ph), (40, 40, 40))
            pair.paste(shown(name, grey, alpha).resize((pw, ph), Image.NEAREST), (0, 0))
            if right_src is not None:
                pair.paste(right_src.resize((pw, ph), Image.LANCZOS), (pw, 0))
            d = ImageDraw.Draw(pair)
            d.line([(pw, 0), (pw, ph)], fill=(200, 200, 200), width=2)
            pending.append(pair)
            if len(pending) == 2:
                flush()
        d = ImageDraw.Draw(pair)
        d.text((6, 4), label, fill=(255, 255, 0), font=font, stroke_width=2, stroke_fill=(0, 0, 0))
    flush()
    out = Image.new('RGB', (WIDTH, sum(r.size[1] + 6 for r in rows)), (40, 40, 40))
    y = 0
    for r in rows:
        out.paste(r, (0, y))
        y += r.size[1] + 6
    out.save(a.sheet, quality=90)
    print('%s: %d rows, %dx%d' % (a.sheet, len(rows), out.size[0], out.size[1]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('name', nargs='?')
    ap.add_argument('rest', nargs='*')
    ap.add_argument('--out')
    ap.add_argument('--art', default=ART)
    ap.add_argument('--uniform', action='store_true')
    ap.add_argument('--range', type=float, default=12)
    ap.add_argument('--match', action='store_true')
    ap.add_argument('--no-elements', action='store_true')
    ap.add_argument('--sheet')
    ap.add_argument('--upload', action='store_true')
    a = ap.parse_args()
    if a.sheet:
        a.names = [x for x in [a.name] + a.rest if x] or list(SPECS)
        sheet(a)
    elif a.name and len(a.rest) == 1:
        a.redraw = a.rest[0]
        spec(a.name)
        upload(a) if a.upload else align(a)
    else:
        ap.print_help()
        sys.exit(1)


if __name__ == '__main__':
    main()

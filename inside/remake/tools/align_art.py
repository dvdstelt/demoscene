"""Fits a high-resolution redraw of one of Inside's pictures onto the original and writes art/NAME.png.

    python3 tools/align_art.py --upload NAME OUT.png         the nearest-neighbour picture to give the image generator
    python3 tools/align_art.py NAME REDRAW.png [--match]      fit the redraw and write art/NAME.png
    python3 tools/align_art.py --sheet OUT.jpg NAME [NAME ..] comparison sheet: original | redraw
(inside the toolbox: numpy + PIL). Adapted from clone/remake/tools/align_art.py (the fit) and
dope/remake/tools/align_art.py (the colour match).

NAME is a picture of PICTURES below (the remake's src/pictures.cpp has the same list): 8-bit pixels and a 6-bit
palette from INSIDE.DAT (read with inside/re/datfile.py; the original files must be in inside/original), shown on the
320x200 screen at 4:3, so one original pixel is 1.2 times as tall as wide. A 6-bit value v is read as 255 * v / 63.

- opaque pictures: uploaded as they are, written as RGB;
- pictures with a transparent index: uploaded on a flat key colour that the picture does not use (KEY below), and
  the key is turned into alpha afterwards (a pixel is a * c + (1 - a) * key; a follows from how far it is from the key
  along the key's direction), written as RGBA.

The fit maps redraw pixel coordinates (x, y) to original pixel coordinates (u, v): u = sx * x + tx, v = sy * y + ty,
comparing both in the original's grid (the redraw area-averaged into it, both blurred so that detail the original
does not have does not count): a brute-force search over scale and offset, then Nelder-Mead with less and less blur.
It prints the residual (RMS of the colour difference 0..255 in the original's grid, plain and with a 1-pixel blur,
the share of pixels where one picture is background and the other is not, and the correlation of the detail).
--match gives the redraw the original's colour distribution per channel (quantile matching in the original's grid),
for redraws that came out darker or with shifted colours.

Options:
  --range R         search offsets up to R original pixels and scales up to 1 +- R/100 (default 12)
  --uniform         one scale for x and y
  --match           match the colours to the original's
  --out FILE        output (default ../art/NAME.png next to this script)
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 're'))
import datfile  # noqa: E402

ORIGINAL = os.path.join(HERE, '..', '..', 'original', 'INSIDE.DAT')
ART = os.path.join(HERE, '..', 'art')
SCREEN_ASPECT = 1.2
KEY = np.array([0.0, 0.0, 255.0])   # blue: none of the keyed pictures uses it

# name: INSIDE.DAT file, palette, width, height, transparent index (None: opaque), the upload's margin around the
# picture (share of its width; keyed pictures only), what it shows (for the prompt)
PICTURES = {
    'logo_inside': dict(file='logo/inside.raw', pal='logo/logo.pal', w=249, h=139, key=0, margin=0.10,
                        what='the title INSIDE in twisted 3D letters with a painted texture'),
    'water': dict(file='water/water.raw', pal='water/water.pal', w=320, h=200, key=None, margin=0.0,
                  what='a blurred blue view of an interior seen through water'),
    # the sprites parts' photos (notes-sprites.md 1), cut out on index 0; key_t: the photo's own colours reach up to
    # this far along the key's direction (B - (R + G) / 2 up to 79 in sprite 1), so the redraw counts as opaque up to it
    'sprite1': dict(file='nigga/sprite1.raw', pal='nigga/nigga1.pal', w=164, h=156, key=0, margin=0.08, key_t=100,
                    what='two men, one in a patterned hat looking up, one in a green jacket'),
    'sprite2': dict(file='nigga/sprite2.raw', pal='nigga/nigga2.pal', w=172, h=200, key=0, margin=0.08, key_t=100,
                    what='a man in white reaching his open hand towards the camera'),
    'sprite4': dict(file='nigga/sprite4.raw', pal='nigga/nigga4.pal', w=170, h=168, key=0, margin=0.08, key_t=100,
                    what='a man with an afro and white sunglasses in a white T-shirt'),
    'sprite9': dict(file='nigga/sprite9.raw', pal='nigga/nigga9.pal', w=274, h=147, key=0, margin=0.08, key_t=100,
                    what='a man lying on his side in an orange shirt and sunglasses'),
    # the intro's photo collage (notes-intro-logo.md 2), shown through the shade table intro/intro.shd
    'intro': dict(file='intro/intro.raw', pal='intro/intro.pal', w=320, h=200, key=None, margin=0.0,
                  what="a woman's body and a tube of Amazing GOOP glue on a swirled background"),
}
# The credits' name pictures (notes-credits.md 4.2): level maps 0..31 (shown here as grey 255 * L / 31 on black) of a
# flat star (level 10), a scanned face (a photo of a real person: never uploaded, never redrawn) and the name. The
# upload and the redraw have the face filled with the star's level (face_mask); the remake puts the original's face
# back on top of the redraw.
for _n, _f, _w, _h, _t in (('credits_destop', 'kredu1', 173, 75, 'DESTOP'), ('credits_wili', 'kredu7', 129, 75, 'WILI'),
                          ('credits_jouni', 'kredu2', 146, 75, 'JOUNI'), ('credits_juliet', 'kredu8', 154, 70, 'JULIET'),
                          ('credits_yolk_legend', 'kredu3', 318, 82, 'YOLK & LEGEND')):
    PICTURES[_n] = dict(file='credits/%s.raw' % _f, pal=None, w=_w, h=_h, key=None, margin=0.08, levels=True,
                        what='a flat grey star (or two) and the name %s in heavy oblique white capitals' % _t)

_dat = None


def dat():
    global _dat
    if _dat is None:
        _dat = datfile.Dat(ORIGINAL)
    return _dat


def spec(name):
    if name not in PICTURES:
        sys.exit('unknown picture %s (one of %s)' % (name, ', '.join(PICTURES)))
    return PICTURES[name]


# The faces in the name pictures (original pixel coordinates; measured on the level maps): ellipses (cx, cy, rx, ry)
# and boxes (x0, y0, x1, y1) that cover each scanned face with its hair and nothing of the name. Inside them every
# pixel that is not the star's level 10 is the face. The remake uses the same shapes (src/scenes/credits.cpp).
FACES = {
    'credits_destop': dict(ellipses=[(50, 35, 36, 30)], boxes=[(64, 2, 93, 38)]),
    'credits_wili': dict(ellipses=[(77, 37, 25, 28)], boxes=[]),
    'credits_jouni': dict(ellipses=[(50, 38, 23, 27)], boxes=[]),
    'credits_juliet': dict(ellipses=[(51, 36, 27, 25)], boxes=[]),
    'credits_yolk_legend': dict(ellipses=[(47, 40, 36, 29), (266, 38, 28, 30)], boxes=[]),
}


def face_mask(name, level):
    h, w = level.shape
    yy, xx = np.mgrid[0:h, 0:w] + 0.5
    inside = np.zeros((h, w), bool)
    for cx, cy, rx, ry in FACES[name]['ellipses']:
        inside |= ((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2 <= 1.0
    for x0, y0, x1, y1 in FACES[name]['boxes']:
        inside |= (xx >= x0) & (xx < x1) & (yy >= y0) & (yy < y1)
    return inside & (level != 10)


def inside_star(level):
    """Pixels that have star pixels (level 10) to their left, right, top and bottom: the inside of the star(s)."""
    st = level == 10
    left = np.maximum.accumulate(st, axis=1)
    right = np.maximum.accumulate(st[:, ::-1], axis=1)[:, ::-1]
    up = np.maximum.accumulate(st, axis=0)
    down = np.maximum.accumulate(st[::-1], axis=0)[::-1]
    return left & right & up & down


def remove_faces(name, level):
    """The level map with every face pixel replaced by the star (where the star surrounds it) or black."""
    mask = face_mask(name, level)
    out = level.copy()
    out[mask] = np.where(inside_star(level)[mask], 10, 0)
    # thin black seams where the hair reached the star's edge: star within 3 pixels on both sides closes them
    st = out == 10
    h, w = st.shape
    for _ in range(2):
        near_l = np.zeros_like(st)
        near_r = np.zeros_like(st)
        for d in range(1, 4):
            near_l[:, d:] |= st[:, :w - d]
            near_r[:, :w - d] |= st[:, d:]
        fix = mask & ~st & near_l & near_r
        out[fix] = 10
        st = out == 10
    return out


def load_original(name):
    """RGB (h, w, 3) 0..255 and alpha (h, w) 0 or 1."""
    s = spec(name)
    if s.get('levels'):
        lv = np.frombuffer(dat().get(s['file']), np.uint8)[:s['w'] * s['h']].reshape(s['h'], s['w']).copy()
        lv = remove_faces(name, lv.astype(np.int32))
        g = lv.astype(np.float64) * 255.0 / 31.0
        return np.repeat(g[..., None], 3, axis=2), np.ones(lv.shape)
    pal = np.frombuffer(dat().get(s['pal']), np.uint8).reshape(256, 3).astype(np.float64)
    rgb = np.minimum(pal, 63) * 255.0 / 63.0
    px = np.frombuffer(dat().get(s['file']), np.uint8)[:s['w'] * s['h']].reshape(s['h'], s['w'])
    alpha = np.ones(px.shape) if s['key'] is None else (px != s['key']).astype(np.float64)
    return rgb[px] * alpha[..., None], alpha


def background(name):
    return KEY if spec(name)['key'] is not None else np.zeros(3)


def composite(name, rgb, alpha):
    return rgb * alpha[..., None] + background(name) * (1 - alpha[..., None])


def unmix(img, key=KEY, t=0.0):
    """Keyed redraw (h, w, 3) -> colour (h, w, 3) and alpha (h, w): the distance from the key along the key's
    direction (for blue: B - (R + G) / 2) is (1 - a) times the key's own. With t > 0 the picture has colours of its
    own along that direction (up to t): those pixels are opaque, and an edge pixel is a mix of the key and the colour
    of the opaque pixels around it (their mean distance, a Gaussian of 4 pixels), so no key is left in its colour."""
    kd = key[2] - (key[0] + key[1]) / 2.0
    d = img[..., 2] - (img[..., 0] + img[..., 1]) / 2.0
    if t > 0:
        solid = (d < t).astype(np.float64)
        w = blur(solid[..., None], 4.0)[..., 0]
        dc = np.where(w > 1e-3, blur((d * solid)[..., None], 4.0)[..., 0] / np.maximum(w, 1e-3), 0.0)
        dc = np.minimum(dc, t)
        a = np.where(solid > 0, 1.0, np.clip((kd - d) / (kd - dc), 0.0, 1.0))
    else:
        a = np.clip(1.0 - d / kd, 0.0, 1.0)
    a = np.clip((a - 0.03) / 0.94, 0.0, 1.0)
    c = np.where(a[..., None] > 1e-3, (img - (1 - a[..., None]) * key) / np.maximum(a[..., None], 1e-3), 0.0)
    return np.clip(c, 0, 255), a


def normalise_key(red, t=0.0):
    """Measure the generator's key on the redraw's border and put the picture on the exact KEY."""
    h, w = red.shape[:2]
    b = max(2, min(h, w) // 50)
    border = np.concatenate([red[:b].reshape(-1, 3), red[-b:].reshape(-1, 3), red[:, :b].reshape(-1, 3),
                             red[:, -b:].reshape(-1, 3)])
    key = np.median(border, axis=0)
    c, a = unmix(red, key, t)
    return c * a[..., None] + KEY * (1 - a[..., None]), key


def canvas(name, w, h):
    m = int(round(spec(name)['margin'] * w))
    return w + 2 * m, h + 2 * m, m, m


def upload_scale(cw):
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


def colour_match(orig, alpha, red, p):
    """Per channel, a monotone curve that gives the redraw the original's distribution (quantile matching in the
    original's grid, where the original is opaque)."""
    fit = Fit(orig, np.zeros(3), red)
    fit.prepare(p[0])
    w = fit.warp(p, ss=4)
    mask = alpha > 0.5
    out = red.copy()
    q = np.linspace(0, 100, 257)
    for ch in range(3):
        xr = np.percentile(blur(w[..., ch:ch + 1], 0.5)[..., 0][mask], q)
        xo = np.percentile(blur(orig[..., ch:ch + 1], 0.5)[..., 0][mask], q)
        xr = np.maximum.accumulate(xr) + np.arange(len(xr)) * 1e-6
        out[..., ch] = np.interp(red[..., ch], xr, xo)
    return out


def level_match(orig, red, p, levels=(0, 10, 31)):
    """For a level map: a piecewise-linear grey curve through the redraw's plateaus. The redraw's median where the
    original is flat at each level (black, the star, the letters) goes to that level, values within the plateau's
    spread stay exactly on it (the original's areas are flat), and the edges in between are scaled linearly."""
    fit = Fit(orig, np.zeros(3), red)
    fit.prepare(p[0])
    w = fit.warp(p, ss=4)[..., 0]
    lv = np.round(orig[..., 0] * 31.0 / 255.0).astype(np.int32)
    xs, ys = [], []
    for L in levels:
        m = lv == L
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):   # away from the edges
            m &= np.roll(np.roll(lv, dy, 0), dx, 1) == L
        if m.sum() < 20:
            continue
        med = float(np.median(w[m]))
        t = float(np.percentile(np.abs(w[m] - med), 98))
        xs.append((med, t))
        ys.append(L * 255.0 / 31.0)
    xp, yp = [], []
    for i, ((med, t), y) in enumerate(zip(xs, ys)):
        lo_gap = med - xs[i - 1][0] if i else 1e9
        hi_gap = xs[i + 1][0] - med if i + 1 < len(xs) else 1e9
        t = min(t, 0.3 * lo_gap, 0.3 * hi_gap)
        xp += [med - t, med + t]
        yp += [y, y]
    print('  level plateaus: ' + ', '.join('%.0f +- %.0f -> %.0f' % (m, t, y) for (m, t), y in zip(xs, ys)))
    g = np.interp(red.mean(axis=2), xp, yp)
    return np.repeat(g[..., None], 3, axis=2)


def align(a):
    name = a.name
    s = spec(name)
    rgb, alpha = load_original(name)
    orig = composite(name, rgb, alpha)
    bg = background(name)
    H, W = alpha.shape
    red = np.asarray(Image.open(a.redraw).convert('RGB'), np.float64)
    if s['key'] is not None:
        red, key = normalise_key(red, s.get('key_t', 0.0))
        print('%s: the redraw\'s key colour %.1f, %.1f, %.1f' % ((name,) + tuple(key)))
    rw, rh = red.shape[1], red.shape[0]
    CW, CH, cx, cy = canvas(name, W, H)
    p0 = (CW / rw, CH / rh, -cx, -cy)
    fit = Fit(orig, bg, red)
    p = refine(fit, coarse_search(fit, p0, a.range, a.uniform), a.uniform)
    res, res0 = fit.residual(p), fit.residual(p0)
    print('%s: redraw %dx%d of a %dx%d canvas (picture %dx%d at %d, %d)' % (name, rw, rh, CW, CH, W, H, cx, cy))
    report(p, res, res0, p0)
    if a.match:
        if s['key'] is not None:
            c, al = unmix(red, KEY, s.get('key_t', 0.0))
            c = colour_match(orig, alpha, c, p)
            red = c * al[..., None] + KEY * (1 - al[..., None])
        elif s.get('levels'):
            red = level_match(orig, red, p)
        else:
            red = colour_match(orig, alpha, red, p)
        fm = Fit(orig, bg, red)
        fm.prepare(p[0])
        print('  after the colour match: residual %.1f / %.1f / %.2f %% / %.3f' % fm.residual(p))
    img = Image.fromarray(np.clip(np.round(red), 0, 255).astype(np.uint8))
    k_out = 1.0 / p[0]
    ow, oh = int(round(W * k_out)), int(round(H * SCREEN_ASPECT * k_out))
    out = render(img, (ow, oh), p, (W / ow, H / oh), tuple(int(c) for c in bg))
    if s['key'] is not None:
        c, al = unmix(np.asarray(out, np.float64), KEY, s.get('key_t', 0.0))
        out = Image.fromarray(np.clip(np.round(np.concatenate([c, al[..., None] * 255.0], axis=2)), 0, 255)
                              .astype(np.uint8), 'RGBA')
    if s.get('levels'):
        out = out.convert('L')   # a level map: grey 255 * level / 31, as the credits read it
    out_path = a.out or os.path.join(ART, name + '.png')
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    out.save(out_path, optimize=True)
    print('  -> %s %dx%d %s (%.0f kB)' % (os.path.relpath(out_path), out.size[0], out.size[1], out.mode,
                                         os.path.getsize(out_path) / 1000))


def upload(a):
    name = a.name
    rgb, alpha = load_original(name)
    H, W = alpha.shape
    CW, CH, cx, cy = canvas(name, W, H)
    c = np.empty((CH, CW, 3))
    c[:] = background(name)
    c[cy:cy + H, cx:cx + W] = composite(name, rgb, alpha)
    fx, fy = upload_scale(CW)
    img = Image.fromarray(np.round(c).astype(np.uint8))
    img.resize((CW * fx, CH * fy), Image.NEAREST).save(a.redraw)
    print('%s: %dx%d (canvas %dx%d original px, picture at %d, %d; %dx%d per pixel); it shows %s'
          % (a.redraw, CW * fx, CH * fy, CW, CH, cx, cy, fx, fy, spec(name)['what']))


def sheet(a):
    try:
        font = ImageFont.truetype('DejaVuSans-Bold.ttf', 16)
    except OSError:
        font = ImageFont.load_default()
    WIDTH = 1600
    rows = []
    for name in a.names:
        rgb, alpha = load_original(name)
        H, W = alpha.shape
        shown = Image.fromarray(np.round(rgb * alpha[..., None] + 60 * (1 - alpha[..., None])).astype(np.uint8))
        pw = WIDTH // 2
        ph = int(round(pw * H * SCREEN_ASPECT / W))
        pair = Image.new('RGB', (WIDTH, ph), (40, 40, 40))
        pair.paste(shown.resize((pw, ph), Image.NEAREST), (0, 0))
        art = os.path.join(a.art, name + '.png')
        if os.path.exists(art):
            r = Image.open(art).convert('RGBA')
            bgimg = Image.new('RGBA', r.size, (60, 60, 60, 255))
            pair.paste(Image.alpha_composite(bgimg, r).convert('RGB').resize((pw, ph), Image.LANCZOS), (pw, 0))
        d = ImageDraw.Draw(pair)
        d.text((6, 4), '%s: original | redraw' % name, fill=(255, 255, 0), font=font, stroke_width=2,
               stroke_fill=(0, 0, 0))
        rows.append(pair)
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
    ap.add_argument('--sheet')
    ap.add_argument('--upload', action='store_true')
    a = ap.parse_args()
    if a.sheet:
        a.names = [x for x in [a.name] + a.rest if x] or list(PICTURES)
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

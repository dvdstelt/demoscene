"""Fits a high-resolution redraw of one of Reve's pictures onto the original and writes art/NAME.png.

    python3 tools/align_art.py --upload NAME OUT.png         the nearest-neighbour picture to give the image generator
    python3 tools/align_art.py NAME REDRAW.png [--match]      fit the redraw and write art/NAME.png
    python3 tools/align_art.py --sheet OUT.jpg NAME [NAME ..] comparison sheet: original | redraw
    python3 tools/align_art.py --derive NAME                  a picture made of another one's pixels (TOM: MRTOMMY)
(inside the toolbox: numpy + PIL). From inside/remake/tools/align_art.py (itself from clone's and DOPE's).

NAME is a picture of PICTURES below (the remake's src/pictures.cpp has the same list): a RAW file of REVE.EXE's
archive ("mhwanh": 8-bit pixels and an 8-bit palette, which the demo gives the DAC as v >> 2), read with
reve/re/unpack.py from reve/original/REVE.EXE, shown through the palette the demo uses (its own or another RAW
file's), on the 320x200 screen at 4:3, so one original pixel is 1.2 times as tall as wide. A 6-bit value v is read as
255 * v / 63.

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
  --segments        then fit every letter (column group of the original) on its own (for lettering, as clone's
                    align_art fits the words of a caption)
  --out FILE        output (default ../art/NAME.png next to this script)
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 're'))
import unpack  # noqa: E402  (reve/re/unpack.py: the archive and its LZW)

ORIGINAL = os.path.join(HERE, '..', '..', 'original', 'REVE.EXE')
ART = os.path.join(HERE, '..', 'art')
SCREEN_ASPECT = 1.2
KEY = np.array([255.0, 0.0, 255.0])   # magenta: check that a keyed picture does not use it

# name: the RAW file, the RAW file whose palette it is shown with (None: its own), the part of the picture
# (x, y, w, h; None: all of it), the transparent index (None: opaque), the upload's margin around the picture (share
# of its width; keyed pictures only), what it shows (for the prompt). The decompiling notes (reve/re/notes-*.md) say
# which pictures are drawn as they are; src/pictures.cpp has the same list.
PICTURES = {
    # shown on a black screen (the title and the still pictures): uploaded and fitted as opaque pictures on black
    'title': dict(file='GFX\\REVE.RAW', pal='GFX\\TEXTURE.RAW', rect=None, key=None, margin=0.0,
                  what='the title in tall thin white letters: a mirrored R, an E with a bar above it, a V, an E'),
    'junior': dict(file='GFX\\JUNIOR.RAW', pal=None, rect=None, key=None, margin=0.0,
                   what='the word Junior in white hand lettering with a blue eye as the o, ".born to code" below, '
                        'on a dark red splash'),
    'mrtommy': dict(file='GFX\\MRTOMMY.RAW', pal=None, rect=None, key=None, margin=0.0,
                    what='a cartoon head of a man with a long brown face, round grey glasses and a dark red cap, '
                         'on a purple glow'),
    'dog': dict(file='GFX\\DOG.RAW', pal=None, rect=None, key=None, margin=0.0,
                what='the words Biscuit Addicted in white brush lettering over a dark red sign'),
    'waaf': dict(file='GFX\\MRWAAF.RAW', pal=None, rect=None, key=None, margin=0.0,
                 what='a cartoon dog with long ears sitting in brown rings, the word Waaf in purple, ".the dog"'),
    'yocol': dict(file='GFX\\YOCOL.RAW', pal=None, rect=None, key=None, margin=0.0,
                  what='a boy with spiky pink hair seen from the side, on a dark red background of squares'),
    'gene': dict(file='GFX\\GENE.RAW', pal=None, rect=None, key=None, margin=0.0,
                 what='the word Pulse in large ornate pink, purple and white letters on a red patterned background, '
                      'GENERATION in white below'),
    'bouge': dict(file='GFX\\BOUGE.RAW', pal=None, rect=None, key=None, margin=0.0,
                  what='a toy puppet: a mouse head with big ears, orange arms and green legs on a striped pole'),
    # textures of the perspective planes (notes-parts2d-b.md 4, 7): square texels, the 256 x 256 frame and the black
    # border kept (the planes tile them)
    'dada': dict(file='GFX\\DADA.RAW', pal=None, rect=None, key=None, margin=0.0, aspect=1.0,
                 what='a winged pony in pink and brown with dark spots and a pink mane, rearing, on black'),
    # TOM.RAW is MRTOMMY.RAW's 200 x 200 pixels at (53, 2) in a black 256 x 256 frame (same colours): its texture is
    # made from the mrtommy redraw (--derive tom), squeezed to square texels
    'tom': dict(file='GFX\\TOM.RAW', pal=None, rect=None, key=None, margin=0.0, aspect=1.0,
                derive=dict(source='mrtommy', at=(53, 2)),
                what='a cartoon head of a man with a long brown face, round grey glasses and a dark red cap, on a '
                     'purple glow'),
    'boz': dict(file='GFX\\BOZ.RAW', pal=None, rect=None, key=None, margin=0.0, aspect=1.0,
                what='a grey clown face with big eyes, red lips, a purple hat and red bows, on a dark speckled cloud'),
    'pulse': dict(file='GFX\\PULSE.RAW', pal=None, rect=None, key=None, margin=0.0,
                  what='a creature\'s head with goggles and teeth on a brown background with rings, the word Pulse '
                       'in white letters below'),
}

_files = None


def files():
    global _files
    if _files is None:
        exe = open(ORIGINAL, 'rb').read()
        _files = {r['name']: r for r in unpack.parse_archive(exe)[2]}
    return _files


def raw(name):
    """(pixels (h, w) uint8, palette (256, 3) 0..255 as the DAC shows it) of a RAW file of the archive."""
    r = files()[name]
    d, _ = unpack.lzw(r['data'], r['size'])
    w, h = int.from_bytes(d[8:10], 'big'), int.from_bytes(d[10:12], 'big')
    pal = (np.frombuffer(d[32:800], np.uint8).reshape(256, 3) >> 2).astype(np.float64) * 255.0 / 63.0
    return np.frombuffer(d[800:800 + w * h], np.uint8).reshape(h, w), pal


def spec(name):
    if name not in PICTURES:
        sys.exit('unknown picture %s (one of %s)' % (name, ', '.join(PICTURES)))
    return PICTURES[name]


def load_original(name):
    """RGB (h, w, 3) 0..255 and alpha (h, w) 0 or 1."""
    s = spec(name)
    px, pal = raw(s['file'])
    if s['pal']:
        pal = raw(s['pal'])[1]
    if s['rect']:
        x, y, w, h = s['rect']
        px = px[y:y + h, x:x + w]
    alpha = np.ones(px.shape) if s['key'] is None else (px != s['key']).astype(np.float64)
    return pal[px] * alpha[..., None], alpha


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


def upload_scale(cw, aspect=SCREEN_ASPECT):
    if aspect == 1.0:                       # square texels
        k = max(1, int(round(2000.0 / cw)))
        return k, k
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
        else:
            red = colour_match(orig, alpha, red, p)
        fm = Fit(orig, bg, red)
        fm.prepare(p[0])
        print('  after the colour match: residual %.1f / %.1f / %.2f %% / %.3f' % fm.residual(p))
    img = Image.fromarray(np.clip(np.round(red), 0, 255).astype(np.uint8))
    k_out = 1.0 / p[0]
    if a.segments:
        img, p = place_segments(orig, bg, img, p, s.get('aspect', SCREEN_ASPECT))
    ow, oh = int(round(W * k_out)), int(round(H * s.get('aspect', SCREEN_ASPECT) * k_out))
    out = render(img, (ow, oh), p, (W / ow, H / oh), tuple(int(c) for c in bg))
    if s['key'] is not None:
        c, al = unmix(np.asarray(out, np.float64), KEY, s.get('key_t', 0.0))
        out = Image.fromarray(np.clip(np.round(np.concatenate([c, al[..., None] * 255.0], axis=2)), 0, 255)
                              .astype(np.uint8), 'RGBA')
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
    fx, fy = upload_scale(CW, spec(name).get('aspect', SCREEN_ASPECT))
    img = Image.fromarray(np.round(c).astype(np.uint8))
    img.resize((CW * fx, CH * fy), Image.NEAREST).save(a.redraw)
    print('%s: %dx%d (canvas %dx%d original px, picture at %d, %d; %dx%d per pixel); it shows %s'
          % (a.redraw, CW * fx, CH * fy, CW, CH, cx, cy, fx, fy, spec(name)['what']))


def place_segments(orig, bg, img, p, aspect):
    """Fits every column group of the original (letters separated by background columns) on its own: the part of the
    redraw over each group is moved by the shift (in the original's pixels, searched in quarter pixels) that matches
    the original there best. Returns the redraw rebuilt from the moved parts, in the frame of p."""
    H, W = orig.shape[:2]
    used = np.abs(orig - bg).max(axis=(0, 2)) > 24
    groups, x = [], 0
    while x < W:
        if used[x]:
            x0 = x
            while x < W and (used[x:x + 3].any()):
                x += 1
            groups.append((x0, x))
        x += 1
    sx, sy, tx, ty = p
    red = np.asarray(img, np.float64)
    fit = Fit(orig, bg, red)
    fit.prepare(sx)
    out = np.empty_like(red)
    out[:] = bg
    rh, rw = red.shape[:2]
    for x0, x1 in groups:
        best = (np.inf, 0.0, 0.0)
        u = (np.arange((x1 - x0 + 8) * 4) + 0.5) / 4 + x0 - 4
        v = (np.arange(H * 4) + 0.5) / 4
        target = np.zeros((H, x1 - x0 + 8, 3))
        lo, hi = max(0, x0 - 4), min(W, x1 + 4)
        target[:] = bg
        target[:, lo - (x0 - 4):hi - (x0 - 4)] = orig[:, lo:hi]
        for dx in np.arange(-6, 6.01, 0.25):
            for dy in np.arange(-3, 3.01, 0.25):
                w_ = box_down(fit.sample(sx, sy, tx + dx, ty + dy, u, v), 4)
                c = np.mean((blur(w_, 0.6) - blur(target, 0.6)) ** 2)
                if c < best[0]:
                    best = (c, dx, dy)
        _, dx, dy = best
        print('  letters %d..%d: moved by %+.2f, %+.2f original px' % (x0, x1, dx, dy))
        # redraw columns over the group (a margin of 3 original pixels each side), shifted by (dx, dy)
        c0, c1 = int(np.floor((x0 - 3 - tx) / sx)), int(np.ceil((x1 + 3 - tx) / sx))
        sh_x, sh_y = int(round(dx / sx)), int(round(dy / sy))
        for cx in range(max(0, c0), min(rw, c1)):
            ox = cx + sh_x
            if 0 <= ox < rw:
                if sh_y >= 0:
                    out[sh_y:, ox] = red[:rh - sh_y, cx]
                else:
                    out[:rh + sh_y, ox] = red[-sh_y:, cx]
    fm = Fit(orig, bg, out)
    fm.prepare(sx)
    print('  after fitting the letters: residual %.1f / %.1f / %.2f %% / %.3f' % fm.residual(p))
    return Image.fromarray(np.clip(np.round(out), 0, 255).astype(np.uint8)), p


def derive(a):
    """A picture that holds another picture's pixels unchanged (spec 'derive'): the source's redraw, resampled to
    this picture's texel aspect and placed at the same offset in a black frame."""
    s = spec(a.name)
    src_name, (ox, oy) = s['derive']['source'], s['derive']['at']
    src = Image.open(os.path.join(a.art, src_name + '.png')).convert('RGB')
    _, salpha = load_original(src_name)
    sh, sw = salpha.shape
    _, alpha = load_original(a.name)
    H, W = alpha.shape
    k = src.size[0] / sw                                   # redraw pixels per original pixel, horizontally
    aspect = s.get('aspect', SCREEN_ASPECT)
    piece = src.resize((int(round(sw * k)), int(round(sh * k * aspect))), Image.LANCZOS)
    out = Image.new('RGB', (int(round(W * k)), int(round(H * k * aspect))), (0, 0, 0))
    out.paste(piece, (int(round(ox * k)), int(round(oy * k * aspect))))
    out_path = a.out or os.path.join(ART, a.name + '.png')
    out.save(out_path, optimize=True)
    print('%s: %s placed at (%d, %d) -> %s %dx%d' % (a.name, src_name, ox, oy, os.path.relpath(out_path), out.size[0],
                                                    out.size[1]))


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
        ph = int(round(pw * H * spec(name).get('aspect', SCREEN_ASPECT) / W))
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
    ap.add_argument('--segments', action='store_true', help='fit every letter (column group) on its own')
    ap.add_argument('--sheet')
    ap.add_argument('--upload', action='store_true')
    ap.add_argument('--derive', action='store_true', help='make NAME from the redraw of the picture it contains')
    a = ap.parse_args()
    if a.derive and a.name:
        derive(a)
    elif a.sheet:
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

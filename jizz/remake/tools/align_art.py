"""Fits a high-resolution redraw of one of JIZZ's two RLE pictures onto the original and writes art/NAME.png.

    python3 tools/align_art.py NAME REDRAW.png [--no-elements]   fit and write art/NAME.png
    python3 tools/align_art.py --upload NAME OUT.png             the nearest-neighbour picture to upload
    python3 tools/align_art.py --sheet OUT.jpg [NAME ...]        comparison sheet: original | redraw | overlay
(inside the toolbox: numpy + PIL). Adapted from clone/remake/tools/align_art.py.

NAME is `logo` or `lettering`, the two 4-bit run-length pictures (op 0x22, decoder 0x13df4) of texture script 14
(0x12a7d), jizz/re/notes-textures.md sections 5 and 7:

  logo       the picture at 0x27d68: Sjaak's "jizz" logo, white brush shapes with a small "jizz!" signature on
             black (levels 0 and 136..255, mostly 0 and 255). logo2 = blurred clouds x this picture; the "jizz"
             part (0x1212a) shows its rows 30..159 at screen rows 50..179 with a one-pixel dark outline.
  lettering  the picture at 0x275b8: "~JIZZ~" above "TBL'97" in serif capitals, light grey (the solid letters are
             level 238) on black. logo1 = blurred clouds x this picture is the end picture (0x18c25).

The script reads the pictures from the unpacked program image (jizz/re/jizz_img.bin; when that is missing it makes
it from jizz/original/JIZZ.EXE with jizz/re/unadam.py). Both are 256x200 with 16 grey levels (n * 17) and are
shown 1:1 on the 320x200 screen, which is shown at 4:3: one original pixel is 1.2 times as tall as wide.

Both are written as coverage masks, 8-bit grey, white (255) on black: the redraw's levels are stretched so that its
background is 0 and its solid shapes 255 (`coverage`). The remake multiplies the mask by the level of the
original's solid pixels (the picture's maximum: 255 for the logo, 238 for the lettering) and by the clouds. The fit
compares it with the original's coverage (the picture divided by that level).

Upload: the picture's non-zero bounding box with a margin of 12 % of its width on every side, on a black canvas of
square display shape (the shape ChatGPT makes at 1254x1254), scaled up with nearest-neighbour to the 4:3 display
shape (5k x 6k per pixel, about 2000 pixels wide).

The fit maps redraw pixel coordinates (x, y) to the original's pixel coordinates in that bounding box (u, v):
u = sx * x + tx, v = sy * y + ty. It compares both pictures in the original's grid (the redraw area-averaged into
it, both blurred so that detail the original does not have does not count): a brute-force search over scale and
offset on a coarse grid, then Nelder-Mead on (scale, aspect, tx, ty) with less and less blur. It prints the scale
(relative to "the redraw fills the upload's canvas") and offset and the residual (RMS of the difference 0..255 in
the original's grid, plain and with a 1-pixel blur, the share of pixels where one picture is background and the
other is not, and the correlation of the two pictures' detail, see `Fit.residual`), all over the bounding box.

The lettering is then fitted again per line (`elements`: split at empty rows), each starting from the whole's fit,
and each line is written exactly into its rectangle (the rectangles meet in the middle of the gap), so each line
stays at its pixels even where the generator spaced them a little differently.

The output covers exactly the original's 256x200 picture at the redraw's own pixel size (width = 256 / sx, height
from the pixel aspect); where the redraw does not reach it is black.

Options:
  --range R         search offsets up to R original pixels and scales up to 1 +- R/100 (default 12)
  --uniform         one scale for x and y
  --no-elements     lettering: keep the whole's fit for both lines
  --out FILE        output (default ../art/NAME.png next to this script)
  --img FILE        the unpacked program image (default ../../re/jizz_img.bin)
"""
import argparse
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..', 're')
IMAGE = os.path.join(RE, 'jizz_img.bin')
EXE = os.path.join(HERE, '..', '..', 'original', 'JIZZ.EXE')
ART = os.path.join(HERE, '..', 'art')
SCREEN_ASPECT = 1.2  # an original screen pixel is 1.2 times as tall as wide on a 4:3 screen
PW, PH = 256, 200    # the pictures' frame
MARGIN = 0.12        # the upload canvas's margin around the bounding box, as a share of its width
BLACK = np.zeros(3)

# addr: the picture in the program image (the op 0x22 arguments of script 0x12a7d); split: how the picture is cut
# into elements that are fitted on their own ('rows': at runs of at least n empty rows).
SPECS = {
    'logo': dict(addr=0x27d68),
    'lettering': dict(addr=0x275b8, split=('rows', 3)),
}


def spec(name):
    if name not in SPECS:
        sys.exit('unknown picture %s (one of %s)' % (name, ', '.join(SPECS)))
    return SPECS[name]


def program_image(path):
    if not os.path.exists(path):
        print('%s is missing: unpacking %s with re/unadam.py' % (path, EXE))
        subprocess.run([sys.executable, os.path.join(RE, 'unadam.py'), os.path.abspath(EXE),
                        os.path.join(os.path.dirname(os.path.abspath(path)), 'jizz')], check=True,
                       cwd=os.path.dirname(os.path.abspath(path)))
    with open(path, 'rb') as f:
        return f.read()


def rle4(img, addr):
    """0x13df4: nibbles, low first; n = 1..14 one pixel of n * 17, 0 + 4 nibbles a run of 0, 15 + 2 nibbles a run
    of 255; 256 x 200 pixels."""
    out = np.zeros(PW * PH, np.uint8)
    pos, hi, i = addr, False, 0

    def nib():
        nonlocal pos, hi
        b = img[pos]
        if hi:
            pos += 1
            hi = False
            return b >> 4
        hi = True
        return b & 15

    while i < PW * PH:
        n = nib()
        if n == 0:
            cnt = nib() | nib() << 4 | nib() << 8 | nib() << 12
            v = 0
        elif n == 15:
            cnt = nib() | nib() << 4
            v = 255
        else:
            cnt, v = 1, n * 17
        out[i:i + cnt] = v
        i += cnt
    return out.reshape(PH, PW).astype(np.float64)


_cache = {}


def load_original(name, img_path=IMAGE):
    """The picture as grey (200, 256) 0..255, as the program decodes it."""
    if name not in _cache:
        _cache[name] = rle4(program_image(img_path), spec(name)['addr'])
    return _cache[name]


def level(pic):
    """The level of the solid pixels: the picture's maximum (the remake reads it the same way)."""
    return float(pic.max())


def bbox(pic):
    ys, xs = np.nonzero(pic)
    return int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1


def frame(name):
    """The bounding box (x0, y0, x1, y1) and the original's coverage in it, 0..255, as (h, w, 3)."""
    pic = load_original(name)
    x0, y0, x1, y1 = bbox(pic)
    cov = np.clip(pic[y0:y1, x0:x1] / level(pic), 0, 1) * 255.0
    return (x0, y0, x1, y1), np.repeat(cov[..., None], 3, axis=2)


def canvas(w, h):
    """The upload canvas in original pixels (CW, CH), square in display shape, and the frame's position in it."""
    m = max(4, int(round(MARGIN * w)))
    cw, ch = w + 2 * m, h + 2 * m
    if cw > ch * SCREEN_ASPECT:
        ch = int(round(cw / SCREEN_ASPECT))
    else:
        cw = int(round(ch * SCREEN_ASPECT))
    return cw, ch, (cw - w) // 2, (ch - h) // 2


def upload_scale(cw):
    """Nearest-neighbour factors (5k, 6k): screen pixels at 4:3, about 2000 pixels wide."""
    k = max(1, int(round(2000.0 / (5 * cw))))
    return 5 * k, 6 * k


def elements(name, cov):
    """The rectangles (x0, y0, x1, y1) in frame coordinates fitted on their own, meeting in the middle of the gaps."""
    s = spec(name)
    if 'split' not in s:
        return []
    _, n = s['split']
    used = (cov[..., 0] > 0).any(axis=1)
    cuts, run = [], 0
    for i, u in enumerate(used):
        if not u:
            run += 1
            continue
        if run >= n and i - run > 0:
            cuts.append(i - run + run // 2)
        run = 0
    edges = [0] + cuts + [len(used)]
    w = cov.shape[1]
    return [(0, a, w, b) for a, b in zip(edges[:-1], edges[1:])]


def coverage(grey):
    """Redraw grey (h, w) -> coverage 0..255: background (median of the border, the canvas margin) to 0, the solid
    shapes (median of the pixels above half way to the bright end) to 255."""
    h, w = grey.shape
    b = max(2, min(h, w) // 50)
    border = np.concatenate([grey[:b].ravel(), grey[-b:].ravel(), grey[:, :b].ravel(), grey[:, -b:].ravel()])
    lo = float(np.median(border))
    top = float(np.percentile(grey, 99.9))
    hi = float(np.median(grey[grey > lo + 0.5 * (top - lo)]))
    return np.clip((grey - lo) / (hi - lo), 0, 1) * 255.0, lo, hi


# ---- fitting (as clone's, DOPE's and 303's)

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
    def __init__(self, orig, redraw):
        self.orig, self.bg = orig, BLACK
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
        fg_o = self.orig.max(axis=2) > 24
        fg_r = warped.max(axis=2) > 24
        # Correlation of the detail band (Gaussian 1 minus Gaussian 4 pixels) of the brightness: 1 when every shape
        # is where the original has it.
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
            u = np.arange(W + 2 * m * q) + 0.5 - m * q
            v = np.arange(H + 2 * m * q) + 0.5 - m * q
            img = box_down(blur(fit.sample(sx, sy, tx, ty, u, v), 0.75 * q), q)  # one cell = q pixels
            for dy in range(0, 2 * m + 1):
                for dx in range(0, 2 * m + 1):
                    win = img[dy:dy + H // q, dx:dx + W // q]
                    c = np.mean((win - ob[:win.shape[0], :win.shape[1]]) ** 2)
                    if c < best[0]:
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


def fit_element(orig, red, p, rect):
    """Fits the redraw again on one rectangle (x0, y0, x1, y1) of orig, starting from the whole's fit p: both scales
    and offsets, small blurs. Returns the fit in orig's coordinates, its residual on the rectangle and the residual
    of p there."""
    x0, y0, x1, y1 = rect
    sub = orig[y0:y1, x0:x1]
    sx, sy, tx, ty = p
    pe0 = (sx, sy, tx - x0, ty - y0)
    fit = Fit(sub, red)
    fit.prepare(sx)
    pe = pe0
    for sigma in (1.0, 0.5):
        def f(q, pe=pe):
            return fit.cost((pe[0] * np.exp(q[0]), pe[1] * np.exp(q[0] + q[1]), pe[2] + q[2], pe[3] + q[3]), sigma)
        q = nelder_mead(f, [0, 0, 0, 0], [0.01, 0.005, 0.3, 0.3])
        pe = (pe[0] * np.exp(q[0]), pe[1] * np.exp(q[0] + q[1]), pe[2] + q[2], pe[3] + q[3])
    if fit.cost(pe, 0.5) > fit.cost(pe0, 0.5):
        pe = pe0
    return (pe[0], pe[1], pe[2] + x0, pe[3] + y0), fit.residual(pe), fit.residual(pe0)


def render(img, size, p, scale_uv, origin):
    """Resamples img (PIL) so that output pixel (X, Y) shows frame coordinates (u0 + X * su, v0 + Y * sv)."""
    sx, sy, tx, ty = p
    su, sv = scale_uv
    u0, v0 = origin
    return img.transform(size, Image.AFFINE, (su / sx, 0, (u0 - tx) / sx, 0, sv / sy, (v0 - ty) / sy),
                         resample=Image.BICUBIC, fillcolor=0)


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
    (bx, by, bx1, by1), orig = frame(name)
    H, W = orig.shape[:2]
    pic = load_original(name)
    grey = np.asarray(Image.open(a.redraw).convert('L'), np.float64)
    cov, lo, hi = coverage(grey)
    red = np.repeat(cov[..., None], 3, axis=2)
    rw, rh = red.shape[1], red.shape[0]
    CW, CH, cx, cy = canvas(W, H)
    p0 = (CW / rw, CH / rh, -cx, -cy)
    fit = Fit(orig, red)
    p = refine(fit, coarse_search(fit, p0, a.range, a.uniform), a.uniform)
    res, res0 = fit.residual(p), fit.residual(p0)
    print('%s: redraw %dx%d of a %dx%d canvas (frame %dx%d at %d, %d; picture level %.0f); redraw levels: '
          'background %.1f, solid %.1f' % (name, rw, rh, CW, CH, W, H, cx, cy, level(pic), lo, hi))
    report(p, res, res0, p0)
    img = Image.fromarray(np.clip(np.round(cov), 0, 255).astype(np.uint8))
    els = []
    if not a.no_elements:
        for r in elements(name, orig):
            pe, rese, rese0 = fit_element(orig, red, p, r)
            uc, vc = (r[0] + r[2]) / 2.0, (r[1] + r[3]) / 2.0
            du = pe[0] * (uc - p[2]) / p[0] + pe[2] - uc
            dv = pe[1] * (vc - p[3]) / p[1] + pe[3] - vc
            print('  element %3d..%3d x %3d..%3d: moved %+.2f, %+.2f px, scaled %.4f x %.4f; residual %.1f / %.1f / '
                  '%.1f %% / %.2f (with the whole\'s fit %.1f / %.1f / %.1f %% / %.2f)'
                  % ((r[0], r[2], r[1], r[3], du, dv, pe[0] / p[0], pe[1] / p[1]) + rese + rese0))
            els.append((r, pe))
    # Output: the whole 256x200 picture at the redraw's own horizontal pixel size; frame coordinates are picture
    # coordinates minus (bx, by).
    k_out = 1.0 / p[0]
    ow, oh = int(round(PW * k_out)), int(round(PH * SCREEN_ASPECT * k_out))
    su, sv = PW / ow, PH / oh
    out = render(img, (ow, oh), p, (su, sv), (-bx, -by))
    for (x0, y0, x1, y1), pe in els:
        X0, X1 = int(round((x0 + bx) / su)), int(round((x1 + bx) / su))
        Y0, Y1 = int(round((y0 + by) / sv)), int(round((y1 + by) / sv))
        out.paste(render(img, (X1 - X0, Y1 - Y0), pe, (su, sv), (X0 * su - bx, Y0 * sv - by)), (X0, Y0))
    whole = Fit(orig, np.repeat(np.asarray(out, np.float64)[..., None], 3, axis=2))
    ps = (su, sv, -bx, -by)  # output pixel -> frame coordinates
    whole.prepare(ps[0])
    print('  written: residual %.1f / %.1f / %.2f %% / %.3f' % whole.residual(ps))
    out_path = a.out or os.path.join(ART, name + '.png')
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    out.save(out_path, optimize=True)
    print('  -> %s %dx%d %s (%.0f kB), %.2f output px per original px across'
          % (os.path.relpath(out_path), out.size[0], out.size[1], out.mode, os.path.getsize(out_path) / 1000, k_out))


def upload(a):
    """The picture to give the image generator: nearest-neighbour at 4:3 on its canvas."""
    name = a.name
    pic = load_original(name)
    x0, y0, x1, y1 = bbox(pic)
    W, H = x1 - x0, y1 - y0
    CW, CH, cx, cy = canvas(W, H)
    c = np.zeros((CH, CW))
    c[cy:cy + H, cx:cx + W] = pic[y0:y1, x0:x1]
    fx, fy = upload_scale(CW)
    img = Image.fromarray(np.round(c).astype(np.uint8))
    img.resize((CW * fx, CH * fy), Image.NEAREST).save(a.redraw)
    print('%s: %dx%d (canvas %dx%d original px, the %dx%d bounding box at %d, %d of the picture placed at %d, %d; '
          '%dx%d per pixel)' % (a.redraw, CW * fx, CH * fy, CW, CH, W, H, x0, y0, cx, cy, fx, fy))


# ---- comparison sheet

def sheet(a):
    try:
        font = ImageFont.truetype('DejaVuSans-Bold.ttf', 16)
    except OSError:
        font = ImageFont.load_default()
    rows = []
    for name in a.names:
        pic = load_original(name)
        x0, y0, x1, y1 = bbox(pic)
        m = 6
        x0, y0, x1, y1 = max(0, x0 - m), max(0, y0 - m), min(PW, x1 + m), min(PH, y1 + m)
        W, H = x1 - x0, y1 - y0
        pw = 640
        ph = int(round(pw * H * SCREEN_ASPECT / W))
        o = np.clip(pic[y0:y1, x0:x1] / level(pic), 0, 1) * 255
        orig = Image.fromarray(o.astype(np.uint8)).resize((pw, ph), Image.NEAREST)
        row = Image.new('RGB', (3 * pw + 8, ph), (40, 40, 40))
        row.paste(orig.convert('RGB'), (0, 0))
        art = os.path.join(a.art, name + '.png')
        if os.path.exists(art):
            r = Image.open(art).convert('L')
            su, sv = r.size[0] / PW, r.size[1] / PH
            crop = r.crop((int(round(x0 * su)), int(round(y0 * sv)), int(round(x1 * su)), int(round(y1 * sv))))
            crop = crop.resize((pw, ph), Image.LANCZOS)
            row.paste(crop.convert('RGB'), (pw + 4, 0))
            # overlay: the original's coverage in red, the redraw's in green and blue (white where both)
            ov = np.stack([np.asarray(orig, np.uint8), np.asarray(crop, np.uint8), np.asarray(crop, np.uint8)], -1)
            row.paste(Image.fromarray(ov), (2 * pw + 8, 0))
        d = ImageDraw.Draw(row)
        d.text((6, 4), '%s: original | redraw | overlay (original red, redraw cyan)' % name, fill=(255, 255, 0),
               font=font, stroke_width=2, stroke_fill=(0, 0, 0))
        rows.append(row)
    out = Image.new('RGB', (max(r.size[0] for r in rows), sum(r.size[1] + 6 for r in rows)), (40, 40, 40))
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
    ap.add_argument('--img', default=IMAGE)
    ap.add_argument('--art', default=ART)
    ap.add_argument('--uniform', action='store_true')
    ap.add_argument('--range', type=float, default=12)
    ap.add_argument('--no-elements', action='store_true')
    ap.add_argument('--sheet')
    ap.add_argument('--upload', action='store_true')
    a = ap.parse_args()
    for n in SPECS:
        load_original(n, a.img)
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

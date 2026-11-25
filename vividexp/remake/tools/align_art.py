"""Fits a high-resolution redraw of one of Vivid Experiment's pictures onto the original and writes art/NAME.png.

    python3 tools/align_art.py --upload NAME OUT.png         the nearest-neighbour picture to give the image generator
    python3 tools/align_art.py NAME REDRAW.png [--match]      fit the redraw and write art/NAME.png (or art/<name>.png
                                                             for each name of a group)
    python3 tools/align_art.py --sheet OUT.jpg NAME [NAME ..] comparison sheet: original | redraw
(inside the toolbox: numpy + PIL). The fit is inside/remake/tools/align_art.py's (from clone's and DOPE's).

NAME is a picture of PICTURES below: 8-bit pixels from VIVID.EXE's archives (read with vividexp/re/files.py from
vividexp/re/unpacked/vivid_img.bin; run vividexp/re/unpack.py first) shown through the palette the demo shows them
with, on the 320x200 screen at 4:3, so one original pixel is 1.2 times as tall as wide. A 6-bit value v is read as
255 * v / 63.

- opaque pictures: uploaded as they are, written as RGB;
- text pictures (alkutext and the six names of the zoom credits): white text with two grey anti-aliasing levels on
  black (index 0, which the demo draws as the sky or as transparent); the redraw is written as a grey coverage mask
  (L) covering the original's whole frame;
- groups (names1, names2): three names cropped from their 1280x200 pictures and stacked in one upload, so that they
  come back in one style; each name is fitted on its own in its part of the redraw.

The fit maps redraw pixel coordinates (x, y) to original pixel coordinates (u, v): u = sx * x + tx, v = sy * y + ty,
comparing both in the original's grid (the redraw area-averaged into it, both blurred so that detail the original
does not have does not count): a brute-force search over scale and offset, then Nelder-Mead with less and less blur.
It prints the residual (RMS of the colour difference 0..255 in the original's grid, plain and with a 1-pixel blur,
the share of pixels where one picture is background and the other is not, and the correlation of the detail).
--match gives the redraw the original's colour distribution per channel (quantile matching in the original's grid).

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
RE = os.path.join(HERE, '..', '..', 're')
sys.path.insert(0, RE)
import files  # noqa: E402

ART = os.path.join(HERE, '..', 'art')
SCREEN_ASPECT = 1.2
KEY = np.array([0.0, 0.0, 255.0])
TEXT_GREY = {129: 20, 130: 41, 131: 63}   # the text pictures' indices as taivas.pal / taivas2.pal show them

# name: archive file, palette file (None: the text greys), what it shows (for the prompt)
PICTURES = {
    'alkutext': dict(file='pics/alkutext.pcx', pal=None, text=True,
                     what='white serif lettering on black: several words and lines of text'),
    'siipiman': dict(file='pics/siipiman.pcx', pal='pal/siipiman.pal',
                     what='a painting of a muscular winged man flying against a yellow sun and an orange sky'),
}
# The zoom credits' names: 1280x200 pictures with the name around column 640 (notes-core.md 8); the crop: columns
# 360..919 and the name's rows with a margin of 6
NAMES = ['doom', 'mri', 'dice', 'damac', 'swallow', 'wode']
CROP_X0, CROP_W, CROP_M = 360, 560, 6
for _n in NAMES:
    PICTURES[_n] = dict(file='pics/%s.pcx' % _n, pal=None, text=True, name=True,
                        what='the word %s in white serif capitals' % _n.upper())
GROUPS = {'names1': ['doom', 'mri', 'dice'], 'names2': ['damac', 'swallow', 'wode']}
GROUP_GAP = 12        # original rows between the stacked names
GROUP_ASPECT = 2.0    # the group upload is padded to 2:1 (as shown at 4:3)

_img = None


def archive_file(name):
    global _img
    if _img is None:
        _img = open(os.path.join(RE, 'unpacked', 'vivid_img.bin'), 'rb').read()
    for table in files.TABLES:
        for n, addr, size in files.entries(_img, table):
            if n == name:
                return _img[addr - files.BASE:addr - files.BASE + size]
    sys.exit('no %s in VIVID.EXE' % name)


def decode_pcx(d):
    w = (d[8] | d[9] << 8) + 1
    h = (d[10] | d[11] << 8) + 1
    out = bytearray(w * h)
    i, o = 128, 0
    while o < w * h:
        b = d[i]; i += 1
        if b & 0xc0 == 0xc0:
            n = b & 0x3f; v = d[i]; i += 1
            out[o:o + n] = bytes([v]) * min(n, w * h - o); o += n
        else:
            out[o] = b; o += 1
    return np.frombuffer(bytes(out), np.uint8).reshape(h, w)


def spec(name):
    if name not in PICTURES and name not in GROUPS:
        sys.exit('unknown picture %s (one of %s)' % (name, ', '.join(list(PICTURES) + list(GROUPS))))
    return PICTURES.get(name, {})


def name_rows(px):
    ys = np.nonzero(px[:, CROP_X0:CROP_X0 + CROP_W].any(axis=1))[0]
    return max(0, ys.min() - CROP_M), min(px.shape[0], ys.max() + 1 + CROP_M)


def load_original(name):
    """RGB (h, w, 3) 0..255 and alpha (h, w) 1. A name: its crop."""
    s = spec(name)
    px = decode_pcx(archive_file(s['file']))
    if s.get('name'):
        y0, y1 = name_rows(px)
        px = px[y0:y1, CROP_X0:CROP_X0 + CROP_W]
    if s.get('text'):
        lut = np.zeros(256)
        for k, v in TEXT_GREY.items():
            lut[k] = v * 255.0 / 63.0
        g = lut[px]
        return np.repeat(g[..., None], 3, axis=2), np.ones(px.shape)
    pal = np.frombuffer(archive_file(s['pal']), np.uint8).reshape(256, 3).astype(np.float64)
    rgb = np.minimum(pal, 63) * 255.0 / 63.0
    return rgb[px], np.ones(px.shape)


def group_layout(group):
    """The stacked names: [(name, y offset, height)], canvas width and height in original pixels."""
    rows, y = [], 0
    for n in GROUPS[group]:
        rgb, _ = load_original(n)
        rows.append((n, y, rgb.shape[0]))
        y += rgb.shape[0] + GROUP_GAP
    content = y - GROUP_GAP
    ch = max(content, int(round(CROP_W / GROUP_ASPECT / SCREEN_ASPECT)))
    top = (ch - content) // 2
    return [(n, oy + top, h) for n, oy, h in rows], CROP_W, ch


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


def write_redraw(name, red, p, W, H, out_path=None):
    """The redraw resampled to cover the original's frame (a name: its whole 1280x200 picture), at the redraw's own
    resolution; text pictures as a grey coverage mask."""
    s = spec(name)
    img = Image.fromarray(np.clip(np.round(red), 0, 255).astype(np.uint8))
    k_out = 1.0 / p[0]
    if s.get('name'):
        px = decode_pcx(archive_file(s['file']))
        y0, _ = name_rows(px)
        FW, FH = px.shape[1], px.shape[0]
        ow, oh = int(round(FW * k_out)), int(round(FH * SCREEN_ASPECT * k_out))
        out = render(img, (ow, oh), p, (FW / ow, FH / oh), (0, 0, 0), origin=(-CROP_X0, -y0))
        # only the name's own crop: the stacked upload's other names lie above and below it
        y1 = name_rows(px)[1]
        a = np.asarray(out).copy()
        r0, r1 = int(np.floor(y0 * oh / FH)), int(np.ceil(y1 * oh / FH))
        c0, c1 = int(np.floor(CROP_X0 * ow / FW)), int(np.ceil((CROP_X0 + CROP_W) * ow / FW))
        keep = np.zeros(a.shape[:2], bool)
        keep[r0:r1, c0:c1] = True
        a[~keep] = 0
        out = Image.fromarray(a)
    else:
        ow, oh = int(round(W * k_out)), int(round(H * SCREEN_ASPECT * k_out))
        out = render(img, (ow, oh), p, (W / ow, H / oh), (0, 0, 0))
    if s.get('text'):
        out = out.convert('L')
    out_path = out_path or os.path.join(ART, name + '.png')
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    out.save(out_path, optimize=True)
    print('  -> %s %dx%d %s (%.0f kB)' % (os.path.relpath(out_path), out.size[0], out.size[1], out.mode,
                                         os.path.getsize(out_path) / 1000))


def fit_one(name, red, p0, a):
    rgb, alpha = load_original(name)
    orig = rgb
    bg = np.zeros(3)
    H, W = alpha.shape
    fit = Fit(orig, bg, red)
    p = refine(fit, coarse_search(fit, p0, a.range, a.uniform), a.uniform)
    res, res0 = fit.residual(p), fit.residual(p0)
    print('%s: picture %dx%d' % (name, W, H))
    report(p, res, res0, p0)
    if a.match and not spec(name).get('text'):
        red = colour_match(orig, alpha, red, p)
        fm = Fit(orig, bg, red)
        fm.prepare(p[0])
        print('  after the colour match: residual %.1f / %.1f / %.2f %% / %.3f' % fm.residual(p))
    return red, p, W, H


def align(a):
    red = np.asarray(Image.open(a.redraw).convert('RGB'), np.float64)
    rw, rh = red.shape[1], red.shape[0]
    if a.name in GROUPS:
        rows, CW, CH = group_layout(a.name)
        for n, oy, h in rows:
            # the redraw maps onto the canvas; this name's crop starts at canvas row oy
            p0 = (CW / rw, CH / rh, 0.0, -oy)
            red_n, p, W, H = fit_one(n, red, p0, a)
            write_redraw(n, red_n, p, W, H)
        return
    rgb, alpha = load_original(a.name)
    H, W = alpha.shape
    p0 = (W / rw, H / rh, 0.0, 0.0)
    red, p, W, H = fit_one(a.name, red, p0, a)
    write_redraw(a.name, red, p, W, H, a.out)


def upload(a):
    if a.name in GROUPS:
        rows, CW, CH = group_layout(a.name)
        c = np.zeros((CH, CW, 3))
        for n, oy, h in rows:
            rgb, _ = load_original(n)
            c[oy:oy + h] = rgb
        what = ', '.join(spec(n)['what'] for n, _, _ in rows)
    else:
        rgb, alpha = load_original(a.name)
        c = rgb
        CH, CW = c.shape[:2]
        what = spec(a.name)['what']
    fx, fy = upload_scale(CW)
    img = Image.fromarray(np.round(c).astype(np.uint8))
    img.resize((CW * fx, CH * fy), Image.NEAREST).save(a.redraw)
    print('%s: %dx%d (%dx%d original px, %dx%d per pixel); it shows %s'
          % (a.redraw, CW * fx, CH * fy, CW, CH, fx, fy, what))


def sheet(a):
    try:
        font = ImageFont.truetype('DejaVuSans-Bold.ttf', 16)
    except OSError:
        font = ImageFont.load_default()
    WIDTH = 1600
    rows = []
    names = []
    for n in a.names:
        names += GROUPS.get(n, [n])
    for name in names:
        rgb, alpha = load_original(name)
        H, W = alpha.shape
        shown = Image.fromarray(np.round(rgb).astype(np.uint8))
        pw = WIDTH // 2
        ph = int(round(pw * H * SCREEN_ASPECT / W))
        pair = Image.new('RGB', (WIDTH, ph), (40, 40, 40))
        pair.paste(shown.resize((pw, ph), Image.NEAREST), (0, 0))
        art = os.path.join(a.art, name + '.png')
        if os.path.exists(art):
            r = Image.open(art).convert('RGB')
            if spec(name).get('name'):
                px = decode_pcx(archive_file(spec(name)['file']))
                y0, y1 = name_rows(px)
                sx, sy = r.size[0] / px.shape[1], r.size[1] / px.shape[0]
                r = r.crop((int(CROP_X0 * sx), int(y0 * sy), int((CROP_X0 + CROP_W) * sx), int(y1 * sy)))
            pair.paste(r.resize((pw, ph), Image.LANCZOS), (pw, 0))
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

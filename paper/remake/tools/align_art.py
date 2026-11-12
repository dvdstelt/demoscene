"""Fits a high-resolution redraw of Paper's "Paper" sketch onto the original and writes the aligned shade map.

    python3 tools/align_art.py sketch REDRAW.png [--out art/sketch.png]   (inside the toolbox: numpy + PIL)
    python3 tools/align_art.py --upload sketch OUT.png                    the nearest-neighbour picture to upload
    python3 tools/align_art.py --sheet OUT.jpg sketch                     comparison sheet of art/sketch.png

Adapted from stars/remake/tools/align_art.py and dope/remake/tools/align_art.py. Paper has one picture that is
drawn as it is: the 224x83 pencil sketch of the word "Paper" at code32 0x1bf03 (4 bits per pixel, high nibble
first, n = darkness 0..15, 0 = paper), which 0x5623 puts on the logo page at (48, 50) as the page value
v = 63 - 4 n (paper/re/notes-logo.md, section 3). The script reads it from the unpacked program image
(paper/re/dump/paper_img.bin; when that is missing it makes it from paper/original/PAPER.EXE with
paper/re/unpklite.py). The other image of the logo part, the ink image at 0x196c3, is a map of palette indices
that the flood fill colours; it is not redrawn (the remake upscales it, see src/scenes/logo.cpp).

The sketch is a shade map: it is uploaded and written as grey, grey = 255 * v / 63 (paper white 255, the darkest
pencil, n = 11, grey 77). The remake reads v = 63 * grey / 255 and shades it with the paper grain exactly as it
shades the original's v. The page is shown at 4:3, so one original pixel is 1.2 times as tall as wide. The upload
puts the sketch on a 2:1 canvas of paper white (240 x 100 original pixels, the sketch at (8, 8)) so that the
generator does not crop the strokes at the edges, and scales it up with nearest-neighbour, 10 x 12 per pixel
(2400 x 1200).

The fit maps redraw pixel coordinates (x, y) to original pixel coordinates (u, v):
    u = sx * x + tx,   v = sy * y + ty
A redraw that has the upload's framing and fills its frame has sx = 240 / width, sy = 100 / height and the offset
of the canvas (-8, -8). The fit compares both pictures in the original's grid (the redraw area-averaged into it,
both blurred so that detail the original does not have does not count): a brute-force search over scale and offset
on a coarse grid, then Nelder-Mead on (scale, aspect, tx, ty) with less and less blur. It prints the scale and
offset and the residual (RMS of the grey difference 0..255 in the original's grid, plain and with a 1-pixel blur,
the share of pixels where one picture is paper and the other is not, and the correlation of the two pictures'
detail, see `Fit.residual`), and writes the redraw resampled to cover exactly the sketch's 224x83 frame at the
redraw's own pixel size (output width = 224 / sx, height from the pixel aspect), as an 8-bit greyscale PNG. Where
the redraw does not reach, it pads with paper white.

Tones: the generator's paper is rarely exactly white, and the remake shows the page value 63 around the sketch, so
the redraw's tones are mapped onto the original's: a monotone tone curve from quantile matching in the original's
grid (see `tone_curve`), which also makes the redraw's paper white, and greys from 252 up are set to white
(`paper_white`: the generator's paper keeps a faint noise of a level or two). `--no-match` keeps the redraw's own
greys (only the paper level is scaled to white).

Options:
  --out FILE        output (default ../art/sketch.png next to this script)
  --img FILE        the unpacked program image (default ../../re/dump/paper_img.bin)
  --uniform         keep the redraw's pixels square (one scale), instead of fitting x and y separately
  --range R         search offsets up to R original pixels and scales up to 1 +- R/100 (default 12)
  --no-match        do not map the redraw's tones onto the original's (see above)
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..', 're')
IMAGE = os.path.join(RE, 'dump', 'paper_img.bin')
EXE = os.path.join(HERE, '..', '..', 'original', 'PAPER.EXE')
CODE32 = 0xd00  # image offset of the 32-bit segment
SCREEN_ASPECT = 1.2  # an original screen pixel is 1.2 times as tall as wide on a 4:3 screen
PAPER = 255.0

# addr: code32 offset of the 4-bit image, w x h: its size, canvas: the upload canvas (CW, CH) and the frame's
# position (cx, cy) in it, in original pixels, up: nearest-neighbour factors of the upload.
SPECS = {
    'sketch': dict(addr=0x1bf03, w=224, h=83, canvas=(240, 100, 8, 8), up=(10, 12)),
}


def spec(name):
    if name not in SPECS:
        sys.exit('unknown picture %s (one of %s)' % (name, ', '.join(SPECS)))
    return SPECS[name]


def program_image(path):
    """The unpacked program image (paper/re/dump/paper_img.bin), made from PAPER.EXE when it is missing."""
    if not os.path.exists(path):
        sys.path.insert(0, RE)
        import unpklite  # paper/re/unpklite.py: runs PKLITE's own stub in unicorn
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        prefix = os.path.join(os.path.dirname(os.path.abspath(path)), 'paper_unp')
        unpklite.main(EXE, prefix)
        with open(prefix + '.mem', 'rb') as f:
            mem = f.read()
        with open(path, 'wb') as f:
            f.write(mem[0x100:0x100 + 0x269e0])
    with open(path, 'rb') as f:
        return f.read()


def load_original(name, img_path=IMAGE):
    """The sketch as grey (h, w, 3) float, 255 * v / 63 with v = 63 - 4 n (0x58ea with invert = 1)."""
    s = spec(name)
    img = program_image(img_path)
    w, h = s['w'], s['h']
    b = np.frombuffer(img, np.uint8, h * w // 2, CODE32 + s['addr'])
    n = np.empty((h, w), np.int64)
    n[:, 0::2] = (b >> 4).reshape(h, w // 2)
    n[:, 1::2] = (b & 15).reshape(h, w // 2)
    g = (63 - 4 * n) * 255.0 / 63.0
    return np.repeat(g[..., None], 3, axis=2)


def on_canvas(name, orig):
    cw, ch, cx, cy = spec(name)['canvas']
    c = np.full((ch, cw, 3), PAPER)
    c[cy:cy + orig.shape[0], cx:cx + orig.shape[1]] = orig
    return c


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
        self.rw, self.rh = redraw.shape[1], redraw.shape[0]
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
        # Correlation of the detail band (Gaussian 2 minus Gaussian 12 pixels) of the brightness: 1 when every
        # stroke is where the original has it; about 0 for unrelated pictures.
        lo, lw = self.orig.mean(axis=2, keepdims=True), warped.mean(axis=2, keepdims=True)
        do, dw = blur(lo, 2.0) - blur(lo, 12.0), blur(lw, 2.0) - blur(lw, 12.0)
        corr = float(np.corrcoef(do.ravel(), dw.ravel())[0, 1])
        return rms, rms1, float(np.mean(fg_o != fg_r)) * 100, corr


def coarse_search(fit, p0, rng, uniform):
    """Brute force over scale (and aspect) around p0 (scaled about the frame's centre) and offset, blurred."""
    W, H = fit.W, fit.H
    q = 2  # coarse cell in original pixels
    ob = box_down(blur(fit.orig, 0.75 * q), q)
    best = (np.inf, None)
    scales = 1 + np.arange(-rng, rng + 1, 1.25) / 100.0
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


def refine(fit, p, uniform):
    sx, sy, tx, ty = p
    fit.prepare(sx)
    for sigma in (2.0, 1.0, 0.6):
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


def tone_curve(orig, red, p, bg):
    """A tone curve that gives the redraw the original's grey distribution: quantile matching of the redraw in the
    original's grid (area-averaged) against the original, both blurred by 1 pixel so that the original's 16 steps do
    not count. Monotone, so every stroke stays; the redraw's paper (its upper quantiles) becomes exactly white.
    Returns a function of an (h, w, 3) array 0..255."""
    fit = Fit(orig, bg, red)
    fit.prepare(p[0])
    w, o = blur(fit.warp(p, ss=4), 1.0)[..., 0], blur(orig, 1.0)[..., 0]
    q = np.linspace(0, 100, 257)
    xr, xo = np.percentile(w, q), np.percentile(o, q)
    xr = np.maximum.accumulate(xr) + np.arange(len(xr)) * 1e-6  # strictly increasing for interp
    xo = np.maximum.accumulate(xo)
    xp = np.concatenate([[min(0.0, xr[0] - 1)], xr, [max(255.0, xr[-1] + 1)]])
    fp = np.concatenate([[max(0.0, xo[0] - (xr[0] - xp[0]))], xo, [PAPER]])

    def apply(a):
        return np.interp(a, xp, np.clip(fp, 0, 255))
    return apply


def paper_white(a, lo=244.0, hi=252.0):
    """The generator's paper keeps a faint noise of a level or two below white after the tone match; greys from hi
    up become white, greys from lo to hi are stretched to lo..255 (the faintest strokes, the "stx" signature at
    239, are below lo and stay)."""
    return np.where(a >= hi, PAPER, np.where(a > lo, lo + (a - lo) * (PAPER - lo) / (hi - lo), a))


def render(img, size, p, scale_uv, offset, fill):
    """Resamples img (PIL) so that output pixel (X, Y) shows original coordinates (ox + X * su, oy + Y * sv) under
    fit p."""
    sx, sy, tx, ty = p
    su, sv = scale_uv
    ox, oy = offset
    return img.transform(size, Image.AFFINE, (su / sx, 0, (ox - tx) / sx, 0, sv / sy, (oy - ty) / sy),
                         resample=Image.BICUBIC, fillcolor=fill)


def report(name, p, res, res0, p0):
    sx, sy, tx, ty = p
    rel, rely = sx / p0[0], sy / p0[1]
    print('  scale %.4f x %.4f of frame-filling (sx %.5f, sy %.5f original px per redraw px, pixel aspect %.4f), '
          'offset (%.2f, %.2f) original px (frame-filling: %.2f, %.2f)'
          % (rel, rely, sx, sy, sy * SCREEN_ASPECT / sx, tx, ty, p0[2], p0[3]))
    print('  residual: RMS %.1f, blurred 1 px %.1f, paper mismatch %.2f %%, detail correlation %.3f   '
          '(redraw just stretched to the frame: %.1f, %.1f, %.2f %%, %.3f)' % (res + res0))
    print('  | %s | %.4f x %.4f | %+.1f, %+.1f | %.1f / %.1f / %.1f %% / %.2f |'
          % ((name, rel, rely, tx - p0[2], ty - p0[3]) + res))


def align(a):
    name = a.name
    s = spec(name)
    orig = load_original(name, a.img)
    cw, ch, cx, cy = s['canvas']
    canvas = on_canvas(name, orig)
    img = Image.open(a.redraw).convert('L').convert('RGB')
    red = np.asarray(img, np.float64)
    rw, rh = red.shape[1], red.shape[0]
    # Fitted against the whole canvas (the paper margins included), in canvas coordinates.
    p0 = (cw / rw, ch / rh, 0.0, 0.0)
    bg = np.array([PAPER] * 3)
    p, res, res0 = fit_picture(canvas, bg, red, p0, a.range or 12, a.uniform)
    print('%s: redraw %dx%d of a %dx%d canvas (frame at %d, %d)' % (name, rw, rh, cw, ch, cx, cy))
    report(name, p, res, res0, p0)
    if a.match:
        red = paper_white(np.clip(tone_curve(canvas, red, p, bg)(red), 0, 255))
        fm = Fit(canvas, bg, red)
        fm.prepare(p[0])
        print('  after the tone match: | %.1f / %.1f / %.1f %% / %.2f |' % fm.residual(p))
    else:
        paper = float(np.percentile(red[..., 0], 75))  # most of the canvas is paper
        red = np.clip(red * (PAPER / max(paper, 1.0)), 0, 255)
        print('  paper level %.1f set to white' % paper)
    img = Image.fromarray(np.round(red).astype(np.uint8))
    # Output: the sketch's frame at the redraw's own horizontal pixel size.
    W, H = s['w'], s['h']
    ow = int(round(W / p[0]))
    oh = int(round(ow * H * SCREEN_ASPECT / W))
    out = render(img, (ow, oh), p, (W / ow, H / oh), (cx, cy), (255, 255, 255)).convert('L')
    out_path = a.out or os.path.join(HERE, '..', 'art', name + '.png')
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    out.save(out_path, optimize=True)
    print('  -> %s %dx%d %s (%.0f kB)' % (os.path.basename(out_path), out.size[0], out.size[1], out.mode,
                                         os.path.getsize(out_path) / 1000))


def upload(a):
    """The picture to give the image generator: nearest-neighbour, on its paper-white canvas."""
    name = a.name
    s = spec(name)
    c = on_canvas(name, load_original(name, a.img))
    fx, fy = s['up']
    img = Image.fromarray(np.round(c).astype(np.uint8)).convert('L')
    ch, cw = c.shape[:2]
    img.resize((cw * fx, ch * fy), Image.NEAREST).save(a.redraw)
    print('%s: %dx%d' % (a.redraw, cw * fx, ch * fy))


def sheet(a):
    """Original (nearest-neighbour) above the redraw, and an overlay: the original's strokes in red over the redraw
    in grey, where they agree the strokes look dark red."""
    try:
        font = ImageFont.truetype('DejaVuSans-Bold.ttf', 16)
    except OSError:
        font = ImageFont.load_default()
    rows = []
    pw = 1600
    for name in a.names:
        s = spec(name)
        orig = load_original(name, a.img)
        W, H = s['w'], s['h']
        ph = int(round(pw * H * SCREEN_ASPECT / W))
        left = Image.fromarray(np.round(orig).astype(np.uint8)).resize((pw, ph), Image.NEAREST)
        art = os.path.join(a.art, name + '.png')
        panels = [(left, '%s: original' % name)]
        if os.path.exists(art):
            r = Image.open(art).convert('L').resize((pw, ph), Image.LANCZOS)
            panels.append((r.convert('RGB'), '%s: redraw (%s)' % (name, os.path.basename(art))))
            o = np.asarray(Image.fromarray(np.round(orig[..., 0]).astype(np.uint8)).resize((pw, ph), Image.BILINEAR),
                           np.float64) / 255.0
            g = np.asarray(r, np.float64) / 255.0
            ov = np.stack([g, g * o, g * o], axis=2)  # the original's darkness removes green and blue
            panels.append((Image.fromarray(np.round(ov * 255).astype(np.uint8)),
                           '%s: overlay (original in red over the redraw)' % name))
        for im, label in panels:
            d = ImageDraw.Draw(im)
            d.text((8, 6), label, fill=(255, 200, 0), font=font, stroke_width=2, stroke_fill=(0, 0, 0))
            rows.append(im)
    out = Image.new('RGB', (pw, sum(r.size[1] + 4 for r in rows)), (40, 40, 40))
    y = 0
    for r in rows:
        out.paste(r, (0, y))
        y += r.size[1] + 4
    out.save(a.sheet, quality=90)
    print('%s: %d panels, %dx%d' % (a.sheet, len(rows), out.size[0], out.size[1]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('name', nargs='?')
    ap.add_argument('redraw', nargs='?')
    ap.add_argument('--out')
    ap.add_argument('--img', default=IMAGE)
    ap.add_argument('--art', default=os.path.join(HERE, '..', 'art'))
    ap.add_argument('--uniform', action='store_true')
    ap.add_argument('--range', type=float, default=0)
    ap.add_argument('--no-match', dest='match', action='store_false')
    ap.add_argument('--sheet')
    ap.add_argument('--upload', action='store_true')
    a = ap.parse_args()
    if a.sheet:
        a.names = [x for x in [a.name, a.redraw] if x]
        sheet(a)
    elif a.upload and a.name and a.redraw:
        upload(a)
    elif a.name and a.redraw:
        align(a)
    else:
        ap.print_help()
        sys.exit(1)


if __name__ == '__main__':
    main()

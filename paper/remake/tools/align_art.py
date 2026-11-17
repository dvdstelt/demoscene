"""Fits a high-resolution redraw of one of the pictures on Paper's logo page onto the original and writes it to art/.

    python3 tools/align_art.py NAME REDRAW.png [--out art/NAME.png]     (inside the toolbox: numpy + PIL)
    python3 tools/align_art.py --upload NAME OUT.png                    the nearest-neighbour picture to upload
    python3 tools/align_art.py --sheet OUT.jpg NAME [NAME ...]          comparison sheet of art/NAME.png

NAME is `sketch`, `text` or `ink`. Adapted from stars/remake/tools/align_art.py and dope/remake/tools/align_art.py.
The three pictures are drawn by 0x5623 / 0x572f on the "present the Paper intro" page (paper/re/notes-logo.md,
sections 3 and 4); the script reads them from the unpacked program image (paper/re/dump/paper_img.bin; when that is
missing it makes it from paper/original/PAPER.EXE with paper/re/unpklite.py):

  sketch  the 224x83 pencil sketch of the word "Paper" at code32 0x1bf03 (4 bits per pixel, high nibble first,
          n = darkness 0..15, 0 = paper), put on the page at (48, 50) as the page value v = 63 - 4 n. A shade map:
          uploaded and written as grey, grey = 255 * v / 63 (paper white 255, the darkest pencil, n = 11, grey 77);
          the remake reads v = 63 * grey / 255 and shades it with the paper grain exactly as the original's v. Canvas
          240 x 100 original pixels of paper white, the sketch at (8, 8).
  text    the sharp copy of "present the\x01intro" (0x5929) that 0x5623 draws with the 1-bit font (0xe2e, RLE at
          0x18ed7) into its 320x80 text buffer from the pen (16, 1) ("intro" lands at (175, 40)). A coverage mask:
          uploaded and written white on black (255 = letter); the remake draws the letters at its 0.5 contour and
          builds the shadow from it. Canvas 324 x 135 of black, the buffer at (2, 27).
  ink     the finished ink: the 224x92 ink image at 0x196c3 (4 bit, 0 = no ink, n = palette colour 0x40 + n of the
          default palette 0x169a3) in its colours on white, as 0x572f's flood fill leaves it, at (48, 50) on the page.
          A colour picture with alpha: the redraw is asked for on a transparent background and written as RGBA; the
          remake reveals it with the fill timing it computes from the original's map. Canvas 252 x 105 of white,
          the image at (14, 6).

The page is shown at 4:3, so one original pixel is 1.2 times as tall as wide. The uploads put the picture on a 2:1
canvas with a margin, so that the generator does not crop it at the edges, and scale it up with nearest-neighbour,
10 x 12 per pixel (sketch 2400 x 1200, text 3240 x 1620, ink 2520 x 1260).

The fit maps redraw pixel coordinates (x, y) to canvas pixel coordinates (u, v):
    u = sx * x + tx,   v = sy * y + ty
A redraw that has the upload's framing and fills it has sx = canvas width / width, sy = canvas height / height and
offset 0. The fit compares both pictures in the original's grid (the redraw area-averaged into it, over the canvas
colour where it has alpha, both blurred so that detail the original does not have does not count): a brute-force
search over scale and offset on a coarse grid, then Nelder-Mead on (scale, aspect, tx, ty) with less and less
blur. It prints the scale and offset and the residual (RMS of the colour difference 0..255 in the original's grid,
plain and with a 1-pixel blur, the share of pixels where one picture is background and the other is not, and the
correlation of the two pictures' detail, see `Fit.residual`), and writes the redraw resampled to cover exactly the
picture's frame at the redraw's own pixel size (output width = frame width / sx, height from the pixel aspect).

Tones:
  sketch  the generator's paper is rarely exactly white, and the remake shows the page value 63 around the sketch,
          so the redraw's tones are mapped onto the original's: a monotone tone curve from quantile matching in the
          original's grid (see `tone_curve`), which also makes the redraw's paper white, and greys from 252 up are
          set to white (`paper_white`: the generator's paper keeps a faint noise of a level or two).
  text    the levels are stretched so that the background is 0 and the solid letters 255; the mismatch of the 0.5
          contour with the original's pixel edges is printed as a share of the letters' pixels.
  ink     the ink body gets the original's body colour 0x4b: per channel a piecewise-linear curve through 0, the
          redraw's body (median of its opaque pixels) -> the original's (median of its ink pixels), and 255
          (`body_match`); colour and alpha are resampled premultiplied.
  `--no-match` keeps the redraw's own tones (sketch: only the paper level is scaled to white; ink: no colour match).

Options:
  --out FILE        output (default ../art/NAME.png next to this script)
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

# kind: 'shade' (a grey shade map), 'mask' (white on black coverage) or 'colour' (a colour picture with alpha);
# w x h: the frame in original pixels; canvas: the upload canvas (CW, CH) and the frame's position (cx, cy) in it,
# in original pixels; up: nearest-neighbour factors of the upload (10 x 12 = the 4:3 display shape); bg: the
# canvas colour (0..255).
SPECS = {
    'sketch': dict(kind='shade', addr=0x1bf03, w=224, h=83, canvas=(240, 100, 8, 8), up=(10, 12), bg=255.0),
    # the sharp copy of "present the\x01intro" in 0x5623's 320x80 text buffer (pen (16, 1)), white on black
    'text': dict(kind='mask', w=320, h=80, canvas=(324, 135, 2, 27), up=(10, 12), bg=0.0),
    # the finished ink: the ink image 0x196c3 (224x92) in its palette colours 0x41..0x4f, on white
    'ink': dict(kind='colour', addr=0x196c3, w=224, h=92, canvas=(252, 105, 14, 6), up=(10, 12), bg=255.0),
}
FONT_RLE = 0x18ed7     # 0xe2e: 29 one-bit glyphs, 32 rows
TEXT = 0x5929          # "present the\x01intro\0"
TEXT_PEN = (16, 1)     # 0x5623: the sharp copy in the text buffer (stride 320)
PALETTE = 0x169a3      # 768 bytes, 6-bit


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


def nibbles(img, addr, w, h):
    """0x58ea: a 4-bit image, high nibble = left pixel."""
    b = np.frombuffer(img, np.uint8, h * w // 2, CODE32 + addr)
    n = np.empty((h, w), np.int64)
    n[:, 0::2] = (b >> 4).reshape(h, w // 2)
    n[:, 1::2] = (b & 15).reshape(h, w // 2)
    return n


def font_glyphs(img):
    """0xe2e: the 29 glyphs ('a'..'}') as (width, 32 x 60 bool array)."""
    src = CODE32 + FONT_RLE
    glyphs = []
    for _ in range(29):
        w = img[src]
        src += 1
        g = np.zeros((32, 60), bool)
        for y in range(32):
            x = 0
            while True:
                b = img[src]
                src += 1
                if b == 0:
                    break
                if b == 0xff:
                    skip, run = img[src], img[src + 1]
                    src += 2
                else:
                    skip, run = b & 15, b >> 4
                x += skip
                g[y, x:x + run] = True
                x += run
        glyphs.append((w, g))
    return glyphs


def text_mask(img):
    """0x5623's sharp text in the 320x80 text buffer: 0x11b0 / 0xe8a from the pen (16, 1), linear addresses with
    stride 320 (code 1 adds 10 + 0x303e: "intro" lands at (175, 40)). 255 = ink."""
    glyphs = font_glyphs(img)
    end = img.index(b'\0', CODE32 + TEXT)
    s = img[CODE32 + TEXT:end]
    buf = np.zeros(320 * 120, np.float64)
    pos = TEXT_PEN[1] * 320 + TEXT_PEN[0]
    for ch in s:
        c = ch - ord('a')
        if c < 0:
            pos += 10
            if ch == 1:
                pos += 0x303e
            continue
        w, g = glyphs[c]
        for y in range(32):
            row = buf[pos + y * 320:pos + y * 320 + w]
            row[g[y, :w]] = 255.0
        pos += w + 1
    return buf[:320 * 80].reshape(80, 320)


def palette8(img):
    """The default palette 0x169a3, 6-bit values scaled to 0..255 (display v / 63)."""
    p = np.frombuffer(img, np.uint8, 768, CODE32 + PALETTE).reshape(256, 3).astype(np.float64)
    return p * 255.0 / 63.0


def load_original(name, img_path=IMAGE):
    """The original picture as an (h, w, 3) float array 0..255: the sketch as grey 255 * v / 63 with v = 63 - 4 n
    (0x58ea with invert = 1), the text as white on black, the ink in its palette colours on white."""
    s = spec(name)
    img = program_image(img_path)
    if name == 'text':
        g = text_mask(img)
        return np.repeat(g[..., None], 3, axis=2)
    n = nibbles(img, s['addr'], s['w'], s['h'])
    if name == 'ink':
        pal = palette8(img)
        out = np.full((s['h'], s['w'], 3), 255.0)
        out[n > 0] = pal[0x40 + n[n > 0]]
        return out
    g = (63 - 4 * n) * 255.0 / 63.0
    return np.repeat(g[..., None], 3, axis=2)


def ink_map(img_path=IMAGE):
    """The ink image's values (92 x 224, 0 = no ink, n = colour 0x40 + n)."""
    s = spec('ink')
    return nibbles(program_image(img_path), s['addr'], s['w'], s['h'])


def on_canvas(name, orig):
    cw, ch, cx, cy = spec(name)['canvas']
    c = np.full((ch, cw, 3), spec(name)['bg'])
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


def frame_size(s, p):
    """The output size: the frame at the redraw's own horizontal pixel size, 4:3 pixel aspect."""
    W, H = s['w'], s['h']
    ow = int(round(W / p[0]))
    return ow, int(round(ow * H * SCREEN_ASPECT / W))


def render_frame(img, s, p, fill):
    """The fitted redraw (PIL image) resampled to cover exactly the frame."""
    cw, ch, cx, cy = s['canvas']
    ow, oh = frame_size(s, p)
    return render(img, (ow, oh), p, (s['w'] / ow, s['h'] / oh), (cx, cy), fill)


def render_float(a, s, p, fill):
    """render_frame for a float array (h, w): bicubic, through PIL's 32-bit float images."""
    return np.asarray(render_frame(Image.fromarray(a.astype(np.float32), 'F'), s, p, fill), np.float64)


def save(out, a, name):
    out_path = a.out or os.path.join(HERE, '..', 'art', name + '.png')
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    out.save(out_path, optimize=True)
    print('  -> %s %dx%d %s (%.0f kB)' % (os.path.basename(out_path), out.size[0], out.size[1], out.mode,
                                         os.path.getsize(out_path) / 1000))


def align(a):
    name = a.name
    s = spec(name)
    orig = load_original(name, a.img)
    cw, ch, cx, cy = s['canvas']
    canvas = on_canvas(name, orig)
    src = Image.open(a.redraw)
    bg = np.array([s['bg']] * 3)
    alpha = None
    if s['kind'] == 'colour':
        rgba = np.asarray(src.convert('RGBA'), np.float64)
        alpha = rgba[..., 3] / 255.0
        if alpha.min() > 0.99:
            alpha = None
            red = rgba[..., :3]
        else:  # fitted as it looks on the canvas colour
            red = rgba[..., :3] * alpha[..., None] + bg * (1 - alpha[..., None])
    else:
        red = np.asarray(src.convert('L').convert('RGB'), np.float64)
    rw, rh = red.shape[1], red.shape[0]
    # Fitted against the whole canvas (the margins included), in canvas coordinates.
    p0 = (cw / rw, ch / rh, 0.0, 0.0)
    p, res, res0 = fit_picture(canvas, bg, red, p0, a.range or 12, a.uniform)
    print('%s: redraw %dx%d%s of a %dx%d canvas (frame at %d, %d)'
          % (name, rw, rh, ' with alpha' if alpha is not None else '', cw, ch, cx, cy))
    report(name, p, res, res0, p0)
    if s['kind'] == 'mask':
        align_mask(a, s, canvas, red, p)
    elif s['kind'] == 'colour':
        align_colour(a, s, canvas, red, alpha, p)
    else:
        align_shade(a, s, canvas, red, p)


def align_shade(a, s, canvas, red, p):
    bg = np.array([s['bg']] * 3)
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
    save(render_frame(img, s, p, (255, 255, 255)).convert('L'), a, a.name)


def mask_mismatch(canvas, red, p):
    """Where the redraw's 0.5 contour and the original's pixel edges disagree: the share of the original's ink
    pixels that one side has and the other has not, in the original's grid (redraw area-averaged)."""
    f = Fit(canvas, np.zeros(3), red)
    f.prepare(p[0])
    w = f.warp(p, ss=4)[..., 0] > 127.5
    o = canvas[..., 0] > 127.5
    return 100.0 * np.sum(w != o) / max(1, np.sum(o))


def align_mask(a, s, canvas, red, p):
    """A coverage mask: its levels stretched so that the background is 0 and the solid ink 255 (the remake
    thresholds it at 0.5)."""
    g = red[..., 0]
    lo, hi = np.percentile(g, 50), np.percentile(g, 99.5)  # the canvas is mostly background
    g = np.clip((g - lo) * 255.0 / max(hi - lo, 1.0), 0, 255)
    red = np.repeat(g[..., None], 3, axis=2)
    fm = Fit(canvas, np.zeros(3), red)
    fm.prepare(p[0])
    print('  levels %.1f..%.1f -> 0..255: | %.1f / %.1f / %.1f %% / %.2f |' % ((lo, hi) + fm.residual(p)))
    print('  0.5 contour mismatch: %.2f %% of the ink pixels' % mask_mismatch(canvas, red, p))
    out = np.clip(render_float(g, s, p, 0.0), 0, 255)
    save(Image.fromarray(np.round(out).astype(np.uint8), 'L'), a, a.name)


def body_match(a, rgb, alpha):
    """Gives the redraw's ink body the original's body colour 0x4b: per channel a piecewise-linear curve through
    0 -> 0, the redraw's body (the median of its opaque pixels) -> the original's body (the median of the original's
    ink pixels) and 255 -> 255, so the highlights stay light and the red rims red."""
    n = ink_map(a.img)
    ob = np.median(load_original('ink', a.img)[n > 0], axis=0)
    rb = np.median(rgb[alpha > 0.98], axis=0)
    out = np.empty_like(rgb)
    for k in range(3):
        out[..., k] = np.interp(rgb[..., k], [0.0, rb[k], 255.0], [0.0, ob[k], 255.0])
    print('  body colour (%.0f, %.0f, %.0f) -> the original\'s (%.0f, %.0f, %.0f)' % (tuple(rb) + tuple(ob)))
    return out


def align_colour(a, s, canvas, red, alpha, p):
    """A colour picture with alpha, written as RGBA covering the frame. Colour and alpha are resampled
    premultiplied, so that no background colour bleeds into the edges."""
    if alpha is None:
        sys.exit('%s: the redraw has no alpha channel' % a.name)
    bg = s['bg']
    straight = np.where(alpha[..., None] > 0, (red - bg * (1 - alpha[..., None])) / np.maximum(alpha[..., None], 1e-6), 0)
    if a.match:
        straight = body_match(a, straight, alpha)
        fm = Fit(canvas, np.array([bg] * 3), straight * alpha[..., None] + bg * (1 - alpha[..., None]))
        fm.prepare(p[0])
        print('  after the colour match: | %.1f / %.1f / %.1f %% / %.2f |' % fm.residual(p))
    rgb = straight * alpha[..., None]  # premultiplied colour
    A = np.clip(render_float(alpha, s, p, 0.0), 0, 1)
    P = np.stack([render_float(rgb[..., k], s, p, 0.0) for k in range(3)], axis=2)
    C = np.clip(P / np.maximum(A[..., None], 1e-3), 0, 255)
    C[A < 1 / 255.0] = 0
    out = np.dstack([C, A * 255.0])
    save(Image.fromarray(np.round(out).astype(np.uint8), 'RGBA'), a, a.name)


def upload(a):
    """The picture to give the image generator: nearest-neighbour, on its paper-white canvas."""
    name = a.name
    s = spec(name)
    c = on_canvas(name, load_original(name, a.img))
    fx, fy = s['up']
    img = Image.fromarray(np.round(c).astype(np.uint8))
    if s['kind'] != 'colour':
        img = img.convert('L')
    ch, cw = c.shape[:2]
    img.resize((cw * fx, ch * fy), Image.NEAREST).save(a.redraw)
    print('%s: %dx%d' % (a.redraw, cw * fx, ch * fy))


def sheet(a):
    """Per picture: the original (nearest-neighbour), the redraw (the colour picture over white), and an overlay.
    Sketch: the original's strokes in red over the redraw in grey (where they agree the strokes look dark red).
    Text and ink: the original's ink pixels in red, the redraw's coverage in cyan (where they agree: white)."""
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
            if s['kind'] == 'colour':
                im = Image.open(art).convert('RGBA').resize((pw, ph), Image.LANCZOS)
                r = np.asarray(im, np.float64)
                cov = r[..., 3] / 255.0
                shown = r[..., :3] * cov[..., None] + 255.0 * (1 - cov[..., None])
                o = (ink_map(a.img) > 0).astype(np.uint8) * 255
            else:
                g = np.asarray(Image.open(art).convert('L').resize((pw, ph), Image.LANCZOS), np.float64)
                shown, cov = np.repeat(g[..., None], 3, axis=2), g / 255.0
                o = np.round(orig[..., 0]).astype(np.uint8)
            panels.append((Image.fromarray(np.round(np.clip(shown, 0, 255)).astype(np.uint8)),
                           '%s: redraw (%s)' % (name, os.path.basename(art))))
            if s['kind'] == 'shade':
                o = np.asarray(Image.fromarray(o).resize((pw, ph), Image.BILINEAR), np.float64) / 255.0
                ov = np.stack([cov, cov * o, cov * o], axis=2)  # the original's darkness removes green and blue
                label = 'overlay (original in red over the redraw)'
            else:
                o = np.asarray(Image.fromarray(o).resize((pw, ph), Image.NEAREST), np.float64) / 255.0
                ov = np.stack([o, cov, cov], axis=2)
                label = 'overlay (original red, redraw cyan, both white)'
            panels.append((Image.fromarray(np.round(np.clip(ov, 0, 1) * 255).astype(np.uint8)),
                           '%s: %s' % (name, label)))
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
    ap.add_argument('more', nargs='*')
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
        a.names = [x for x in [a.name, a.redraw] + a.more if x]
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

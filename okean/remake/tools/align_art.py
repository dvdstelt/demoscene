"""The redrawn pictures of The Sea Robot of Love: the uploads for the image generator, and the fit of a redraw onto
the original, written to art/NAME.png.

    python3 tools/align_art.py --upload NAME OUT.png         the nearest-neighbour picture to upload
    python3 tools/align_art.py NAME REDRAW.png                fit and write art/NAME.png
    python3 tools/align_art.py --sheet OUT.jpg NAME [NAME..]  comparison sheet: original | redraw
(inside the toolbox: numpy + PIL; reads okean/re/unpacked/korso2.bin, made by okean/re/unpack.py). The fitting is
clone/remake/tools/align_art.py's (DOPE's and 303's before it).

The pictures (okean/re/notes-core.md section 6, notes-title-objects.md):

- title_mask: "the sea ROBOT of love", the 1-bit 320x200 mask at code32 0x1502c that the title's light beams skip.
  White letters on black. Uploaded at 5 x 6 per pixel (1600 x 1200: the screen at 4:3); fitted as a whole, then per
  element (the three lines, then the words of each line, split at gaps of 3 or more empty columns), each element
  written exactly into its rectangle. Written as grey (the remake reads it as coverage).
- font: der piipo's font, the 285 x 38 sheet at 0x3cec2 (26 glyphs a..z, widths at 0x4c8c2): body (index 216)
  white, single edge pixels (254) grey 33/63, the 1-pixel outline (7) pure red, on black. The glyphs are uploaded
  in a grid of three rows (a..i, j..r, s..z) with gaps, on black, at 5 x 6 per pixel; fitted as a whole, then per
  glyph (its cell), and written back into the sheet's layout: art/font.png covers exactly the 285 x 38 sheet,
  RGB on black (the remake takes alpha = max(R, G, B), body = green, outline = red minus green).

The fit maps redraw pixel coordinates (x, y) to original pixel coordinates (u, v): u = sx * x + tx,
v = sy * y + ty; it prints the scale, offset and residual (RMS 0..255 in the original's grid, plain and with a
1-pixel blur, the share of pixels where one picture is background and the other is not, and the correlation of the
two pictures' detail).
"""
import argparse
import os
import struct

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, '..', '..', 're', 'unpacked', 'korso2.bin')
ART = os.path.join(HERE, '..', 'art')
SCREEN_ASPECT = 1.2   # an original pixel is 1.2 times as tall as wide on the 4:3 screen
BLACK = np.zeros(3)
UP = (5, 6)           # upload: nearest-neighbour factors (display shape)

# font grid of the upload: rows of glyphs, gap between glyphs and around them (original pixels)
FONT_ROWS = ['abcdefghi', 'jklmnopqr', 'stuvwxyz']
FONT_GAP_X, FONT_GAP_Y, FONT_MARGIN = 9, 12, 6
FONT_CANVAS = (180, 150)


def code32(a, n):
    d = open(IMG, 'rb').read()
    return d[0xec0 + a:0xec0 + a + n]


def font_widths():
    return list(code32(0x4c8c2, 26))


def font_sheet():
    """The 285 x 38 sheet as RGB 0..255 (body white, edge grey, outline red, background black)."""
    raw = np.frombuffer(code32(0x3cec2, 320 * 38), np.uint8).reshape(38, 320)[:, :285]
    out = np.zeros((38, 285, 3))
    out[raw == 216] = 255
    out[raw == 254] = 33 * 255 / 63
    out[raw == 7] = (255, 0, 0)
    return out


def title_mask():
    raw = np.frombuffer(code32(0x1502c, 64000), np.uint8).reshape(200, 320)
    out = np.zeros((200, 320, 3))
    out[raw != 0] = 255
    return out


def font_layout():
    """Glyph k -> (sheet x, width, canvas x, canvas y) of the upload grid."""
    w = font_widths()
    sx = np.concatenate([[0], np.cumsum(w)[:-1]])
    cells = {}
    for r, row in enumerate(FONT_ROWS):
        width = sum(w[ord(ch) - 97] for ch in row) + FONT_GAP_X * (len(row) - 1)
        x = (FONT_CANVAS[0] - width) // 2
        y = FONT_MARGIN + r * (38 + FONT_GAP_Y)
        for ch in row:
            k = ord(ch) - 97
            cells[k] = (int(sx[k]), w[k], x, y)
            x += w[k] + FONT_GAP_X
    return cells


def font_canvas():
    """The upload grid in original pixels (FONT_CANVAS), RGB."""
    sheet = font_sheet()
    out = np.zeros((FONT_CANVAS[1], FONT_CANVAS[0], 3))
    for k, (sx, w, x, y) in font_layout().items():
        out[y:y + 38, x:x + w] = sheet[:, sx:sx + w]
    return out


def original(name):
    return {'title_mask': title_mask, 'font': font_canvas}[name]()


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


def render(img, size, p, scale_uv, fill, origin=(0.0, 0.0)):
    """Resamples img (PIL) so that output pixel (X, Y) shows original coordinates (u0 + X * su, v0 + Y * sv)."""
    sx, sy, tx, ty = p
    su, sv = scale_uv
    u0, v0 = origin
    return img.transform(size, Image.AFFINE, (su / sx, 0, (u0 - tx) / sx, 0, sv / sy, (v0 - ty) / sy),
                         resample=Image.BICUBIC, fillcolor=fill)




def fit_picture(orig, bg, red, p0, rng, uniform):
    fit = Fit(orig, bg, red)
    p = refine(fit, coarse_search(fit, p0, rng, uniform), uniform)
    return p, fit.residual(p), fit.residual(p0)


def report(p, res, res0, p0):
    sx, sy, tx, ty = p
    print('  scale %.4f x %.4f of canvas-filling (sx %.5f, sy %.5f original px per redraw px, pixel aspect %.4f), '
          'offset (%.2f, %.2f) original px' % (sx / p0[0], sy / p0[1], sx, sy, sy * SCREEN_ASPECT / sx, tx, ty))
    print('  residual: RMS %.1f, blurred 1 px %.1f, background mismatch %.2f %%, detail correlation %.3f   '
          '(redraw just stretched to the canvas: %.1f, %.1f, %.2f %%, %.3f)' % (res + res0))


def spans(profile, gap):
    """Runs of non-empty entries of a 1D profile, split at gaps of >= gap empty entries: [(a, b)]."""
    out, a, empty = [], None, 0
    for i, v in enumerate(list(profile) + [0] * gap):
        if v:
            if a is None:
                a = i
            empty = 0
        elif a is not None:
            empty += 1
            if empty >= gap:
                out.append((a, i - empty + 1))
                a, empty = None, 0
    return out


def title_elements(orig):
    """The lines (split at empty rows), then the words of each line (gaps of >= 3 empty columns)."""
    lum = orig.mean(axis=2) > 0
    els = []
    for y0, y1 in spans(lum.any(axis=1), 2):
        for x0, x1 in spans(lum[y0:y1].any(axis=0), 3):
            els.append((x0, y0, x1, y1))
    return els


def element_rects(els, W, H):
    """Rectangles around the elements that meet in the middle of the gaps (and reach the frame's edges)."""
    rects = []
    for (x0, y0, x1, y1) in els:
        same = [e for e in els if not (e[3] <= y0 or e[1] >= y1)]           # elements on the same line
        left = max([e[2] for e in same if e[2] <= x0], default=None)
        right = min([e[0] for e in same if e[0] >= x1], default=None)
        above = max([e[3] for e in els if e[3] <= y0], default=None)
        below = min([e[1] for e in els if e[1] >= y1], default=None)
        rx0 = 0 if left is None else (left + x0) // 2
        rx1 = W if right is None else (x1 + right + 1) // 2
        ry0 = 0 if above is None else (above + y0) // 2
        ry1 = H if below is None else (y1 + below + 1) // 2
        rects.append((rx0, ry0, rx1, ry1))
    return rects


def load_redraw(path):
    return np.asarray(Image.open(path).convert('RGB'), float)


def align(a):
    name = a.name
    orig = original(name)
    H, W = orig.shape[:2]
    red = load_redraw(a.redraw)
    rh, rw = red.shape[:2]
    p0 = (W / rw, H / rh, 0.0, 0.0)         # the redraw stretched over the upload's canvas
    print('%s: redraw %dx%d' % (name, rw, rh))
    p, res, res0 = fit_picture(orig, BLACK, red, p0, a.range, False)
    report(p, res, res0, p0)
    img = Image.fromarray(np.clip(red, 0, 255).astype(np.uint8))
    if name == 'title_mask':
        rects = element_rects(title_elements(orig), W, H)
        scale = (1.0 / ((W / p[0]) / W), 1.0)   # output: the redraw's own pixel size
        ow = int(round(W / p[0]))
        oh = int(round(ow * H / W * SCREEN_ASPECT * W / H * H / W)) if False else int(round(H / p[1]))
        out = Image.new('RGB', (ow, oh))
        for r in rects:
            pe, rese, rese0 = fit_element(orig, BLACK, red, p, r)
            print('  element %s: RMS %.1f (whole fit %.1f), mismatch %.2f %%' % (r, rese[0], rese0[0], rese[2]))
            x0, y0, x1, y1 = r
            X0, Y0 = int(round(x0 * ow / W)), int(round(y0 * oh / H))
            X1, Y1 = int(round(x1 * ow / W)), int(round(y1 * oh / H))
            piece = render(img, (X1 - X0, Y1 - Y0), pe, (W / ow, H / oh), (0, 0, 0), (X0 * W / ow, Y0 * H / oh))
            out.paste(piece, (X0, Y0))
        out = out.convert('L')
    else:
        cells = font_layout()
        sheet_w = 285
        k_out = 1.0 / p[0]                      # redraw pixels per original pixel (horizontal)
        ow, oh = int(round(sheet_w * k_out)), int(round(38 / p[1]))
        out = Image.new('RGB', (ow, oh))
        for k in range(26):
            sx, w, x, y = cells[k]
            rect = (max(0, x - FONT_GAP_X // 2), max(0, y - FONT_GAP_Y // 2),
                    min(W, x + w + FONT_GAP_X // 2), min(H, y + 38 + FONT_GAP_Y // 2))
            pe, rese, rese0 = fit_element(orig, BLACK, red, p, rect)
            print('  glyph %s: RMS %.1f (whole fit %.1f), mismatch %.2f %%' % (chr(97 + k), rese[0], rese0[0], rese[2]))
            X0, X1 = int(round(sx * ow / sheet_w)), int(round((sx + w) * ow / sheet_w))
            piece = render(img, (X1 - X0, oh), pe, (sheet_w / ow * 1.0, 38 / oh), (0, 0, 0),
                           (x + (X0 * sheet_w / ow - sx), float(y)))
            out.paste(piece, (X0, 0))
    os.makedirs(ART, exist_ok=True)
    path = a.out or os.path.join(ART, name + '.png')
    out.save(path)
    print('wrote %s (%dx%d)' % (path, out.size[0], out.size[1]))


def upload(a):
    orig = original(a.name)
    H, W = orig.shape[:2]
    big = np.repeat(np.repeat(orig, UP[1], axis=0), UP[0], axis=1)
    Image.fromarray(big.astype(np.uint8)).save(a.out)
    print('wrote %s (%dx%d)' % (a.out, big.shape[1], big.shape[0]))


def sheet(a):
    rows = []
    for name in a.names:
        orig = original(name) if name == 'title_mask' else font_sheet()
        H, W = orig.shape[:2]
        pw = 640
        ph = int(round(pw * H / W * SCREEN_ASPECT))
        left = Image.fromarray(orig.astype(np.uint8)).resize((pw, ph), Image.NEAREST)
        right = Image.new('RGB', (pw, ph))
        path = os.path.join(ART, name + '.png')
        if os.path.exists(path):
            right = Image.open(path).convert('RGB').resize((pw, ph), Image.LANCZOS)
        row = Image.new('RGB', (2 * pw, ph))
        row.paste(left, (0, 0))
        row.paste(right, (pw, 0))
        rows.append(row)
    out = Image.new('RGB', (max(r.size[0] for r in rows), sum(r.size[1] for r in rows)))
    y = 0
    for r in rows:
        out.paste(r, (0, y))
        y += r.size[1]
    out.save(a.out, quality=92)
    print('wrote', a.out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--upload', nargs=2, metavar=('NAME', 'OUT'))
    ap.add_argument('--sheet', nargs='+', metavar='OUT NAME')
    ap.add_argument('name', nargs='?')
    ap.add_argument('redraw', nargs='?')
    ap.add_argument('--range', type=float, default=12)
    ap.add_argument('--out')
    a = ap.parse_args()
    if a.upload:
        a.name, a.out = a.upload
        upload(a)
    elif a.sheet:
        a.out, a.names = a.sheet[0], a.sheet[1:]
        sheet(a)
    else:
        align(a)


if __name__ == '__main__':
    main()

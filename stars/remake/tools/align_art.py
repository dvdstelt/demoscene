"""Fits a high-resolution redraw of one of STARS' pictures onto the original and writes the aligned picture.

    python3 tools/align_art.py NN REDRAW.png [--out art/fileNN.png]     (run inside the toolbox: numpy + PIL)
    python3 tools/align_art.py --upload NN OUT.png                     the nearest-neighbour picture to upload
    python3 tools/align_art.py --sheet OUT.jpg NN [NN ...]             comparison sheet of art/fileNN.* vs originals

The original is stars/re/dump/pictures/fileNN.png (written by stars/re/models/core/pictures.py: the file's pixels
with the palette the demo shows them with). Screen pictures are shown at 4:3, so one original pixel is 1.2 times as
tall as wide; file 7 is a texture and has square texels. The redraw has square pixels.

The fit maps redraw pixel coordinates (x, y) to original pixel coordinates (u, v):
    u = sx * x + tx,   v = sy * y + ty
A redraw that has the original's framing and fills its frame has sx = W / width, sy = H / height and no offset. The
fit compares both pictures in the original's grid (the redraw area-averaged into it, both blurred so that detail
the original does not have does not count): a brute-force search over scale and offset on a coarse grid, then
Nelder-Mead on (scale, aspect, tx, ty) with less and less blur. It prints the scale and offset and the residual
(RMS of the RGB difference 0..255 in the original's grid, plain and with a 1-pixel blur, the share of pixels where
one picture is background and the other is not, and the correlation of the two pictures' detail, see
`Fit.residual`), and writes the redraw resampled to cover exactly the
original's frame at the redraw's own pixel size (output width = W / sx, height from the display aspect); where the
redraw does not reach, it pads with the original's background colour.

Sprites (files 5 and 6: four words of 144x36 and four names of 128x74, stacked, colour 0 transparent; 32: "23806
faces", 80x36, indices >= 0x50 transparent) are uploaded in a 2x2 grid with gaps (see `layout`). Each sprite is
fitted on its own and the output stacks them again like the file, all at one scale, as RGBA. The sprites'
transparent pixels are uploaded as flat magenta, green for 32 (the lettering has black outlines, so black cannot
stand for transparency), and the redraw keeps it: alpha comes from the redraw's own alpha when it has one,
otherwise from keying out its key colour (see `add_alpha`), limited to the original's mask dilated by about 1.5
pixels.

Options:
  --out FILE        output (default ../art/fileNN.png next to this script; .jpg writes quality 95, 4:4:4)
  --orig FILE       the original as an indexed PNG (default ../../re/dump/pictures/fileNN.png)
  --uniform         keep the redraw's pixels square (one scale), instead of fitting x and y separately
  --range R         search offsets up to R original pixels and scales up to 1 +- R/100 (default 32; sprites 12)
  --auto-jpeg MB    write .jpg instead of .png when the PNG would be larger than MB megabytes (default: never)
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PICTURES = os.path.join(HERE, '..', '..', 're', 'dump', 'pictures')
SCREEN_ASPECT = 1.2  # an original screen pixel is 1.2 times as tall as wide on a 4:3 screen
TEXTURES = {7}  # square texels
# Sprite sheets: sprite width, height, count; the upload's display aspect (width / height).
SPRITES = {5: (144, 36, 4, 3.0), 6: (128, 74, 4, 1.5), 32: (80, 36, 1, 1.5)}
GAP = 8  # original pixels between the sprites of an upload grid and around it
# The flat colour that stands for transparency in the sprite uploads and redraws: magenta, but green for the
# mauve "23806 faces" (32).
KEYS = {32: (0, 255, 0)}


def key_colour(nn):
    return KEYS.get(nn, (255, 0, 255))


def pixel_aspect(nn):
    return 1.0 if nn in TEXTURES else SCREEN_ASPECT


def load_original(path, key=(255, 0, 255)):
    """RGB (h, w, 3) float, the background colour and the opaque mask (False where the PNG's tRNS makes an index
    transparent); transparent pixels get the key colour, which is then also the background."""
    im = Image.open(path)
    if im.mode != 'P':
        rgb = np.asarray(im.convert('RGB'), np.float64)
        return rgb, rgb[0, 0].copy(), np.ones(rgb.shape[:2], bool)
    idx = np.asarray(im)
    pal = np.array(im.getpalette()[:768] + [0] * (768 - len(im.getpalette()[:768])), np.float64).reshape(256, 3)
    trans = im.info.get('transparency')
    if isinstance(trans, int):
        opaque = idx != trans
    elif isinstance(trans, bytes):
        table = np.frombuffer(trans + b'\xff' * (256 - len(trans)), np.uint8)
        opaque = table[idx] > 0
    else:
        opaque = np.ones(idx.shape, bool)
    border = np.concatenate([idx[0], idx[-1], idx[:, 0], idx[:, -1]])
    bg = pal[np.bincount(border, minlength=256).argmax()]
    rgb = pal[idx]
    if not opaque.all():
        # Transparent sprite pixels are shown (and uploaded) as flat magenta, which the redraw keeps and the
        # alignment keys out; the lettering has opaque black / dark outlines, so black cannot be the key.
        rgb[~opaque] = key
        bg = np.array(key, np.float64)
    return rgb, bg, opaque


def layout(nn, rgb, opaque):
    """For a sprite sheet: the upload grid (RGB, opaque mask) and per sprite (x, y) of its cell in the grid."""
    sw, sh, n, aspect = SPRITES[nn]
    cols, rows = min(n, 2), (n + 1) // 2
    gw, gh = cols * sw + (cols + 1) * GAP, rows * sh + (rows + 1) * GAP
    # Pad to the requested display aspect (width / (height * 1.2)).
    if gw / (gh * SCREEN_ASPECT) < aspect:
        W, H = int(round(aspect * gh * SCREEN_ASPECT)), gh
    else:
        W, H = gw, int(round(gw / aspect / SCREEN_ASPECT))
    ox, oy = (W - gw) // 2, (H - gh) // 2
    grid = np.empty((H, W, 3))
    grid[:] = key_colour(nn)
    mask = np.zeros((H, W), bool)
    cells = []
    for k in range(n):
        x = ox + GAP + (k % 2) * (sw + GAP)
        y = oy + GAP + (k // 2) * (sh + GAP)
        grid[y:y + sh, x:x + sw] = rgb[k * sh:(k + 1) * sh]
        mask[y:y + sh, x:x + sw] = opaque[k * sh:(k + 1) * sh]
        cells.append((x, y))
    return grid, mask, cells


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
        # shape is where the original has it, whatever the colours; about 0 for unrelated pictures.
        lo, lw = self.orig.mean(axis=2, keepdims=True), warped.mean(axis=2, keepdims=True)
        do, dw = blur(lo, 2.0) - blur(lo, 12.0), blur(lw, 2.0) - blur(lw, 12.0)
        corr = float(np.corrcoef(do.ravel(), dw.ravel())[0, 1])
        return rms, rms1, float(np.mean(fg_o != fg_r)) * 100, corr


def coarse_search(fit, p0, rng, uniform):
    """Brute force over scale (and aspect) around p0 (scaled about the frame's centre) and offset, blurred."""
    W, H = fit.W, fit.H
    q = 4 if min(W, H) >= 100 else 2  # coarse cell in original pixels
    ob = box_down(blur(fit.orig, 0.75 * q), q)
    best = (np.inf, None)
    scales = 1 + np.arange(-rng, rng + 1, 2.5) / 100.0
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
    for sigma in (3.0, 1.5, 0.75):
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


def render(img, size, p, scale_uv, fill):
    """Resamples img (PIL) so that output pixel (X, Y) shows original coordinates (X * su, Y * sv) under fit p."""
    sx, sy, tx, ty = p
    su, sv = scale_uv
    return img.transform(size, Image.AFFINE, (su / sx, 0, -tx / sx, 0, sv / sy, -ty / sy), resample=Image.BICUBIC,
                         fillcolor=fill)


def save(img, path, quality=95):
    if path.lower().endswith(('.jpg', '.jpeg')):
        img.save(path, quality=quality, subsampling=0, optimize=True)
    else:
        img.save(path, optimize=True)


def report(nn, p, res, res0, W, aspect, rw, rh):
    sx, sy, tx, ty = p
    rel = sx / (W / rw)
    pa = sy * aspect / sx
    print('  scale %.4f of frame-filling (sx %.5f, sy %.5f original px per redraw px, pixel aspect %.4f), '
          'offset (%.2f, %.2f) original px' % (rel, sx, sy, pa, tx, ty))
    print('  residual: RMS %.1f, blurred 1 px %.1f, background mismatch %.2f %%, detail correlation %.3f   '
          '(redraw just stretched to the frame: %.1f, %.1f, %.2f %%, %.3f)' % (res + res0))
    print('  | %s | %.4f x %.4f | %+.1f, %+.1f | %.1f / %.1f / %.1f %% / %.2f |' % ((nn, rel, rel * pa, tx, ty) + res))


def align_sprites(a, nn, rgb, opaque, red_img):
    sw, sh, n, _ = SPRITES[nn]
    grid, _, cells = layout(nn, rgb, opaque)
    GH, GW = grid.shape[:2]
    has_alpha = red_img.mode in ('RGBA', 'LA', 'PA') and np.asarray(red_img.convert('RGBA'))[..., 3].min() < 255
    rgba = red_img.convert('RGBA')
    ra = np.asarray(rgba, np.float64)
    kc = np.array(key_colour(nn), np.float64)
    red = ra[..., :3] * ra[..., 3:] / 255.0 + kc * (1 - ra[..., 3:] / 255.0)  # over the key colour
    rw, rh = red.shape[1], red.shape[0]
    print('file %02d: redraw %dx%d of the %dx%d grid%s' % (nn, rw, rh, GW, GH, ', with alpha' if has_alpha else ''))
    fits = []
    for k, (cx, cy) in enumerate(cells):
        sprite = rgb[k * sh:(k + 1) * sh]
        # Start from the grid fitting the redraw's frame: the sprite's cell is then at (cx, cy).
        p0 = (GW / rw, GH / rh, -cx, -cy)
        p, res, res0 = fit_picture(sprite, kc, red, p0, a.range or 12, a.uniform)
        print(' sprite %d:' % k)
        report('%02d.%d' % (nn, k), p, res, res0, sw, SCREEN_ASPECT, rw * sw / GW, rh)
        fits.append(p)
    k_out = float(np.median([1.0 / p[0] for p in fits]))  # output pixels per original pixel, horizontally
    ow = int(round(sw * k_out))
    oh = int(round(sh * SCREEN_ASPECT * k_out))
    sheet = Image.new('RGBA', (ow, oh * n), (0, 0, 0, 0))
    key = measure_key(red_img, kc)
    if not has_alpha:
        print('  key colour of the redraw: (%d, %d, %d)' % tuple(key))
    for k, p in enumerate(fits):
        src = rgba.convert('RGBa') if has_alpha else red_img.convert('RGB')
        fill = (0, 0, 0, 0) if has_alpha else tuple(int(c) for c in key)
        cell = render(src, (ow, oh), p, (sw / ow, sh / oh), fill)
        if has_alpha:
            cell = cell.convert('RGBA')
        else:
            cell = add_alpha(cell, opaque[k * sh:(k + 1) * sh], key)
        sheet.paste(cell, (0, k * oh))
    return sheet


def measure_key(img, kc):
    """The redraw's actual key colour: the median of the pixels near the pure one."""
    a = np.asarray(img.convert('RGB'), np.float64).reshape(-1, 3)
    near = a[np.abs(a - kc).max(axis=1) < 60]
    return np.median(near, axis=0) if len(near) else kc


def add_alpha(cell, mask, key):
    """Alpha for lettering redrawn on a flat key colour. The lettering's colours have (almost) none of the key's
    hue: greys for magenta, mauve for green. With c(P) = (P - mean(P)) . (K - mean(K)), the amount of key hue, a
    pixel alpha * colour + (1 - alpha) * K has c = (1 - alpha) * c(K) (plus c(colour) * alpha, which is about 0 or
    negative), so alpha = 1 - c(P) / c(K) whatever the letter's brightness (the outlines are black), and the colour
    is unmixed from the key. Alpha is limited to the original's mask dilated by about 1.5 pixels; fully transparent
    pixels get black."""
    ow, oh = cell.size
    m = Image.fromarray((mask * 255).astype(np.uint8)).resize((ow, oh), Image.BILINEAR)
    m = np.asarray(m, np.float64)[..., None] / 255.0
    k = ow / mask.shape[1]  # output pixels per original pixel
    near = np.clip(blur(m, 1.0 * k) * 4.0, 0, 1)
    rgb = np.asarray(cell.convert('RGB'), np.float64)
    hue = key - key.mean()
    c = ((rgb - rgb.mean(axis=2, keepdims=True)) * hue).sum(axis=2, keepdims=True)
    ck = float((hue * hue).sum())
    alpha = np.clip((1 - c / ck - 0.04) / 0.92, 0, 1) * near
    a = np.maximum(alpha, 1e-3)
    col = np.clip((rgb - (1 - a) * key) / a, 0, 255)
    col = np.where(alpha > 1e-3, col, 0)
    out = np.concatenate([col, alpha * 255], axis=2)
    return Image.fromarray(np.round(out).astype(np.uint8), 'RGBA')


def align(a):
    nn = int(a.nn)
    path = a.orig or os.path.join(PICTURES, 'file%02d.png' % nn)
    rgb, bg, opaque = load_original(path, key_colour(nn))
    red_img = Image.open(a.redraw)
    out_path = a.out or os.path.join(HERE, '..', 'art', 'file%02d.png' % nn)
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    if nn in SPRITES:
        out = align_sprites(a, nn, rgb, opaque, red_img)
    else:
        red_img = red_img.convert('RGB')
        red = np.asarray(red_img, np.float64)
        H, W = rgb.shape[:2]
        aspect = pixel_aspect(nn)
        rw, rh = red.shape[1], red.shape[0]
        p, res, res0 = fit_picture(rgb, bg, red, (W / rw, H / rh, 0.0, 0.0), a.range or 32, a.uniform)
        # Output: the original's frame at the redraw's own horizontal pixel size.
        ow = int(round(W / p[0]))
        oh = int(round(ow * H * aspect / W))
        out = render(red_img, (ow, oh), p, (W / ow, H / oh), tuple(int(c) for c in bg))
        print('file %02d: redraw %dx%d' % (nn, rw, rh))
        report('%02d' % nn, p, res, res0, W, aspect, rw, rh)
    if a.auto_jpeg and out_path.lower().endswith('.png') and out.mode == 'RGB':
        save(out, out_path)
        if os.path.getsize(out_path) > a.auto_jpeg * 1e6:
            os.remove(out_path)
            out_path = out_path[:-4] + '.jpg'
            save(out, out_path)
    else:
        save(out, out_path)
    print('  -> %s %dx%d %s (%.0f kB)' % (os.path.basename(out_path), out.size[0], out.size[1], out.mode,
                                         os.path.getsize(out_path) / 1000))


def upload(a):
    """The picture to give the image generator: nearest-neighbour, 5 x 6 per screen pixel (4:3), 4 x 4 per texel,
    multiplied when that is less than 1000 pixels wide."""
    nn = int(a.nn)
    rgb, _, opaque = load_original(a.orig or os.path.join(PICTURES, 'file%02d.png' % nn), key_colour(nn))
    if nn in SPRITES:
        rgb, _, _ = layout(nn, rgb, opaque)
    fx, fy = (4, 4) if nn in TEXTURES else (5, 6)
    m = max(1, -(-1000 // (rgb.shape[1] * fx)))  # at least 1000 pixels wide
    fx, fy = fx * m, fy * m
    img = Image.fromarray(np.round(rgb).astype(np.uint8))
    img.resize((img.size[0] * fx, img.size[1] * fy), Image.NEAREST).save(a.redraw)
    print('%s: %dx%d' % (a.redraw, img.size[0] * fx, img.size[1] * fy))


def sheet(a):
    pw = 800
    try:
        font = ImageFont.truetype('DejaVuSans-Bold.ttf', 18)
    except OSError:
        font = ImageFont.load_default()
    rows = []
    for nn in a.nums:
        nn = int(nn)
        rgb, _, opaque = load_original(os.path.join(PICTURES, 'file%02d.png' % nn))
        H, W = rgb.shape[:2]
        ph = int(round(pw * H * pixel_aspect(nn) / W))
        if ph > 1000:  # tall sprite sheets: half width
            pw2, ph = pw // 2, int(round(pw / 2 * H * pixel_aspect(nn) / W))
        else:
            pw2 = pw
        checker = ((np.indices((ph, pw2)) // 16).sum(axis=0) % 2 * 40 + 30).astype(np.uint8)
        bgimg = Image.fromarray(np.dstack([checker] * 3))
        left = bgimg.copy()
        o = np.dstack([rgb, opaque * 255.0]).round().astype(np.uint8)
        left.paste(Image.fromarray(o, 'RGBA').resize((pw2, ph), Image.NEAREST),
                   (0, 0), Image.fromarray(o, 'RGBA').resize((pw2, ph), Image.NEAREST))
        art = None
        for ext in ('png', 'jpg'):
            p = os.path.join(a.art, 'file%02d.%s' % (nn, ext))
            if os.path.exists(p):
                art = p
        row = Image.new('RGB', (2 * pw, ph), (0, 0, 0))
        row.paste(left, (0, 0))
        d = ImageDraw.Draw(row)
        if art:
            r = Image.open(art).convert('RGBA').resize((pw2, ph), Image.LANCZOS)
            right = bgimg.copy()
            right.paste(r, (0, 0), r)
            row.paste(right, (pw, 0))
            label = 'file %02d: original | redraw (%s)' % (nn, os.path.basename(art))
        else:
            row.paste(left, (pw, 0))
            label = 'file %02d: original | no redraw, keeps the original' % nn
        d.line([(pw, 0), (pw, ph)], fill=(80, 80, 80), width=2)
        d.text((10, 8), label, fill=(255, 255, 0), font=font)
        rows.append(row)
    out = Image.new('RGB', (2 * pw, sum(r.size[1] for r in rows)))
    y = 0
    for r in rows:
        out.paste(r, (0, y))
        y += r.size[1]
    out.save(a.sheet, quality=88)
    print('%s: %d rows, %dx%d' % (a.sheet, len(rows), out.size[0], out.size[1]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('nn', nargs='?')
    ap.add_argument('redraw', nargs='?')
    ap.add_argument('nums', nargs='*')
    ap.add_argument('--out')
    ap.add_argument('--orig')
    ap.add_argument('--art', default=os.path.join(HERE, '..', 'art'))
    ap.add_argument('--uniform', action='store_true')
    ap.add_argument('--range', type=float, default=0)
    ap.add_argument('--auto-jpeg', type=float, default=0)
    ap.add_argument('--sheet')
    ap.add_argument('--upload', action='store_true')
    a = ap.parse_args()
    if a.sheet:
        a.nums = [x for x in [a.nn, a.redraw] + a.nums if x]
        sheet(a)
    elif a.upload and a.nn and a.redraw:
        upload(a)
    elif a.nn and a.redraw:
        align(a)
    else:
        ap.print_help()
        sys.exit(1)


if __name__ == '__main__':
    main()

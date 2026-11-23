"""Upscales Inside's textures, level maps and the intro's photo 4x and writes art/tex_NAME.jpg (see art/README.md).

    python tools/upscale_art.py MODEL.pth [NAME ..]      (all of TEXTURES below when no name is given)

These pictures are not drawn as they are: the parts read them by palette index through shade, fog and blend tables,
map them on 3D objects, wrap them and warp them. A redraw would move their pixels; an upscaler keeps every pixel
where it is (output pixel 4x + k comes from the input around pixel x), so the remake can still read the original's
indices and tables and add only the upscale's detail (src/inside/engine_gl.cpp, and the intro's, the twirl's and
the sprites' warp shaders). Each picture is decoded in the palette
the demo shows it with, padded with its own wrapped (tiling textures) or mirrored edges so its borders upscale like
its inside, upscaled, cropped back and written as JPEG (quality 95, no chroma subsampling). Level maps are written
as grey, 255 * level / max.

The model is Real-ESRGAN's realesr-general-x4v3 (BSD-3-Clause, https://github.com/xinntao/Real-ESRGAN):
https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-x4v3.pth
(sha256 8dc7edb9ac80ccdc30c3a5dca6616509367f05fbc184ad95b731f05bece96292). It runs on the CPU in about a second per
256 x 256 texture. Needs PyTorch, spandrel, numpy and PIL, for example in a virtual environment in the toolbox:
python3 -m venv VENV; VENV/bin/pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu;
VENV/bin/pip install spandrel numpy pillow. The original files must be in inside/original.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 're'))
import datfile  # noqa: E402

ORIGINAL = os.path.join(HERE, '..', '..', 'original', 'INSIDE.DAT')
ART = os.path.join(HERE, '..', 'art')
PAD = 16

# name: INSIDE.DAT file, the size the part gives it, its palette (None: a level map 0..levels), palette entry 0 set
# to black (as the parts' inits do), padding (wrap: the texture tiles; reflect: it does not)
TEXTURES = {
    # credits (notes-credits.md 4.1): the water mask over the tunnel, the tunnel, the ball's environment map
    'wtrmask': dict(file='credits/wtrmask.raw', w=320, h=200, pal=None, levels=31, pad='reflect'),
    'tunnel': dict(file='credits/tunnel.raw', w=256, h=1024, pal='credits/tunnel.pal', pad='wrap'),
    'piikkis': dict(file='credits/piikkis.raw', w=256, h=256, pal='credits/tunnel.pal', pad='wrap'),
    # logo (notes-intro-logo.md 6.2, 6.4): the 3D logo's texture, the lettering's dark mask (levels 0..15)
    'twlogo': dict(file='logo/twlogo.raw', w=256, h=256, pal='logo/logo.pal', pad='wrap'),
    'inmask': dict(file='logo/inmask.raw', w=253, h=147, pal=None, levels=15, pad='reflect'),
    # sprites (notes-sprites.md 2.3) and twirl (notes-twirl.md 9): the warp and room backgrounds
    'nigga1': dict(file='nigga/nigga1.raw', w=256, h=256, pal='nigga/nigga1.pal', pad='wrap'),
    'nigga2': dict(file='nigga/nigga2.raw', w=256, h=256, pal='nigga/nigga2.pal', black0=True, pad='wrap'),
    'nigga4': dict(file='nigga/nigga4.raw', w=256, h=256, pal='nigga/nigga4.pal', black0=True, pad='wrap'),
    'nigga9': dict(file='nigga/nigga9.raw', w=256, h=256, pal='nigga/nigga9.pal', black0=True, pad='wrap'),
    # flower (notes-flower.md 10) and twirl: the tube's texture and the flower's environment map
    'jeesus1': dict(file='flower/jeesus1.raw', w=256, h=256, pal='flower/jeesus.pal', pad='wrap'),
    'jeesus2': dict(file='flower/jeesus2.raw', w=256, h=256, pal='flower/jeesus.pal', pad='wrap'),
    # temple (notes-temple.md 10) and water (notes-water.md 8): the chrome spheres' environment maps
    'temple': dict(file='temple/temple.raw', w=256, h=256, pal='temple/temple.pal', pad='wrap'),
    'waterenv': dict(file='water/waterenv.raw', w=256, h=256, pal='water/water.pal', pad='wrap'),
    # intro (notes-intro-logo.md 2): the photo collage the light reveals
    'intro': dict(file='intro/intro.raw', w=320, h=200, pal='intro/intro.pal', pad='reflect'),
}


def decode(dat, t):
    px = np.frombuffer(dat.get(t['file']), np.uint8)[:t['w'] * t['h']].reshape(t['h'], t['w'])
    if t['pal'] is None:
        g = np.clip(px.astype(np.float32) / t['levels'], 0, 1)
        return np.repeat(g[..., None], 3, axis=2)
    pal = np.minimum(np.frombuffer(dat.get(t['pal']), np.uint8).reshape(256, 3), 63).astype(np.float32) / 63.0
    if t.get('black0'):
        pal = pal.copy()
        pal[0] = 0
    return pal[px]


def upscale(model, scale, img, pad):
    import torch
    x = np.pad(img, ((PAD, PAD), (PAD, PAD), (0, 0)), mode=pad)
    t = torch.from_numpy(np.ascontiguousarray(x.transpose(2, 0, 1)))[None]
    with torch.inference_mode():
        y = model(t)[0].clamp(0, 1).numpy().transpose(1, 2, 0)
    return y[PAD * scale:-PAD * scale, PAD * scale:-PAD * scale]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('model')
    ap.add_argument('names', nargs='*')
    ap.add_argument('--art', default=ART)
    a = ap.parse_args()
    from spandrel import ModelLoader
    desc = ModelLoader().load_from_file(a.model).eval()
    dat = datfile.Dat(ORIGINAL)
    for name in a.names or TEXTURES:
        t = TEXTURES[name]
        y = upscale(desc.model, desc.scale, decode(dat, t), t['pad'])
        if y.shape[:2] != (t['h'] * desc.scale, t['w'] * desc.scale):
            sys.exit('%s: the upscale is %dx%d' % (name, y.shape[1], y.shape[0]))
        out = np.round(y * 255).astype(np.uint8)
        img = Image.fromarray(out[..., 0], 'L') if t['pal'] is None else Image.fromarray(out, 'RGB')
        path = os.path.join(a.art, 'tex_%s.jpg' % name)
        img.save(path, quality=95, subsampling=0, optimize=True)
        print('%s: %dx%d -> %s (%.0f kB)' % (name, img.size[0], img.size[1], os.path.relpath(path),
                                             os.path.getsize(path) / 1000), flush=True)


if __name__ == '__main__':
    main()

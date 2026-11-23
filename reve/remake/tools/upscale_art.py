"""Upscales GFX\\TEXTURE.RAW 4x and writes art/tex_texture.jpg (see art/README.md).

    python tools/upscale_art.py MODEL.pth [--art DIR]

TEXTURE.RAW is a 256 x 256 sheet of hand-drawn 64 x 64 tiles. The parts do not show it as it is: the 3D engine
reads each tile by palette index through GOURAUD.PAL's 16 shade levels and wraps it over the faces
(src/reve/engine_gl.cpp), and the rotozoom layers of WIZ and the dot spirals and DADA's planes wrap single tiles and
16 x 16 blocks of it (src/scenes/roto.h, planes.h). A redraw would move its pixels; an upscaler keeps every pixel
where it is (output pixel 4x + k comes from the input around pixel x), so the remake still reads the original's
indices and tables and adds only the upscale's detail.

Each tile is decoded through TEXTURE.RAW's palette (the one every part shows it with: WIZ.RAW's is the same, and
DADA.RAW's matches on the tile DADA uses), padded with its own wrapped edges so its borders upscale like its inside,
upscaled, cropped back and pasted at its place in the sheet, which is written as JPEG (quality 95, no chroma
subsampling). The tile at (0, 64) is not drawn: it holds 16 x 16 blocks of coloured noise (the rotozoom layers of
WIZ and the dot spirals wrap two of them), which the upscaler turns into blobs, so it is written as the original
resized smoothly (bicubic, each block wrapped on its own), which adds no detail.

The model is Real-ESRGAN's realesr-general-x4v3 (BSD-3-Clause, https://github.com/xinntao/Real-ESRGAN):
https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-x4v3.pth
(sha256 8dc7edb9ac80ccdc30c3a5dca6616509367f05fbc184ad95b731f05bece96292). It runs on the CPU in a few seconds.
Needs PyTorch, spandrel, numpy and PIL, for example in a virtual environment in the toolbox:
python3 -m venv VENV; VENV/bin/pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu;
VENV/bin/pip install spandrel numpy pillow. The original must be in reve/original/REVE.EXE.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 're'))
import unpack  # noqa: E402  (reve/re/unpack.py: the archive and its LZW)

ORIGINAL = os.path.join(HERE, '..', '..', 'original', 'REVE.EXE')
ART = os.path.join(HERE, '..', 'art')
PAD = 16
TILE = 64
NOISE = (0, 64)   # the tile of 16 x 16 noise blocks: not upscaled
BLOCK = 16


def texture():
    exe = open(ORIGINAL, 'rb').read()
    r = {f['name']: f for f in unpack.parse_archive(exe)[2]}['GFX\\TEXTURE.RAW']
    d, _ = unpack.lzw(r['data'], r['size'])
    w, h = int.from_bytes(d[8:10], 'big'), int.from_bytes(d[10:12], 'big')
    pal = (np.frombuffer(d[32:800], np.uint8).reshape(256, 3) >> 2).astype(np.float32) / 63.0
    return pal[np.frombuffer(d[800:800 + w * h], np.uint8).reshape(h, w)]


def upscale(model, img):
    import torch
    x = np.pad(img, ((PAD, PAD), (PAD, PAD), (0, 0)), mode='wrap')
    t = torch.from_numpy(np.ascontiguousarray(x.transpose(2, 0, 1)))[None]
    with torch.inference_mode():
        y = model(t)[0].clamp(0, 1).numpy().transpose(1, 2, 0)
    return y[PAD * 4:-PAD * 4, PAD * 4:-PAD * 4]


def smooth(img):
    """4x bicubic resize with wrapped edges: the original as a filtered read would show it, without new detail."""
    x = np.pad(img, ((PAD, PAD), (PAD, PAD), (0, 0)), mode='wrap')
    h, w = x.shape[:2]
    y = np.asarray(Image.fromarray(np.round(x * 255).astype(np.uint8)).resize((w * 4, h * 4), Image.BICUBIC))
    return y[PAD * 4:-PAD * 4, PAD * 4:-PAD * 4].astype(np.float32) / 255.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('model')
    ap.add_argument('--art', default=ART)
    a = ap.parse_args()
    from spandrel import ModelLoader
    desc = ModelLoader().load_from_file(a.model).eval()
    if desc.scale != 4:
        sys.exit('the model scales %dx, not 4x' % desc.scale)
    src = texture()
    h, w = src.shape[:2]
    out = np.zeros((h * 4, w * 4, 3), np.float32)
    for ty in range(0, h, TILE):
        for tx in range(0, w, TILE):
            if (tx, ty) != NOISE:
                tile = src[ty:ty + TILE, tx:tx + TILE]
                out[ty * 4:(ty + TILE) * 4, tx * 4:(tx + TILE) * 4] = upscale(desc.model, tile)
                continue
            for y in range(ty, ty + TILE, BLOCK):
                for x in range(tx, tx + TILE, BLOCK):
                    out[y * 4:(y + BLOCK) * 4, x * 4:(x + BLOCK) * 4] = smooth(src[y:y + BLOCK, x:x + BLOCK])
    img = Image.fromarray(np.round(out * 255).astype(np.uint8), 'RGB')
    path = os.path.join(a.art, 'tex_texture.jpg')
    img.save(path, quality=95, subsampling=0, optimize=True)
    print('texture: %dx%d -> %s (%.0f kB)' % (img.size[0], img.size[1], os.path.relpath(path),
                                             os.path.getsize(path) / 1000), flush=True)


if __name__ == '__main__':
    main()

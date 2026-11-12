"""Shared helpers for the parts3d-a models: capture frames, snapshots, overlay files, image dumps."""
import os
import pickle
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.abspath(os.path.join(HERE, '..', '..'))
IMG = open(os.path.join(RE, 'unpacked', 'stars_img.bin'), 'rb').read()
REC = 64000 + 768
# generated images and arrays (made from the original: they stay out of the repository)
import tempfile
OUT = os.environ.get('P3A_OUT', os.path.join(tempfile.gettempdir(), 'stars-parts3d-a'))
os.makedirs(OUT, exist_ok=True)


def out(name):
    return os.path.join(OUT, name)


def file(n):
    return open(os.path.join(RE, 'unpacked', 'files', f'{n:02d}.bin'), 'rb').read()


def frame(f, path=None):
    """(pixels 200x320 uint8, palette 256x3 uint8 6-bit) of capture record f."""
    path = path or os.path.join(RE, 'full.frames')
    with open(path, 'rb') as fh:
        fh.seek(32 + f * REC)
        b = fh.read(REC)
    return np.frombuffer(b[:64000], np.uint8).reshape(200, 320), np.frombuffer(b[64000:], np.uint8).reshape(256, 3)


def ticks():
    rows = {}
    for line in open(os.path.join(RE, 'full.ticks')):
        if line.startswith('#'):
            continue
        p = line.split()
        rows[int(p[0])] = p
    return rows


def snap(name):
    import unicorn  # noqa: F401  (the pickle refers to it)
    return pickle.load(open(os.path.join(RE, 'snaps', name), 'rb'))


def rd32(mem, a): return struct.unpack_from('<i', mem, a)[0]
def rd16(mem, a): return struct.unpack_from('<h', mem, a)[0]
def ru32(mem, a): return struct.unpack_from('<I', mem, a)[0]


def save_png(path, pix, pal):
    from PIL import Image
    rgb = (pal.astype(np.uint32) * 255 // 63).astype(np.uint8)[pix]
    Image.fromarray(rgb).save(path)


COS1023 = np.frombuffer(IMG[0x5599c:0x5599c + 8192], np.int16).astype(np.int64)   # engine table: trunc(1023 cos)

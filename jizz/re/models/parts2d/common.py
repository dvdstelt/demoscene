"""Shared helpers for the parts2d models: capture frames, snapshots, textures, image output."""
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, RE)
import rep  # noqa: E402

FRAMES = os.path.join(RE, 'full.frames')
REC = 128000


def frame565(t):
    """The captured visible page at the end of tick t, as uint16 [200,320] 5:6:5."""
    with open(FRAMES, 'rb') as f:
        f.seek(32 + t * REC)
        return np.frombuffer(f.read(REC), dtype='<u2').reshape(200, 320)


def rgb565(px):
    px = px.astype(np.uint32)
    r, g, b = (px >> 11) & 31, (px >> 5) & 63, px & 31
    return np.stack([r * 255 // 31, g * 255 // 63, b * 255 // 31], -1).astype(np.uint8)


def flips():
    out = []
    for line in open(os.path.join(RE, 'full.flips')):
        if line.startswith('#'): continue
        f = line.split()
        out.append((int(f[0]), int(f[1]), float(f[2]), int(f[3]), f[4]))
    return out


def ticks():
    out = {}
    for line in open(os.path.join(RE, 'full.ticks')):
        if line.startswith('#'): continue
        f = line.split()
        out[int(f[0])] = dict(pit=int(f[1]), time=float(f[2]), order=int(f[3]), row=int(f[4]), event=int(f[5]),
                              sync=int(f[6]), part=f[7], flips=int(f[8]), eip=int(f[9], 16))
    return out


def load_snap(t):
    import unicorn  # noqa: F401  (needed to unpickle)
    with open(os.path.join(RE, 'snaps', f't{t}.snap'), 'rb') as f:
        return pickle.load(f)


def u32(mem, a): return struct.unpack_from('<I', mem, a)[0]
def i32(mem, a): return struct.unpack_from('<i', mem, a)[0]


def tga(name):
    """dump/<name>.tga as planes [3,256,256] (R, G, B), rows top to bottom as stored in memory."""
    d = open(os.path.join(RE, 'dump', name + '.tga'), 'rb').read()
    a = np.frombuffer(d[18:18 + 196608], np.uint8).reshape(256, 256, 3)
    desc = d[17]
    if not (desc & 0x20): a = a[::-1]
    return np.stack([a[:, :, 2], a[:, :, 1], a[:, :, 0]])


def save(path, img, scale=1):
    from PIL import Image
    im = Image.fromarray(img)
    if scale != 1: im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    im.save(path)

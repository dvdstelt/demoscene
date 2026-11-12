"""Shared helpers for the parts2d-a models: image, overlay files, capture records, PNG output."""
import os
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, RE)
OUT = os.path.join(HERE, 'out')

IMG = open(os.path.join(RE, 'unpacked', 'stars_img.bin'), 'rb').read()


def rd32(a, mem=IMG): return struct.unpack_from('<I', mem, a)[0]
def rds32(a, mem=IMG): return struct.unpack_from('<i', mem, a)[0]
def rd16(a, mem=IMG): return struct.unpack_from('<H', mem, a)[0]


def sintab(mem=IMG):
    """The sine table at 0xa740: signed dwords, 0x10000 = 1.0, 4096 entries per turn (5120 stored: cos = +1024)."""
    return np.frombuffer(bytes(mem[0xa740:0xa740 + 0x5000]), dtype='<i4').astype(np.int64)


def ofile(n):
    return open(os.path.join(RE, 'unpacked', 'files', '%02d.bin' % n), 'rb').read()


class Frames:
    def __init__(self, path=os.path.join(RE, 'full.frames')):
        self.raw = np.memmap(path, dtype=np.uint8, mode='r')
        assert bytes(self.raw[:8]) == b'STARFRM1'
        w, h, bits, rec, hdr, _ = struct.unpack('<IIIIII', bytes(self.raw[8:32]))
        self.rec, self.hdr = rec, hdr
        self.n = (len(self.raw) - hdr) // rec

    def vga(self, f):
        r = self.raw[self.hdr + f * self.rec:self.hdr + f * self.rec + 64000]
        return np.asarray(r).reshape(200, 320)

    def pal(self, f):
        r = self.raw[self.hdr + f * self.rec + 64000:self.hdr + (f + 1) * self.rec]
        return np.asarray(r).reshape(256, 3)


def read_ticks(path=os.path.join(RE, 'full.ticks')):
    rows = []
    for line in open(path):
        if line.startswith('#'): continue
        f = line.split()
        rows.append(dict(frame=int(f[0]), time=float(f[1]), pit=int(f[2]), gus=int(f[3]), order=int(f[4]),
                         row=int(f[5]), part=f[6], cb=f[7], drawn=int(f[8]), eip=f[9]))
    return rows


def rgb(idx, pal6):
    pal = (np.asarray(pal6).reshape(256, 3).astype(np.uint16) * 255 // 63).astype(np.uint8)
    return pal[np.asarray(idx)]


def save(name, arr, scale=1):
    im = Image.fromarray(np.asarray(arr).astype(np.uint8))
    if scale != 1:
        im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    os.makedirs(OUT, exist_ok=True)
    p = os.path.join(OUT, name)
    im.save(p)
    return p


def load_snap(path):
    import pickle
    import unicorn  # noqa: F401  (needed to unpickle)
    with open(path, 'rb') as fh:
        return pickle.load(fh)

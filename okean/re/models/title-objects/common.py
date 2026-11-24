"""Shared helpers for the title-objects models: the image, snapshots, the capture."""
import os, sys, pickle, struct
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, RE)
import rep
from sheet import open_frames, read_ticks

IMG = open(os.path.join(RE, 'unpacked', 'korso2.bin'), 'rb').read()
C32 = 0x8fc0

def frd(addr, n):
    """bytes of the file at a code32 address"""
    return IMG[0xec0 + addr:0xec0 + addr + n]

def fi32(addr, n=1):
    a = np.frombuffer(frd(addr, 4 * n), '<i4')
    return a if n > 1 else int(a[0])

_snaps = {}
def snap(name):
    if name not in _snaps:
        import unicorn  # noqa: needed to unpickle
        with open(os.path.join(RE, 'snaps', 'part_%s.snap' % name), 'rb') as f:
            _snaps[name] = pickle.load(f)
    return _snaps[name]

def mem(s, addr, n):
    m = s['mem']
    return bytes(m[C32 + addr:C32 + addr + n])

def mi32(s, addr, n=1):
    a = np.frombuffer(mem(s, addr, 4 * n), '<i4')
    return a if n > 1 else int(a[0])

_cap = None
def cap():
    global _cap
    if _cap is None:
        _cap = open_frames(os.path.join(RE, 'full.frames'))
    return _cap

def cap_frame(k):
    """(indices 200x320 uint8, palette 256x3 6-bit) of capture record k"""
    raw, W, H, rec, hdr, n = cap()
    o = hdr + k * rec
    pal = np.frombuffer(bytes(raw[o + 8:o + 776]), np.uint8).reshape(256, 3)
    idx = np.asarray(raw[o + 776:o + 776 + W * H]).reshape(H, W)[:200, :320].copy()
    return idx, pal

def ticks():
    rows = []
    for line in open(os.path.join(RE, 'full.ticks')):
        if line.startswith('#'):
            continue
        rows.append(line.split())
    return rows

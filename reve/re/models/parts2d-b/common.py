"""Shared helpers for the parts2d-b models: the image (rep.py), the archive's RAW pictures, the capture.

Run everything inside the toolbox from reve/re:  toolbox run -c acme303 python3 models/parts2d-b/<model>.py ...
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))          # reve/re
sys.path.insert(0, RE)

import numpy as np                                    # noqa: E402

import rep                                            # noqa: E402
from sheet import open_frames, indexed, read_ticks    # noqa: E402,F401

GFX = os.path.join(RE, 'unpacked', 'files', 'GFX')
FULL = os.path.join(RE, 'full')
DUMP = os.path.join(RE, 'dump', 'pictures')


def raw(name):
    """GFX\\<name>.RAW (Alchemy 'mhwanh'): (pixels as (h, w) uint8, 8-bit palette (256, 3) uint8)."""
    b = open(os.path.join(GFX, name.upper() + '.RAW'), 'rb').read()
    assert b[:6] == b'mhwanh'
    w, h, n = struct.unpack('>HHH', b[8:14])
    pal = np.frombuffer(b[32:32 + 3 * n], np.uint8).reshape(n, 3)
    px = np.frombuffer(b[32 + 3 * n:32 + 3 * n + w * h], np.uint8).reshape(h, w)
    return px, pal


def pal6(pal8):
    """The DAC values the loader 0x25a1fb writes: v >> 2 (6 bits)."""
    return (pal8 >> 2).astype(np.uint8)


def to_rgb(px, pal6_):
    """Indices through a 6-bit palette as the DAC shows them (v * 255 / 63, as sheet.rgb)."""
    lut = (pal6_.astype(np.uint32) * 255 + 31) // 63
    return lut[px].astype(np.uint8)


def sine():
    """The 4096-entry int16 sine table at 0x2176c8 (amplitude 32767)."""
    return np.frombuffer(rep.rd(0x2176c8, 8192), '<i2').astype(np.int64)


def frames():
    return open_frames(FULL + '.frames')


def entry_starts():
    """{entry: (init frame, retrace-set frame)} from full.log."""
    import re
    init, setf = {}, {}
    for line in open(FULL + '.log'):
        m = re.match(r'entry (\d+): (init|retrace routine set) at frame (\d+)', line)
        if m:
            (init if m.group(2) == 'init' else setf)[int(m.group(1))] = int(m.group(3))
    return init, setf

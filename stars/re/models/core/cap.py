"""Access to the reference capture (full.frames / full.ticks) and the overlay files, for the core models."""
import os
import numpy as np

RE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
REC = 64000 + 768
_frames = None
_ticks = None


def frames():
    global _frames
    if _frames is None:
        _frames = np.memmap(os.path.join(RE, 'full.frames'), dtype=np.uint8, mode='r', offset=32)
    return _frames


def screen(f):
    return np.asarray(frames()[f * REC:f * REC + 64000]).reshape(200, 320)


def palette(f):
    return np.asarray(frames()[f * REC + 64000:(f + 1) * REC]).reshape(256, 3)


def nframes():
    return len(frames()) // REC


def ticks():
    """rows: frame, time, pit, gus, order, row, part, callback, drawn, eip (part / callback / eip as int)."""
    global _ticks
    if _ticks is None:
        rows = []
        for line in open(os.path.join(RE, 'full.ticks')):
            if line.startswith('#'): continue
            p = line.split()
            rows.append((int(p[0]), float(p[1]), int(p[2]), int(p[3]), int(p[4]), int(p[5]), int(p[6], 16),
                         int(p[7], 16), int(p[8]), int(p[9], 16)))
        _ticks = rows
    return _ticks


def overlay(n):
    return open(os.path.join(RE, 'unpacked', 'files', '%02d.bin' % n), 'rb').read()

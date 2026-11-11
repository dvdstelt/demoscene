"""frames.py: read records of full.frames (320x200 5:6:5) and full.ticks / full.flips."""
import numpy as np, os
RE = '/home/dennis/src/dvdstelt/demoscene/jizz/re'
REC = 128000
def raw(t, path=os.path.join(RE, 'full.frames')):
    with open(path, 'rb') as f:
        f.seek(32 + t * REC)
        return np.frombuffer(f.read(REC), '<u2').reshape(200, 320)
def rgb(p):
    r = (p >> 11) & 31; g = (p >> 5) & 63; b = p & 31
    return np.stack([(r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31], -1).astype(np.uint8)
def ticks():
    out = []
    for line in open(os.path.join(RE, 'full.ticks')):
        if line.startswith('#'): continue
        v = line.split()
        out.append((int(v[0]), int(v[1]), float(v[2]), int(v[3]), int(v[4]), int(v[5]), int(v[6]), v[7], int(v[8]), v[9]))
    return out
def flips():
    out = []
    for line in open(os.path.join(RE, 'full.flips')):
        if line.startswith('#'): continue
        v = line.split()
        out.append((int(v[0]), int(v[1]), float(v[2]), int(v[3]), v[4]))
    return out

"""Per part (PARTS.md frame ranges), how the DAC palette changes in the capture: number of distinct palettes, and
which overlay palette (a 0x300 file or the first 0x300 bytes of a picture file) it equals scaled by f/256."""
import sys
import numpy as np
import cap

PARTS = [(279, 5211, 'intro'), (5421, 5941, 'cloud 126ae'), (5947, 7015, 'bee 11e7f'), (7170, 9475, 'spikes 6534'),
         (9478, 11312, 'warp 975b'), (11498, 13803, 'tunnel 11e8'), (13810, 15971, 'surfaces a0ca'),
         (15972, 16642, 'picture 57'), (16643, 18450, 'star 164dc'), (18451, 18996, 'particles 133ad'),
         (18999, 19468, 'dissolve 1535c'), (19504, 21955, 'torso 16f7c'), (21956, 23432, 'particles 133f1'),
         (23436, 25162, 'face 1089c'), (25163, 26667, 'credits')]
PALS = {n: np.frombuffer(cap.overlay(n)[:768], np.uint8).reshape(256, 3).astype(int)
        for n in (4, 15, 20, 22, 26, 31, 42, 48, 8, 9, 10, 11, 12, 13, 14, 52, 56, 57, 35)}


def which(dac):
    out = []
    for n, p in PALS.items():
        for f in (256,):
            if np.array_equal(np.minimum(63, (p * f) >> 8), dac): out.append(n)
    return out


for a, b, name in PARTS:
    seen = {}
    for f in range(a, b + 1, 1):
        k = cap.palette(f).tobytes()
        seen.setdefault(k, f)
    full = [which(np.frombuffer(k, np.uint8).reshape(256, 3).astype(int)) for k in seen]
    hits = sorted(set(x for h in full for x in h))
    print(f'{name:18s} frames {a}-{b}: {len(seen):5d} distinct palettes; equal to a whole file palette: {hits}')

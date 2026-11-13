"""The texture formulas of texgen.py in plain doubles (numpy), as a port would write them, and their continuous
high-resolution form.

usage (inside the toolbox, from clone/re):
  python3 models/gfx/texplain.py               compare with texgen.py's exact FPU model (bytes) and measure how far
                                               the continuous variants are from the original bytes
  python3 models/gfx/texplain.py --png 4       also write dump/textures/hires_<name>.png at 4x (continuous form)

Variants (radius r of the cell pattern, c = the cell brightness):
  'int'       r = floor(sqrt(dx^2 + dy^2)) at integer texels, c and the stored byte truncated: the original
  'float'     as 'int' but c and the level not truncated (float levels)
  'cont'      r = sqrt(dx^2 + dy^2) continuous, nothing truncated
  'half'      r = max(0, sqrt(dx^2 + dy^2) - 0.5): the continuous radius shifted by the mean of the floor
Texel coordinates: u = x (texel index) at scale 1; at scale s, sample X has u = X / s.
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)


def enc23(x, q):
    """0x4034e7: level = 128 + 23 ln(1 + x), clamped to 0..255 (truncated when q)."""
    v = 23.0 * np.log1p(x) + 128.0
    return np.clip(np.trunc(v) if q else v, 0, 255)


def enc90(x, q):
    """0x4167b7: level = 128 + (1000 / 11) ln((1 + x) / 128), clamped to 0..255 (truncated when q)."""
    v = np.log((1.0 + x) / 128.0) * (1000.0 / 11.0) + 128.0
    return np.clip(np.trunc(v) if q else v, 0, 255)


def radius(u, v, n, variant):
    dx, dy = u % n - n / 2, v % n - n / 2
    r = np.sqrt(dx * dx + dy * dy)
    if variant in ('int', 'float'): return np.floor(r)
    if variant == 'half': return np.maximum(r - 0.5, 0.0)
    return r


def build(variant='int', scale=1, rows=257):
    q = variant == 'int'
    tr = np.trunc if q else (lambda a: a)
    Y, X = np.mgrid[0:rows * scale, 0:256 * scale].astype(np.float64)
    u, v = X / scale, Y / scale
    su, sv = np.sin(u / 128.0 * np.pi), np.sin(v / 128.0 * np.pi)
    lo = lambda a0: a0 + 0.05 * su + 0.05 * sv       # noqa: E731
    hi = 0.7 + 0.15 * su + 0.15 * sv
    c8 = tr((np.cos(radius(u, v, 8, variant) * np.pi / 8) + 1.0) * 48.0)
    c16 = tr((np.cos(radius(u, v, 16, variant) * np.pi / 16) + 1.0) * 56.0)
    r32 = radius(u, v, 32, variant)
    c32 = tr((np.cos(r32 * np.pi / 32) + 1.0) * 56.0)
    cA = tr(168.0 - (np.cos(r32 * np.pi / 32) + 1.0) * 56.0)
    if q: cA[np.floor(r32) == 16] = 111       # the truncating FPU: 168 - 56.0000000000000034 (see texgen.py)
    t1 = np.stack([enc23(c8 * 0.7, q), enc23(c8 * lo(0.5), q), enc23(c8 * hi, q)], -1)          # R, G, B
    t2 = np.stack([enc23(c32 * 0.74, q), enc23(c32 * lo(0.42), q), enc23(c32 * hi, q)], -1)    # R, G, B
    mapC = np.stack([enc90(c8 * 1.5, q), enc90(c8 * hi, q), enc90(c8 * lo(0.7), q)], -1)       # B, G, R
    mapB = np.stack([enc90(c16 * 1.5, q), enc90(c16 * lo(0.7), q), enc90(c16 * hi, q)], -1)    # B, G, R
    mapA = np.stack([enc90(cA * 1.5, q), enc90(cA * lo(0.7), q), enc90(cA * hi, q)], -1)       # B, G, R
    return mapA, mapB, mapC, t1, t2


NAMES = ('map A', 'map B', 'map C', 'table 1', 'table 2')


def main():
    import texgen
    exact = texgen.build()
    ex = [np.frombuffer(bytes(e[:257 * 256 * n]), np.uint8).reshape(257, 256, n)[..., :3].astype(float)
          for e, n in zip(exact, (4, 4, 4, 3, 3))]
    for variant in ('int', 'float', 'cont', 'half'):
        print(f'variant {variant}:')
        for name, e, p in zip(NAMES, ex, build(variant)):
            d = p - e
            print(f'  {name}: {np.count_nonzero(np.abs(d) >= 0.5 if variant != "int" else d)} of {d.size} levels '
                  f'differ, mean |diff| {np.abs(d).mean():.2f}, mean diff {d.mean():+.2f}, max {np.abs(d).max():.1f}')
    # the fillers interpolate the levels L and decode afterwards (geometric mean of the linear values); how much
    # brighter is the arithmetic mean of the decoded neighbours, halfway between two texels (x or y neighbour)?
    print('linear vs log-domain filtering, halfway between neighbouring texels:')
    for name, e in zip(NAMES, ex):
        L = e[:256]
        V = 128 * np.exp((L - 128) * 0.011) if name.startswith('map') else np.exp((L - 128) / 23)
        rel = max((((V + np.roll(V, -1, ax)) / 2) / np.sqrt(V * np.roll(V, -1, ax)) - 1).max() for ax in (0, 1))
        print(f'  {name}: up to {rel * 100:.2f} % brighter')
    if '--png' in sys.argv:
        from PIL import Image
        s = int(sys.argv[sys.argv.index('--png') + 1])
        out = os.path.join(RE, 'dump', 'textures')
        os.makedirs(out, exist_ok=True)
        for name, m in zip(NAMES, build('cont', s, rows=256)):
            rgb = m[..., ::-1] if name.startswith('map') else m
            Image.fromarray(np.uint8(np.clip(rgb, 0, 255))).save(
                os.path.join(out, 'hires_' + name.replace(' ', '') + '.png'))


if __name__ == '__main__':
    main()

"""The continuous form of the ring twirl (entries 9, 10, 15) against the exact model.

usage: python3 twirl_continuous.py [--refresh 6500] [--out cmp.png]      (a record of entry 9)

Renders a record of entry 9/10 with twirl.py's exact model and with the continuous formula of the notes
(notes, section 5.5: the rotation of ring R by f = (phase + B + (R - 1) twist) / 4096 of its visible angles,
centre (160, 101), nearest-neighbour read) and prints how many pixels agree; --out writes them next to the
capture (capture, exact, continuous with the clipped arcs, continuous as a plain rotation), at 2x.
"""
import argparse
import math

import numpy as np
from PIL import Image

from common import Mem, snap, frames, indexed, raw, pal6_to_rgb
import twirl


def continuous(pic, phase, d, b=0, arcs=True):
    """Pixel (X, Y) at distance rho from c = (160, 101) belongs to the ring of radius R = rho + 0.5 (index R - 1).
    The ring's visible angles (rows 1..199: -99 <= R sin(psi) < 100, about |sin psi| < 99.5 / R) are walked from
    psi = 2 pi downwards (the list order); the colours move f = (phase + B + (R - 1) twist) / 4096 of that walk:
    out(s) = in(s - f L), s the position along the walk, L its length (2 pi for a full ring). arcs=False treats every
    ring as full (a plain rotation by 2 pi f)."""
    Y, X = np.mgrid[0:200, 0:320].astype(float)
    dx = X + 0.5 - 160.0
    dy = Y + 0.5 - 101.0
    rho = np.hypot(dx, dy)
    R = rho + 0.5
    f = (phase + b + (R - 1) * d) / 4096
    psi = np.arctan2(-dy, dx) % (2 * math.pi)
    if arcs:
        al = np.arcsin(np.clip(99.5 / R, 0, 1))
        full = al >= math.pi / 2 - 1e-9
        al = np.where(full, math.pi / 2, al)
        L = 4 * al
        # position along the walk: segment A (2pi - al, 2pi], B (pi - al, pi + al), C [0, al)
        s = np.where(psi > 2 * math.pi - al, 2 * math.pi - psi,
                     np.where(psi < al, 3 * al + (al - psi), al + (math.pi + al - psi)))
        s = np.where(full, (2 * math.pi - psi) % (2 * math.pi), s)
        ss = (s - f * L) % L
        src = np.where(ss < al, 2 * math.pi - ss, np.where(ss < 3 * al, math.pi + al - (ss - al), al - (ss - 3 * al)))
        src = np.where(full, 2 * math.pi - ss, src)
    else:
        src = psi + 2 * math.pi * f
    sx = 160.0 + rho * np.cos(src)
    sy = 101.0 - rho * np.sin(src)
    xi = np.clip(np.floor(sx).astype(int), 0, 319)
    yi = np.clip(np.floor(sy).astype(int), 0, 199)
    out = pic[yi, xi]
    outside = rho > 120.5
    out[outside] = pic[outside]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--refresh', type=int, default=6500)
    ap.add_argument('--out')
    a = ap.parse_args()
    assert twirl.ENTRY[9]['set'] <= a.refresh < twirl.ENTRY[9]['end'], 'a record of entry 9 (6331..6776)'
    raw_frames, n = frames()
    mem = Mem(snap('snaps/f6300.snap'))
    pic, pal = raw('GFX\\PLAYMO.RAW')
    rings = twirl.build_rings(pic, mem)
    t = twirl.Twirl(9)
    buf = pic.copy()
    for k in range(a.refresh - twirl.ENTRY[9]['set']):
        t.retrace()
    t.frame(buf, rings)
    px, _ = indexed(raw_frames, a.refresh)
    c = continuous(pic, t.phase, t.d)
    c0 = continuous(pic, t.phase, t.d, arcs=False)
    Y, X = np.mgrid[0:200, 0:320]
    rho = np.hypot(X + 0.5 - 160, Y + 0.5 - 101)
    print('record %d (phase %d, twist %d): exact model = capture: %s' % (a.refresh, t.phase, t.d, (px == buf).all()))
    for name, img in (('with the clipped arcs', c), ('plain rotation', c0)):
        nz = (buf != 0) | (img != 0)
        print('  continuous %-22s equals the exact model on %.0f%% of the non-black pixels (%.0f%% at rho < 99, '
              '%.0f%% at rho >= 99)' % (name, 100 * (img == buf)[nz].mean(), 100 * (img == buf)[nz & (rho < 99)].mean(),
                                         100 * (img == buf)[nz & (rho >= 99)].mean()))
    if a.out:
        lut = pal6_to_rgb(pal >> 2)
        img = np.concatenate([lut[px], lut[buf], lut[c], lut[c0]], axis=1)
        Image.fromarray(img).resize((img.shape[1] * 2, img.shape[0] * 2), Image.NEAREST).save(a.out)


if __name__ == '__main__':
    main()

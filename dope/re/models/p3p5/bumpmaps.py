"""Bump textures of p3 (2341:0000) and p5 (1c96:0000): nibble statistics and a check whether the nibbles are the
gradient of a height field (curl test), plus a least-squares height field reconstruction (out/*.png)."""
import os
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
OUT = os.path.join(HERE, 'out')

def tex(part, seg):
    img = open(os.path.join(RE, 'unpacked', part + '.bin'), 'rb').read()
    a = np.frombuffer(img[seg * 16:seg * 16 + 65536], np.uint8).reshape(256, 256)
    return a[:128, :128].astype(int)

def corr(a, b):
    a = a - a.mean(); b = b - b.mean()
    return (a * b).sum() / np.sqrt((a * a).sum() * (b * b).sum())

def recon(gx, gy):
    """periodic least-squares integration (FFT Poisson) of gx = dh/dcol, gy = dh/drow"""
    n = gx.shape[0]
    fx = np.fft.fft2(gx); fy = np.fft.fft2(gy)
    kx = np.fft.fftfreq(n)[None, :] * 2 * np.pi; ky = np.fft.fftfreq(n)[:, None] * 2 * np.pi
    dx = np.exp(1j * kx) - 1; dy = np.exp(1j * ky) - 1
    den = np.abs(dx) ** 2 + np.abs(dy) ** 2; den[0, 0] = 1
    h = np.real(np.fft.ifft2((np.conj(dx) * fx + np.conj(dy) * fy) / den))
    return h

for part, seg in (('p3', 0x2341), ('p5', 0x1c96)):
    t = tex(part, seg)
    hi = (t >> 4) - 8; lo = (t & 15) - 8
    print(part, 'hi nibble hist', np.bincount(t.ravel() >> 4, minlength=16).tolist())
    print(part, 'lo nibble hist', np.bincount(t.ravel() & 15, minlength=16).tolist())
    print(part, 'mean hi %.2f lo %.2f' % (hi.mean(), lo.mean()), 'values 128..143:', ((t >= 128) & (t < 144)).sum())
    # derivative along rows (axis 0) and columns (axis 1), periodic
    d_r = lambda a: np.roll(a, -1, 0) - a
    d_c = lambda a: np.roll(a, -1, 1) - a
    print(part, 'curl test: corr(d_r hi, d_c lo) %.3f   corr(d_c hi, d_r lo) %.3f' % (corr(d_r(hi), d_c(lo)), corr(d_c(hi), d_r(lo))))
    # hi as column-gradient, lo as row-gradient (one hypothesis), and swapped
    for name, gx, gy in (('hi=dh/dcol lo=dh/drow', hi, lo), ('hi=dh/drow lo=dh/dcol', lo, hi)):
        h = recon(gx.astype(float), gy.astype(float))
        rx = (np.roll(h, -1, 1) - h); ry = (np.roll(h, -1, 0) - h)
        res = np.sqrt(((rx - gx) ** 2 + (ry - gy) ** 2).mean())
        print(part, name, 'residual rms %.3f (of rms %.3f)' % (res, np.sqrt((gx ** 2 + gy ** 2).mean())))
        hn = ((h - h.min()) / (h.max() - h.min()) * 255).astype(np.uint8)
        Image.fromarray(hn).resize((256, 256), Image.NEAREST).save(os.path.join(OUT, '%s_bump_height_%s.png' % (part, name.split()[0].replace('=', '_').replace('/', ''))))
    Image.fromarray(((hi + 8) * 17).astype(np.uint8)).resize((256, 256), Image.NEAREST).save(os.path.join(OUT, '%s_bump_hi.png' % part))
    Image.fromarray(((lo + 8) * 17).astype(np.uint8)).resize((256, 256), Image.NEAREST).save(os.path.join(OUT, '%s_bump_lo.png' % part))

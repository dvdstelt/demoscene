"""How the in-place radial blur (0x403df7) compares with order-free formulations a GPU can run.

The original runs one sequential pass per frame, reading the buffer it writes. This script feeds it typical inputs
(4x4 stamps as parts 1 and 6 make them) and compares its result with:
  A  the fixed point of out = min(255, in + L * bilinear(out, c + S (p - c)))  (every pixel reads finished pixels:
     the recursion the in-place pass approximates when its samples lie towards the centre),
  T  the same with the original's truncation of the level product: in + floor(L * bilinear(..)),
  H  the same with a constant loss instead: in + max(0, L * bilinear(..) - 0.5),
  B  the same series without the per-step clamp, built in log-doubling passes (B_{n+1}(p) = B_n(p) + L^(2^n)
     B_n(c + S^(2^n) (p - c)), 8 passes) and clamped once at the end,
  E  a single echo: min(255, in + L * bilinear(in, c + S (p - c))).
S = diag(1 + zoom, 1 + zoom * 0.8333 * s), zoom = -0.025, s = 1 (part 1) or sin(phase) (part 6, mode 1).
Prints the mean absolute difference (levels 0..255) over the visible rows 20..179.
usage: python3 blur_study.py
"""
import math

import numpy as np

import blur_model
from fpu import f32


def stamps(rng, n, stamp):
    b = np.zeros((200, 320), dtype=np.int32)
    for _ in range(n):
        x, y = rng.integers(0, 317), rng.integers(20, 177)
        b[y:y + 4, x:x + 4] += stamp
    return np.minimum(b, 255).astype(np.uint8)


def bilinear(img, sx, sy):
    h, w = img.shape
    x0 = np.floor(sx).astype(int); y0 = np.floor(sy).astype(int)
    fx = sx - x0; fy = sy - y0
    def at(y, x):
        ok = (x >= 0) & (x < w) & (y >= 0) & (y < h)
        return np.where(ok, img[np.clip(y, 0, h - 1), np.clip(x, 0, w - 1)], 0.0)
    top = at(y0, x0) * (1 - fx) + at(y0, x0 + 1) * fx
    bot = at(y0 + 1, x0) * (1 - fx) + at(y0 + 1, x0 + 1) * fx
    return top * (1 - fy) + bot * fy


def series_fixed_point(inp, L, kx, ky, iters=1000, step=None):
    yy, xx = np.mgrid[0:200, 0:320].astype(float)
    sx = 160 + (xx - 160) * kx; sy = 100 + (yy - 100) * ky
    out = inp.astype(float)
    step = step or (lambda v: L * v)
    for _ in range(iters):
        nxt = np.minimum(255, inp + step(bilinear(out, sx, sy)))
        if np.abs(nxt - out).max() < 0.01: break
        out = nxt
    return out


def series_doubling(inp, L, kx, ky, passes=8):
    yy, xx = np.mgrid[0:200, 0:320].astype(float)
    out = inp.astype(float)
    for n in range(passes):
        m = 2 ** n
        sx = 160 + (xx - 160) * kx ** m; sy = 100 + (yy - 100) * ky ** m
        out = out + L ** m * bilinear(out, sx, sy)
    return np.minimum(out, 255)


def echo(inp, L, kx, ky):
    yy, xx = np.mgrid[0:200, 0:320].astype(float)
    return np.minimum(255, inp + L * bilinear(inp.astype(float), 160 + (xx - 160) * kx, 100 + (yy - 100) * ky))


def main():
    rng = np.random.default_rng(7)
    st0 = np.array([[21, 41, 41, 21], [41, 73, 73, 41], [41, 73, 73, 41], [21, 41, 41, 21]])
    st1 = np.array([[3, 5, 5, 3], [5, 6, 6, 5], [5, 6, 6, 5], [3, 5, 5, 3]])
    BUF0 = 0xeaf410
    print('input            L     s     | original mean  | |orig-A|  |orig-T|  |orig-H|  |orig-B|  |orig-E|   (mean abs, rows 20..179)')
    for name, st, n in (('part 1 (600 st)', st0, 600), ('part 6 (2500 st)', st1, 2500)):
        inp = stamps(rng, n, st)
        for L in (0.62, 0.8, 0.99):
            for s in (1.0, 0.5, 0.0, -0.5, -1.0):
                mem = np.zeros(BUF0 + 0x20000, dtype=np.uint8)
                mem[BUF0:BUF0 + 64000] = inp.reshape(-1)
                phase = math.asin(s) - 0.04 * 1.0          # the routine adds 0.04 dt (dt = 1) before using it
                mode = 0 if s == 1.0 else 1
                blur_model.radial_blur(mem, BUF0, 160.0, 100.0, f32(L), f32(-0.025), mode, phase, 1.0)
                orig = mem[BUF0:BUF0 + 64000].reshape(200, 320).astype(float)
                kx, ky = 0.975, 1 - 0.025 * 0.8333333 * s
                a = series_fixed_point(inp, L, kx, ky)
                t = series_fixed_point(inp, L, kx, ky, step=lambda v: np.floor(L * v))
                hh = series_fixed_point(inp, L, kx, ky, step=lambda v: np.maximum(0, L * v - 0.5))
                b = series_doubling(inp, L, kx, ky)
                e = echo(inp, L, kx, ky)
                v = slice(20, 180)
                print('%-16s %.2f %+5.2f | %6.1f         | %7.1f  %7.1f  %7.1f  %7.1f  %7.1f' % (
                    name, L, s, orig[v].mean(), np.abs(orig - a)[v].mean(), np.abs(orig - t)[v].mean(),
                    np.abs(orig - hh)[v].mean(), np.abs(orig - b)[v].mean(),
                    np.abs(orig - e)[v].mean()))


if __name__ == '__main__':
    main()

"""How far is the packed 8.8 evaluation of 0x9a62 from the same patch formula in floats?

For some frames of runs/warp2 (curves dumped at 0x9a62), computes per pixel the texel coordinates (u, v) both ways:
the integer path (accumulators exactly as 0x9a62, see warp_model.draw) and
  P(x, y) = T(x) + (y+1)/200 (B(x) - T(x)) + Dl(y) + x/200 (Dr(y) - Dl(y)) + (u0, v0)
with Dl / Dr the left / right curve minus the straight line between the corner averages. Prints the largest and the
mean difference in texels (mod 256), and the share of pixels that read another texel."""
import numpy as np

import warp_model as W
from common import *

M32 = 0xffffffff
L = [l.split() for l in open('runs/warp2.hook')]


def int_uv(top, bot, right, left, u0, v0):
    s32, pack, pstep, idiv = W.s32, W.pack, W.pstep, W.idiv
    avg = lambda a, b: (s32(a[0] + b[0]) >> 1, s32(a[1] + b[1]) >> 1)
    TL, TR = avg(top[0], left[0]), avg(top[319], right[0])
    BL, BR = avg(bot[0], left[199]), avg(bot[319], right[199])
    acc = np.array([pack(*top[x]) for x in range(320)], np.int64)
    stp = np.array([pstep(bot[x][0] - top[x][0], bot[x][1] - top[x][1], 200) for x in range(320)], np.int64)
    Lx, Ly, Rx, Ry = TL[0], TL[1], TR[0], TR[1]
    dLx, dLy = idiv(BL[0] - TL[0], 200), idiv(BL[1] - TL[1], 200)
    dRx, dRy = idiv(BR[0] - TR[0], 200), idiv(BR[1] - TR[1], 200)
    off = ((u0 << 16) | (v0 & 0xffff)) & M32
    U = np.zeros((200, 320)); V = np.zeros((200, 320))
    xs = np.arange(320, dtype=np.int64)
    for y in range(200):
        start = pack(s32(left[y][0] - Lx), s32(left[y][1] - Ly))
        h = pstep(s32(s32(right[y][0] - Rx) - s32(left[y][0] - Lx)), s32(s32(right[y][1] - Ry) - s32(left[y][1] - Ly)), 200)
        acc = (acc + stp) & M32
        e = (acc + ((start + off) & M32) + xs * h) & M32
        U[y] = (e >> 16) / 256.0
        V[y] = (e & 0xffff) / 256.0
        Lx, Ly, Rx, Ry = s32(Lx + dLx), s32(Ly + dLy), s32(Rx + dRx), s32(Ry + dRy)
    return U, V


def float_uv(top, bot, right, left, u0, v0):
    T, B, R, Lf = [np.array(c, float) / 65536 for c in (top, bot, right, left)]
    TL, TR, BL, BR = (T[0] + Lf[0]) / 2, (T[319] + R[0]) / 2, (B[0] + Lf[199]) / 2, (B[319] + R[199]) / 2
    y = np.arange(200)[:, None, None]
    x = np.arange(320)[None, :, None]
    Dl = Lf[:, None, :] - (TL + y / 200 * (BL - TL))
    Dr = R[:, None, :] - (TR + y / 200 * (BR - TR))
    P = T[None, :, :] + (y + 1) / 200 * (B - T)[None, :, :] + Dl + x / 200 * (Dr - Dl)
    return (P[..., 0] + u0 / 256) % 256, (P[..., 1] + v0 / 256) % 256


for i in range(len(L) - 2):
    if L[i][0] != '9989' or L[i + 1][0] != '9a62':
        continue
    f = int(L[i][1])
    if f % 150:
        continue
    p = W.parse(L[i + 1])
    c = np.frombuffer(bytes.fromhex(L[i + 1][11:][3]), '<i4').reshape(-1, 2)
    tl = lambda a: [tuple(int(v) for v in r) for r in a]
    top, bot, left, right = tl(c[0:320]), tl(c[320:640]), tl(c[640:840]), tl(c[840:1040])
    Ui, Vi = int_uv(top, bot, right, left, p['u0'], p['v0'])
    Uf, Vf = float_uv(top, bot, right, left, p['u0'], p['v0'])
    du = (Ui - Uf + 128) % 256 - 128
    dv = (Vi - Vf + 128) % 256 - 128
    other = np.mean((np.floor(Ui) != np.floor(Uf)) | (np.floor(Vi) != np.floor(Vf)))
    print('frame %d: u diff max %.2f mean %.2f, v diff max %.2f mean %.2f texels; other texel at %.0f%% of pixels'
          % (f, np.abs(du).max(), np.abs(du).mean(), np.abs(dv).max(), np.abs(dv).mean(), 100 * other))

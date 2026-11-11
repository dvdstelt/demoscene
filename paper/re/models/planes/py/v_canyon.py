# verify 0x2f24 + 0x3095 against a Python model
import sys, struct, math
import numpy as np
from emu import Emu
tick = int(sys.argv[1])
e = Emu(tick)
# fresh state: call 0x2f24 and 0x3095 ourselves
M = e.ff(0x23a8, 9); C = e.ff(0x23cc, 3); K = e.ff(0x23d8, 4)
print('M', M, 'C', C, 'K', K)
e.call(0x2f24)
grid = np.array(e.ii(e.d(0x1edd9), 41 * 26 * 3)).reshape(26, 41, 3)
back = e.d(0x1edf9)
e.call(0x3095, edi=back)
img = np.frombuffer(e.rd(back, 64000), np.uint8).reshape(200, 320)
tex = np.frombuffer(e.rd(e.d(0x1eddd), 65536), np.uint8).reshape(256, 256)
shade = np.frombuffer(e.rd(e.d(0x1eded), 65 * 256), np.uint8).reshape(65, 256)
np.save('grid_%d.npy' % tick, grid); np.save('img_%d.npy' % tick, img); np.save('tex_%d.npy' % tick, tex); np.save('shade.npy', shade)
# model of 0x2f24 (float64)
H = K[3]
g2 = np.zeros((26, 41, 3), np.int64)
def rint(x):  # x87 round to nearest even
    return int(np.rint(x))
for j in range(26):
    for i in range(41):
        sx, sy = -20.0 + i, -12.51 + j  # K[0], K[1] as float32
        sx = float(np.float32(K[0])) + i; sy = float(np.float32(K[1])) + j
        dx = M[0] * sx + M[3] * sy + M[6] * K[2]
        dy = M[1] * sx + M[4] * sy + M[7] * K[2]
        dz = M[2] * sx + M[5] * sy + M[8] * K[2]
        t = (H - C[2]) / dz
        if t < 0: t = (-H - C[2]) / dz
        s = rint(t) - 0x400
        s = min(s, -256); s = -s * 8; s = min(s, 0x2000)
        v = rint((t * dx + C[0]) * K[1])
        u = rint((t * dy + C[1]) * K[1])
        g2[j, i] = (u, v, s)
d = np.abs(g2 - grid)
print('grid diff max', d.max(axis=(0, 1)), 'count', (d > 0).sum(axis=(0, 1)))
# model of 0x3095: exact integer emulation of the block interpolation
def model(grid):
    out = np.zeros((200, 320), np.int64)
    for by in range(25):
        for bx in range(40):
            P00, P10, P01, P11 = grid[by, bx], grid[by, bx + 1], grid[by + 1, bx], grid[by + 1, bx + 1]
            # per channel: start (8.8), dx step (>>3), d(dx)/dy (>>6), left dy step (>>3)
            st = [int(P00[c]) for c in range(3)]
            dxs = [(int(P10[c]) - int(P00[c])) >> 3 for c in range(3)]
            ddx = [((int(P11[c]) - int(P01[c])) - (int(P10[c]) - int(P00[c]))) >> 6 for c in range(3)]
            dy = [(int(P01[c]) - int(P00[c])) >> 3 for c in range(3)]
            row = st[:]
            step = dxs[:]
            for y in range(8):
                cur = row[:]
                for x in range(8):
                    u, v, s = cur
                    texel = tex[(u >> 8) & 255, (v >> 8) & 255]
                    out[by * 8 + y, bx * 8 + x] = shade[(s >> 8) & 255 if (s >> 8) & 255 < 65 else 64, texel]
                    cur = [cur[c] + step[c] for c in range(3)]
                row = [row[c] + dy[c] for c in range(3)]
                step = [step[c] + ddx[c] for c in range(3)]
    return out
m = model(grid)
diff = (m != img)
print('pixels differing (model vs emu):', diff.sum(), 'of 64000')
if diff.sum():
    ys, xs = np.nonzero(diff)
    print(list(zip(ys[:10], xs[:10])), m[diff][:10], img[diff][:10])
ys, xs = np.nonzero(d.max(axis=2) > 0)
for j, i in list(zip(ys, xs))[:12]:
    sx = float(np.float32(K[0])) + i; sy = float(np.float32(K[1])) + j
    dz = M[2] * sx + M[5] * sy + M[8] * K[2]
    print(j, i, 'dz %.5g' % dz, 'emu', grid[j, i], 'model', g2[j, i])

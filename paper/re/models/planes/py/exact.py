import numpy as np, sys
from PIL import Image
from snap import M
tick = int(sys.argv[1]); sc = 2
m = M(tick)
Mx = np.array(m.ff(0x23a8, 9)).reshape(3, 3); C = np.array(m.ff(0x23cc, 3)); H = m.f(0x23e4)
tex = np.load('tex_%d.npy' % tick).astype(int); img = np.load('img_%d.npy' % tick)
pal = np.frombuffer(m.s['pal'], np.uint8).reshape(256, 3).astype(int) * 255 // 63
# exact per-pixel (pixel corners like the original grid: x = 8i -> sx = -20 + x/8)
W, Hh = 320 * sc, 200 * sc
xs = -20.0 + (np.arange(W) / sc) / 8.0; ys = -12.51 + (np.arange(Hh) / sc) / 8.0
X, Y = np.meshgrid(xs, ys)
d = X[..., None] * Mx[0] + Y[..., None] * Mx[1] + 28.0 * Mx[2]
t = (H - C[2]) / d[..., 2]
t = np.where(t < 0, (-H - C[2]) / d[..., 2], t)
u = (t * d[..., 1] + C[1]) * -12.51; v = (t * d[..., 0] + C[0]) * -12.51
lev = tex[(np.floor(u / 256).astype(np.int64)) & 255, (np.floor(v / 256).astype(np.int64)) & 255] - 128
fog = np.clip(t / 32.0, 0, 24)
b = np.clip(lev - fog, 0, 31)
# continuous palette interpolation on the 32-entry ramp
lo = np.floor(b).astype(int); fr = (b - lo)[..., None]; hi = np.minimum(lo + 1, 31)
rgb = pal[128 + lo] * (1 - fr) + pal[128 + hi] * fr
orig = pal[img].repeat(sc, 0).repeat(sc, 1)
Image.fromarray(np.concatenate([orig, rgb], 1).astype(np.uint8)).save('../exact_%d.png' % tick)

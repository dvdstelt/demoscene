"""Search the layer offsets that make the cloud effect look like file 27 (bee background), by FFT correlation.

Per pixel, estimate the two layer values (i = layer 1, j = layer 2) from file 27's RGB (palette 26), using the
near-linear 16x16 cloud palette (file 31), then correlate each estimate with the texture in (angle, depth) space.
"""
import numpy as np
from common import out, file

T = np.load(out('cloud_table.npy'))
TEX = np.frombuffer(file(30), np.uint8)[:65536].astype(float).reshape(256, 256)
f27 = np.frombuffer(file(27), np.uint8).reshape(200, 320)
p26 = np.frombuffer(file(26), np.uint8).reshape(256, 3).astype(float)
p31 = np.frombuffer(file(31), np.uint8).reshape(16, 16, 3).astype(float)   # [j][i]
# least squares fit of the cloud palette: rgb = c + a*i + b*j
I, J = np.meshgrid(np.arange(16), np.arange(16))
A = np.stack([np.ones(256), I.ravel(), J.ravel()], 1)
coef, *_ = np.linalg.lstsq(A, p31.reshape(256, 3), rcond=None)
rgb = p26[f27].reshape(-1, 3)
# solve rgb - c = [a b] (i j) per pixel
M = coef[1:].T                              # 3x2
ij, *_ = np.linalg.lstsq(M, (rgb - coef[0]).T, rcond=None)
ii = ij[0].reshape(200, 320); jj = ij[1].reshape(200, 320)
valid = (f27 != 0)
valid[0] = valid[198:] = False
ang = (T >> 8).reshape(200, 320); dep = (T & 255).reshape(200, 320)

def corr(est):
    O = np.zeros((256, 256)); W = np.zeros((256, 256))
    np.add.at(O, (ang[valid], dep[valid]), est[valid] - est[valid].mean())
    np.add.at(W, (ang[valid], dep[valid]), 1)
    # C(A, D) = sum O[a,d] * TEX[a+A, d+D]
    C = np.real(np.fft.ifft2(np.conj(np.fft.fft2(O)) * np.fft.fft2(TEX - TEX.mean())))
    k = np.argsort(C.ravel())[::-1][:5]
    return [(int(x // 256), int(x % 256), round(float(C.ravel()[x]), 1)) for x in k]

print('layer 1 (A1, D1):', corr(ii))
print('layer 2 (A2, D2):', corr(jj))


def render(si, di):
    TEXi = np.frombuffer(file(30), np.uint8)[:65536].astype(np.int64)
    p = np.arange(64000)
    c1 = TEXi[(T + si) & 0xffff]
    base = (T[p | 1] & 0xfffe) + di
    c2 = TEXi[(base + (p & 1)) & 0xffff]
    return (c1 + 16 * c2).reshape(200, 320)


if __name__ == '__main__':
    for si, di in [(86 * 256 + 108, 219 * 256 + 228)]:
        img = render(si, di)
        rows = slice(1, 198)
        d = np.abs(p31.reshape(256, 3)[img] - p26[f27])[rows].sum(2)
        print('si %04x di %04x: mean |rgb diff| %.2f, pixels > 6: %d' % (si, di, d[valid[rows]].mean(), int((d[valid[rows]] > 6).sum())))
        # index consistency: for each cloud index, how often does file 27 use its most common index?
        a = img[rows][valid[rows]]; b = f27[rows][valid[rows]]
        agree = 0
        for v in np.unique(a):
            agree += np.bincount(b[a == v]).max()
        print('index function consistency: %.4f of %d pixels' % (agree / len(a), len(a)))
        print('index-0 pixels in file 27 (patched colour 0):', int((f27 == 0).sum()), 'their cloud indices:', np.bincount(img[f27 == 0] , minlength=256).nonzero()[0][:20])

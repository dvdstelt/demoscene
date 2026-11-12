"""Model of how p2's objects are placed and projected (engine 348b / 1d86 / 1c60 with p2's parameters), checked
against the engine's own render buffer 4527 in the dumps of p2trace.py (silhouette coverage).

Per object record (0x1810 + k*0x3a): angles = (obj angle + camera angle) >> 4 (signed), sine table index =
(that >> 1) & 0x7ff (2048 steps per turn); M = Rz(c) Rx(a) Ry(b) with 15-bit entries; per vertex
p = v + pivot; X = M0.p + (objx+camx)<<8; Y = M1.p + (objy+camy)<<8; Z = M2.p - (objz+camz)<<8;
x = 160 + (X*f/Z)>>3 (rounded), y = 100 + (3*(Y*f/Z))>>5 (rounded), in render-buffer rows (screen row = y - 1).
"""
import glob, math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mesh import mesh

img = open(os.path.join(HERE, '../../unpacked/p2.bin'), 'rb').read()


def sintab():
    q = np.frombuffer(img[0x100 + 0x410:0x100 + 0x410 + 1024], '<i2').astype(np.int64)
    t = np.zeros(2048 + 512, np.int64)
    t[0:512] = q; t[512:1024] = q[::-1]; t[1024:2048] = -t[0:1024]; t[2048:2560] = q
    return t


SIN = sintab()


def s15(v):          # sar 15; adc 0  (round half up)
    return (v >> 15) + ((v >> 14) & 1)


def matrix(a, b, c):
    def sc(x):
        i = ((((x + 0x8000) & 0xffff) - 0x8000) >> 4) & 0xffe
        return int(SIN[i // 2]), int(SIN[i // 2 + 512])
    sa, ca = sc(a); sb, cb = sc(b); ss, cs = sc(c)
    m00 = s15(cb * cs - s15(sb * sa) * ss); m01 = s15(ca * ss); m02 = s15(sb * cs + s15(cb * sa) * ss)
    m10 = s15(-(cb * ss + s15(sb * sa) * cs)); m11 = s15(ca * cs); m12 = s15(-(sb * ss - s15(cb * sa) * cs))
    m20 = s15(-sb * ca); m21 = -sa; m22 = s15(cb * ca)
    return np.array([[m00, m01, m02], [m10, m11, m12], [m20, m21, m22]], np.int64)


def project(V, ang, pos, pivot, f):
    M = matrix(*ang)
    P = V.astype(np.int64) + np.array(pivot, np.int64)
    X = P @ M[0] + (pos[0] << 8); Y = P @ M[1] + (pos[1] << 8); Z = P @ M[2] - (pos[2] << 8)
    def div(a, b):
        q = np.abs(a) // np.abs(b); return np.where((a < 0) ^ (b < 0), -q, q)        # idiv truncates
    qx = div(X * f, Z); qy = div(Y * f, Z)
    x = (qx >> 3) + 160 + ((qx >> 2) & 1)
    q3 = qy * 3
    y = (q3 >> 5) + 100 + ((q3 >> 4) & 1)
    return x, y, Z


def raster(x, y, F, H=202, W=320):
    m = np.zeros((H, W), bool)
    yy, xx = np.mgrid[0:H, 0:W]
    for i, j, k in F:
        xs = np.array([x[i], x[j], x[k]], float); ys = np.array([y[i], y[j], y[k]], float)
        x0, x1 = int(max(0, xs.min())), int(min(W - 1, xs.max())); y0, y1 = int(max(0, ys.min())), int(min(H - 1, ys.max()))
        if x0 > x1 or y0 > y1: continue
        X = xx[y0:y1 + 1, x0:x1 + 1] + 0.5; Y = yy[y0:y1 + 1, x0:x1 + 1] + 0.5
        def e(a, b): return (xs[b] - xs[a]) * (Y - ys[a]) - (ys[b] - ys[a]) * (X - xs[a])
        e0, e1, e2 = e(0, 1), e(1, 2), e(2, 0)
        ins = ((e0 >= 0) & (e1 >= 0) & (e2 >= 0)) | ((e0 <= 0) & (e1 <= 0) & (e2 <= 0))
        m[y0:y1 + 1, x0:x1 + 1] |= ins
    return m


def frame_state(log, drawn):
    for line in open(log):
        if line.startswith('F ') and int(line.split()[3]) == drawn:
            f = line.split(); v = list(map(int, f[-12:]))
            kv = dict(t.split('=') for t in f[4:-12])
            return v, {k: int(x) for k, x in kv.items()}


def main():
    discs, C = mesh(0x1e71), mesh(0x2121)
    for p in sorted(glob.glob(os.path.join(HERE, 'out', 'runs', 'p2_dump_*.npz')), key=lambda s: int(s.split('_')[-1][:-4])):
        d = np.load(p)
        cam, var = frame_state(os.path.join(HERE, 'out', 'runs', 'p2.p2log'), int(d['drawn']))
        x, y, z, vx, vy, vz, a, b, c, va, vb, vc = cam
        ret = int(d['ret'])
        piv = None
        for line in open(os.path.join(HERE, 'out', 'runs', 'p2.p2log')):
            if line.startswith('R %d ' % ret): piv = int(line.split()[15]); break
        objs = [(discs, 4000, (piv, 0, 0))]
        if var['Chidden'] == 0: objs.append((C, 1300, (0, 0, 0)))
        cover = np.zeros((202, 320), bool)
        for m, f, pv in objs:
            px, py, Z = project(m['V'], (a, b, c), (x, y, z), pv, f)
            cover |= raster(px, py, m['F'][:, 1:] // 4)
        new = d['new'][:202 * 320].reshape(202, 320)
        eng = new != 0
        if var['backdrop']:
            bg = np.zeros((202, 320), np.uint8)
            bg[1:201] = np.frombuffer(img[0xec30:0xec30 + 64000], np.uint8).reshape(200, 320)
            eng = new != bg
        rows = np.ones(202, bool); rows[0] = rows[201] = False
        if var['strip'] != -2:
            r0 = var['row'] // 320; rows[r0:r0 + 32] = False
        eng, cov = eng[rows], cover[rows]
        print('%s ret %d: engine pixels %d, model %d, both %d, only engine %d, only model %d' % (
            os.path.basename(p), ret, eng.sum(), cov.sum(), (eng & cov).sum(), (eng & ~cov).sum(), (cov & ~eng).sum()))


if __name__ == '__main__':
    main()

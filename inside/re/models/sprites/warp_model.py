"""Model of the warping background of pictures 1 and 2 (warp grid, rasterizer 0x4172e0) against a dump.

usage: python3 models/sprites/warp_model.py models/sprites/runs/a.dump.pkl [out.png]
For every dumped frame with the warp grid visible:
 1. recomputes the grid's texture coordinates from the warp parameters (frame routine 0x4047a0) and checks them
    against the vertex array the frame used;
 2. projects the vertices with the dumped orientation matrix (Rx(1.5) Ry(3.0 + 0.01 m), angles.py) and the camera
    at (0, 0, -1300): screen (160 + 256 x / z, 100 + 256 * 5 / 6 * y / z), and checks the screen positions;
 3. rasterizes the 98 triangles in screen space (affine interpolation: the face flags select no perspective
    subdivision; sampled at pixel centres) with pixel = shd[tex[row2][col2] * 256 + tex[V1][U1]] and compares
    with the background the frame routine drew (the draw buffer before the sprite). out.png: captured | model |
    differences, for the last frame.
coverage_over_rotation() (and models/sprites/uncovered.py): does the grid cover the screen at every rotation.
"""
import os, pickle, struct, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile

d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
TEX = {k: np.frombuffer(d.get(f'nigga/nigga{k}.raw'), np.uint8).reshape(256, 256) for k in (1, 2)}
SHD = {k: np.frombuffer(d.get(f'nigga/nigga{k}.shd'), np.uint8).reshape(256, 256) for k in (1, 2)}


def warp_uv(params):
    """(64, 4) array of the words +0x28, +0x2a, +0x2c, +0x2e for vertex j * 8 + i (frame routine 0x4047a0)."""
    A, sA, kA, iA, jA, B, sB, kB, iB, jB, C, sC, kC, iC, jC = [np.float32(p) for p in params]
    out = np.zeros((64, 4), np.int64)
    for j in range(8):
        for i in range(8):
            a = float(iA) * i + float(A) + float(jA) * j          # the float32 constants, x87 arithmetic
            b = float(iB) * i + float(B) + float(jB) * j
            c = float(iC) * i + float(C) + float(jC) * j
            u1 = i * 512 + np.sin(a) * float(kA) + np.sin(b) * float(kB) + np.sin(c) * float(kC)
            v1 = j * 512 + np.cos(a) * float(kA) + np.cos(b) * float(kB) + np.cos(c) * float(kC)
            u2 = i * 512 + np.cos(a) * float(kC) + np.cos(b) * float(kB) + np.sin(c) * float(kA)   # word +0x2e
            v2 = j * 512 + np.sin(a) * float(kC) + np.cos(b) * float(kB) + np.sin(c) * float(kA)   # word +0x2c
            out[j * 8 + i] = [int(u1), int(v1), int(v2), int(u2)]          # truncation toward zero, like 0x40ba78
    return (out + 0x8000) % 0x10000 - 0x8000                              # stored as 16-bit words


def Rx(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def Ry(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def raster(tri, vals, H=200, W=320):
    """Affine interpolation of vals (3 x k) over the triangle tri (3 x 2 screen coordinates, pixels).
    Returns (mask, interpolated (H, W, k)). Pixel (x, y) is covered when its top-left corner is inside (the
    rasterizer starts spans at ceil of the edge, sub-pixel exact)."""
    (x0, y0), (x1, y1), (x2, y2) = tri
    ys, xs = np.mgrid[0:H, 0:W].astype(float)
    den = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    if den == 0:
        return np.zeros((H, W), bool), None
    w1 = ((xs - x0) * (y2 - y0) - (x2 - x0) * (ys - y0)) / den
    w2 = ((x1 - x0) * (ys - y0) - (xs - x0) * (y1 - y0)) / den
    w0 = 1 - w1 - w2
    e = -1e-9
    mask = (w0 >= e) & (w1 >= e) & (w2 >= e)
    out = w0[..., None] * vals[0] + w1[..., None] * vals[1] + w2[..., None] * vals[2]
    return mask, out


def main():
    frames = pickle.load(open(sys.argv[1], 'rb'))
    for fr in frames:
        if fr['pic'] >= 3:
            continue
        k = fr['pic']
        wg = fr['warpgrid']
        vb = wg['verts']
        verts = [struct.unpack_from('<IIiiiiiiii4hiiii', vb, 0x40 * n) for n in range(64)]
        words = np.array([v[10:14] for v in verts])
        model = warp_uv(fr['params'])
        uv_err = np.abs(words - model).max()
        # matrix check: obj+0x28 doubles
        M = np.array(struct.unpack_from('<9d', wg['raw'], 0x28)).reshape(3, 3)
        cam = struct.unpack_from('<3i', fr['camera']['raw'], 0x10)
        loc = np.array([[v[2], v[3], v[4]] for v in verts], float)
        p = loc @ M.T - np.array(cam, float)
        sx = (160 + 256 * p[:, 0] / p[:, 2]) * 16
        sy = (100 + 256 * 5 / 6 * p[:, 1] / p[:, 2]) * 16                       # scene +0x1c = 0x6aaa = 0x8000 * 5 / 6
        scr = np.array([[v[5], v[6], v[7]] for v in verts], float)
        pos_err = max(np.abs(sx - scr[:, 0]).max(), np.abs(sy - scr[:, 1]).max())
        zerr = np.abs(p[:, 2] * 128 - scr[:, 2]).max()
        # rasterize in face order
        bg = np.frombuffer(fr['bg'], np.uint8).reshape(200, 320)
        img = np.full((200, 320), -1, np.int64)
        tex, shd = TEX[k], SHD[k]
        for (fn, face) in fr.get('drawn', []):
            if fn != 0x4172e0:
                continue
            fo = face - wg['fbase']
            vi = [(struct.unpack_from('<I', wg['faces'], fo + o)[0] - wg['vbase']) // 0x40 for o in (0x14, 0x18, 0x1c)]
            tri = np.array([[scr[n, 0] / 16 - 0.5, scr[n, 1] / 16 - 0.5] for n in vi])   # sampled at pixel centres
            vals = np.array([words[n] for n in vi], float)
            mask, uv = raster(tri, vals)
            if uv is None:
                continue
            u1 = (np.floor(uv[..., 0] / 16).astype(np.int64)) & 255
            v1 = (np.floor(uv[..., 1] / 16).astype(np.int64)) & 255
            c2 = (np.floor(uv[..., 2] / 16).astype(np.int64)) & 255      # +0x2c: column of the second read
            r2 = (np.floor(uv[..., 3] / 16).astype(np.int64)) & 255      # +0x2e: row of the second read
            px = shd[tex[r2, c2].astype(np.int64), tex[v1, u1]]
            img[mask] = px[mask]
        cov = img >= 0
        same = (img == bg) & cov
        print(f"frame {fr['frame']} pic {k}: uv words max err {uv_err}, screen pos err {pos_err:.2f}/16 px, z err "
              f"{zerr:.1f}; covered {cov.mean():.4f}, pixels equal {same.sum() / cov.sum():.4f}")
        if len(sys.argv) > 2 and fr is frames[-1]:
            from PIL import Image
            pal = np.zeros((256, 3), np.uint8)
            Image.fromarray(np.hstack([bg, np.where(cov, img, 0).astype(np.uint8),
                                       ((img != bg) * 255).astype(np.uint8)])).save(sys.argv[2])


if __name__ == '__main__':
    main()


def coverage_over_rotation(steps=3600):
    """Does the warp grid cover the whole 320 x 200 screen at every rotation angle? (camera at (0, 0, -1300),
    grid x, z in -1400 .. 1050, orientation Rx(1.5) Ry(3.0 + a)). Returns the smallest margin in pixels between
    the screen corners and the grid's projected outline."""
    def inside(poly, q):                 # even-odd rule
        n, c = len(poly), False
        for i in range(n):
            (x1, y1), (x2, y2) = poly[i], poly[(i + 1) % n]
            if (y1 > q[1]) != (y2 > q[1]) and q[0] < x1 + (q[1] - y1) * (x2 - x1) / (y2 - y1):
                c = not c
        return c
    worst = 1e9
    edge = [(-1400 + 350 * i, -1400) for i in range(8)] + [(1050, -1400 + 350 * j) for j in range(8)] + \
           [(1050 - 350 * i, 1050) for i in range(8)] + [(-1400, 1050 - 350 * j) for j in range(8)]
    corners = np.array([[0, 0], [320, 0], [320, 200], [0, 200]], float)
    for k in range(steps):
        M = Rx(1.5) @ Ry(3.0 + 2 * np.pi * k / steps)
        pts = []
        for x, z in edge:
            p = M @ np.array([x, 0, z], float) + np.array([0, 0, 1300])
            pts.append((160 + 256 * p[0] / p[2], 100 + 256 * 5 / 6 * p[1] / p[2]))
        if not all(inside(pts, c) for c in corners):
            return -1
        # distance from each corner to the outline
        P = np.array(pts + [pts[0]])
        for c in corners:
            a, b = P[:-1], P[1:]
            t = np.clip(((c - a) * (b - a)).sum(1) / ((b - a) ** 2).sum(1), 0, 1)
            dist = np.sqrt((((a + (b - a) * t[:, None]) - c) ** 2).sum(1)).min()
            worst = min(worst, dist)
    return worst

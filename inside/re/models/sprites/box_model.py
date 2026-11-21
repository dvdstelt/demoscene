"""Model of the shadow scenes (the lit box and the shadow quad) against a dump of models/sprites/run_inside.py.

usage: python3 models/sprites/box_model.py models/sprites/runs/c.dump.pkl [index] [out.png]
Checks, for every dumped frame of the pictures with shadows (index 3 / 4):
 1. the per-vertex light level (vertex word +0x30) from the point light routine 0x41b508:
    level = 112 + sum over lights of trunc-accumulated I * (1 - d / R) * dot(n, L - v) / (512 * d), clamped to 992;
 2. the camera transform and projection: camera space = C^T (world - camera position), C = Rx(a) Ry(b) Rz(c),
    screen = (160 + 256 x / z, 100 + 256 * 5 / 6 * y / z);
 3. the picture: the faces in the order they were drawn, textured + Gouraud through the .qnt table
    (rasterizer 0x414e5c: pixel = qnt[(level >> 4) * 256 + tex[v][u]]) and the shadow quad (0x41669c: pixel =
    sdw[shadow[v][u] * 256 + pixel]), affine, sampled at pixel centres; compared with the draw buffer before the
    sprite is drawn.
"""
import os, pickle, struct, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile
from warp_model import raster

d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
PIC = {3: 9, 4: 4}
TEX = {k: np.frombuffer(d.get(f'nigga/nigga{k}.raw'), np.uint8).reshape(256, 256) for k in (9, 4)}
QNT = {k: np.frombuffer(d.get(f'nigga/nigga{k}.qnt'), np.uint8).reshape(64, 256) for k in (9, 4)}
SDW = {k: np.frombuffer(d.get(f'nigga/shadow{k}.sdw'), np.uint8).reshape(16, 256) for k in (9, 4)}
SHADOW = {9: np.frombuffer(d.get('nigga/shadow9.raw'), np.uint8).reshape(128, 128),
          4: np.frombuffer(d.get('nigga/shadow4.raw'), np.uint8).reshape(256, 256)}
VFMT = '<IIiiiiiiii4hhhiii'      # next, flags, x, y, z (local), sx, sy (28.4), z*128, x*128, y*128*5/6,
                                 # u, v, u2, v2 (words), light (word +0x30), pad, normal x, y, z (819 = 1.0)


def verts_of(o, n):
    return [struct.unpack_from(VFMT, o['verts'], 0x40 * i) for i in range(n)]


def light_levels(fr):
    kb, lr = fr['kuubio'], fr['light']['raw']
    KM = np.array(struct.unpack_from('<9d', kb['raw'], 0x28)).reshape(3, 3)
    kpos = np.array(struct.unpack_from('<3i', kb['raw'], 0x1c), float)
    lw = np.array(struct.unpack_from('<3i', lr, 0x1c), float)
    R, I = struct.unpack_from('<2i', lr, 0xc8)
    ambient = struct.unpack_from('<i', fr['scene'], 0x40)[0]
    L = KM.T @ (lw - kpos)
    out = []
    for v in verts_of(kb, 98):
        lev = ambient
        dv = L - np.array(v[2:5], float)
        dist = np.sqrt(dv @ dv)
        n = np.array(v[16:19], float)
        dot = n @ dv
        if dist <= R and dot >= 0:
            f = dot / (512 * dist)
            c = (I if dist < 1 else I - dist * (I / R)) * f
            if c > 0:
                lev = int(lev + c)
        out.append(min(lev, 0x3e0))
    return np.array(out)


def main():
    frames = pickle.load(open(sys.argv[1], 'rb'))
    sel = [int(sys.argv[2])] if len(sys.argv) > 2 else range(len(frames))
    for idx in sel:
        fr = frames[idx]
        if fr['pic'] < 3:
            continue
        k = PIC[fr['pic']]
        kb, sp = fr['kuubio'], fr['shadepoly']
        kv = verts_of(kb, 98)
        vis = np.array([(v[1] >> 1) & 1 for v in kv]) == 1
        got = np.array([v[14] for v in kv])
        lev = light_levels(fr)
        lerr = np.abs(got - lev)[vis].max()
        # camera transform check
        cam = fr['camera']['raw']
        C = np.array(struct.unpack_from('<9d', cam, 0x28)).reshape(3, 3)
        cp = np.array(struct.unpack_from('<3i', cam, 0x10), float)
        KM = np.array(struct.unpack_from('<9d', kb['raw'], 0x28)).reshape(3, 3)
        loc = np.array([v[2:5] for v in kv], float)
        p = (loc @ KM.T - cp) @ C
        sx, sy = (160 + 256 * p[:, 0] / p[:, 2]) * 16, (100 + 256 * 5 / 6 * p[:, 1] / p[:, 2]) * 16
        front = vis & (p[:, 2] > 44)
        perr = max(np.abs(sx - [v[5] for v in kv])[front].max(), np.abs(sy - [v[6] for v in kv])[front].max())
        # picture: every rasterizer call in order, from the recorded face / vertex / material copies
        bg = np.frombuffer(fr['bg'], np.uint8).reshape(200, 320)
        img = np.full((200, 320), -1, np.int64)
        near = struct.unpack_from('<i', fr['scene'], 0x44)[0]
        n_clip = 0
        for fn, face, fb, vbs, mb in fr.get('draws', []):
            vs = [struct.unpack(VFMT, vb) for vb in vbs]
            if struct.unpack_from('<I', fb, 0x30)[0] & 0x100000 and min(v[7] for v in vs) < near:
                n_clip += 1      # crosses the near plane: the clipper draws its pieces as separate calls
                continue
            mat = struct.unpack('<17i', mb)
            tri = np.array([[v[5] / 16 - 0.5, v[6] / 16 - 0.5] for v in vs])
            if fn == 0x414e5c:
                if mat[6] & 1:   # material +0x18 bit 0: the corners' (u, v) come from the material +0x20..+0x34
                    uv = [(mat[8], mat[9]), (mat[10], mat[11]), (mat[12], mat[13])]
                else:
                    uv = [(v[10], v[11]) for v in vs]
                vals = np.array([[uv[q][0], uv[q][1], vs[q][14]] for q in range(3)], float)
                mask, a = raster(tri, vals)
                if a is None:
                    continue
                u = (np.floor(a[..., 0] / 16).astype(np.int64)) & 255
                v = (np.floor(a[..., 1] / 16).astype(np.int64)) & 255
                L = np.clip(np.floor(a[..., 2] / 16).astype(np.int64), 0, 63)
                px = QNT[k][L, TEX[k][v, u]]
            else:
                vals = np.array([[v[10], v[11]] for v in vs], float)
                mask, a = raster(tri, vals)
                if a is None:
                    continue
                sh = SHADOW[k]
                lg = 7 if sh.shape[0] == 128 else 8
                u = (np.floor(a[..., 0] / 16).astype(np.int64) >> (8 - lg)) & (sh.shape[0] - 1)
                v = (np.floor(a[..., 1] / 16).astype(np.int64) >> (8 - lg)) & (sh.shape[0] - 1)
                under = np.where(img >= 0, img, bg)
                px = SDW[k][sh[v, u], under]
            img[mask] = px[mask]
        cov = img >= 0
        eq = ((img == bg) & cov).sum() / max(cov.sum(), 1)
        print(f"frame {fr['frame']} pic {fr['pic']}: light level max err {lerr} ({vis.sum()} vertices), screen err "
              f"{perr:.2f}/16 px, covered {cov.mean():.4f}, equal {eq:.4f}, faces crossing the near plane {n_clip}")
        if len(sys.argv) > 3:
            from PIL import Image
            Image.fromarray(np.hstack([bg, np.where(cov, img, 0).astype(np.uint8),
                                       ((img != bg) * 255).astype(np.uint8)])).save(sys.argv[3])


if __name__ == '__main__':
    main()

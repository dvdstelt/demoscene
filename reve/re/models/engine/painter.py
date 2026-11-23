"""Does anything visible depend on the painter's order? Renders sampled frames of a 3D entry twice with the exact
model: as the original (objects and faces sorted, drawn back to front) and with a per-pixel depth test (1/z
interpolated linearly in screen space over each face; new near-clip vertices at z = 128), and counts the pixels of the
back buffer that differ.

usage: python3 painter.py ENTRY [--every N] [--png DIR]
"""
import argparse
import os

import engine
from model_part import PARTS
from tables import raw_picture


class DepthBuffer(bytearray):
    """The back buffer with a depth test: a write lands only where the current face is nearer."""

    def __init__(self):
        super().__init__(64000)
        self.w = [0.0] * 64000
        self.plane = None

    def _ok(self, i):
        A, B, C = self.plane
        y, x = divmod(i, 320)
        w = A * (x + 0.5) + B * (y + 0.5) + C
        if w >= self.w[i]:
            self.w[i] = w
            return True
        return False

    def __setitem__(self, k, v):
        if isinstance(k, slice):
            for j, i in enumerate(range(*k.indices(64000))):
                if self._ok(i):
                    bytearray.__setitem__(self, i, v[j])
        elif self._ok(k):
            bytearray.__setitem__(self, k, v)


class DepthRenderer(engine.Renderer):
    def near_clip(self, f, pool):
        faces = engine.Renderer.near_clip(self, f, pool)
        for n in pool:
            n.ztrue = 128
        return faces

    def draw_face(self, f, L):
        if isinstance(self.buf, DepthBuffer):
            p = []
            for v in (f.a, f.b, f.c):
                z = getattr(v, 'ztrue', None) or v.zc32 or v.zc
                p.append((v.sx + 0.5, v.sy + 0.5, 1.0 / max(z, 1)))
            (x0, y0, w0), (x1, y1, w1), (x2, y2, w2) = p
            det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
            if det == 0:
                self.buf.plane = (0.0, 0.0, max(w0, w1, w2))
            else:
                A = ((w1 - w0) * (y2 - y0) - (w2 - w0) * (y1 - y0)) / det
                B = ((x1 - x0) * (w2 - w0) - (x2 - x0) * (w1 - w0)) / det
                self.buf.plane = (A, B, w0 - A * x0 - B * y0)
        engine.Renderer.draw_face(self, f, L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('entry', type=int)
    ap.add_argument('--every', type=int, default=150)
    ap.add_argument('--png')
    a = ap.parse_args()
    name, start, end = PARTS[a.entry]
    world = engine.World(engine.arch_file('3D/%s.WLD' % name))
    an = engine.Anim(engine.arch_file('3D/%s.ANI' % name))
    an.reset(world)
    an.step()
    tex = raw_picture('GFX/TEXTURE.RAW')[3]
    gour = engine.arch_file('GFX/GOURAUD.PAL')
    pal = raw_picture('GFX/TEXTURE.RAW')[2] >> 2
    first = {1: 652, 2: 1327, 3: 1953, 4: 2371}.get(a.entry, start)
    n = 0
    tot = []
    for R in range(first + 4, end - 1, a.every):
        while n < R - start:
            an.step()
            n += 1

        class S:
            pass
        st = S()
        st.cam = an.cam
        r = DepthRenderer(world, tex, gour, part4=(a.entry == 22))
        p = r.render(st)
        r.boxes = lambda: None
        z = r.render(st, DepthBuffer())
        if a.entry == 22:          # the boxes are drawn after everything, the same in both
            p = r.render(st)       # (render again without boxes for a like-for-like comparison)
        diff = sum(1 for x, y in zip(p, z) if x != y)
        # how far apart in colour (6-bit RGB distance) the differing pixels are
        big = sum(1 for x, y in zip(p, z) if x != y and sum(abs(int(pal[x][k]) - int(pal[y][k])) for k in range(3)) > 12)
        cover = sum(1 for x in p if x)
        tot.append((diff, big, cover))
        print('record %d (n %d): %d pixels differ (%d by more than 12/63 summed RGB), %d covered' % (
            R, n, diff, big, cover), flush=True)
        if a.png and diff:
            from PIL import Image
            import numpy as np
            P = np.frombuffer(bytes(p), np.uint8).reshape(200, 320)
            Z = np.frombuffer(bytes(z), np.uint8).reshape(200, 320)
            lut = (pal.astype(np.uint32) * 255 + 31) // 63
            img = np.concatenate([lut[P], lut[Z], np.stack([(P != Z) * 255] * 3, -1)], 1).astype(np.uint8)
            Image.fromarray(img).save(os.path.join(a.png, 'painter_%d_%d.png' % (a.entry, R)))
    d = sum(t[0] for t in tot)
    b = sum(t[1] for t in tot)
    c = sum(t[2] for t in tot)
    print('%s entry %d: %d frames, %.3f %% of the drawn pixels differ, %.3f %% by more than 12/63' % (
        name, a.entry, len(tot), 100.0 * d / max(c, 1), 100.0 * b / max(c, 1)))


if __name__ == '__main__':
    main()

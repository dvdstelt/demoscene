"""Compare the model's render with the original's: for every pair of dumps (render start 0x25f5b5, render end
0x25f65b) in a folder, rebuild the state from the start dump, render it with the model and compare the transformed
vertices and the back buffer [0x205e8c] with the end dump.

usage: python3 test_render.py DUMPDIR WLDNAME [--png out.png]
"""
import argparse
import sys

import dumps
import engine


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder')
    ap.add_argument('wld')
    ap.add_argument('--png')
    a = ap.parse_args()
    total = 0
    for ps, pe in dumps.pairs(a.folder):
        ds, de = dumps.Dump(ps), dumps.Dump(pe)
        world = engine.World(engine.arch_file('3D/%s.WLD' % a.wld))
        cam, camf = dumps.state(ds, world)
        r = dumps.renderer(ds, world)

        class A:
            pass
        an = A()
        an.cam = cam
        buf = r.render(an)
        # vertices of the objects drawn
        vbad = vtot = 0
        for o in r.drawn_objects:
            got = dumps.vertices(de, o)
            for v, g in zip(o.verts, got):
                mine = (v.xc, v.yc, v.zc)
                if g[9] == 0 and v.clip == 0:
                    mine2 = (v.xc, v.yc, v.zc, v.sx, v.sy)
                    vtot += 1
                    if mine2 != g[:5]:
                        vbad += 1
        ref = de.buf(0x205e8c)
        diff = sum(1 for x, y in zip(buf, ref) if x != y)
        total += diff
        print('render at refresh %d (drawn %d): vertices %d/%d differ, pixels %d differ, faces drawn %d %s' % (
            ds.frame, ds.drawn, vbad, vtot, diff, r.drawn_faces, r.stats))
        if a.png and diff:
            from PIL import Image
            img = Image.new('L', (640, 200))
            img.paste(Image.frombytes('L', (320, 200), bytes(buf)), (0, 0))
            img.paste(Image.frombytes('L', (320, 200), bytes(ref)), (320, 0))
            img.save(a.png)
    print('total pixels different', total)


if __name__ == '__main__':
    main()

"""Reproduce the logo part's 3D render (logo/obuke.sro inside-out, lit by a point light, texture logo/twlogo.raw
through logo/logo.shd, rasterizer 0x414e5c) with engine.py and compare it with the original.

Inputs: out/logo_dump.pkl from run_engine.py --hooks hooks_logo.py (the engine state before each render and the
frame buffer after it). Only the inputs the parts set are taken from the dump: the object's rotation matrix
(the logo's tick routine rotates it), the camera, the light and the scene parameters; vertices, normals, lighting,
projection, sorting, clipping and rasterizing are the model's.

usage: python3 logo_model.py [--frames N] [--png out.png]
"""
import argparse
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile  # noqa: E402
import engine as E  # noqa: E402
import sro  # noqa: E402


def i32(b, o): return struct.unpack_from('<i', b, o)[0]
def f64(b, o): return struct.unpack_from('<d', b, o)[0]
def mat(b, o): return [[E.L(f64(b, o + 8 * (3 * r + c))) for c in range(3)] for r in range(3)]


def render(d, dat, verts, faces, fnorm, vnorm, check=None):
    sc_b, cam_b, obj_b, li_b = d['scene'], d['cam'], d['obj'], d['light']
    sc = E.Scene(width=i32(sc_b, 0x20), height=i32(sc_b, 0x24), x0=i32(sc_b, 0x28), x1=i32(sc_b, 0x2c),
                 y0=i32(sc_b, 0x30), y1=i32(sc_b, 0x34), fov=i32(sc_b, 0x3c), near=i32(sc_b, 0x44),
                 ambient=E.s16(i32(sc_b, 0x40)), flags=i32(sc_b, 0x4c) & 0xff)
    cam_pos = [i32(cam_b, 0x10 + 4 * k) for k in range(3)]      # no parents: world = local
    obj_pos = [i32(obj_b, 0x10 + 4 * k) for k in range(3)]
    cam_m, obj_m = mat(cam_b, 0x28), mat(obj_b, 0x28)
    M, T = E.object_to_camera(cam_m, cam_pos, obj_m, obj_pos)
    vs = E.make_vertices(verts, vnorm)
    vis = E.backface(vs, faces, fnorm, obj_m, obj_pos, cam_pos)
    used = {k for f, v in zip(faces, vis) if v for k in f}
    E.project(vs, used, M, T, sc)
    lst = E.face_list(vs, faces, vis, bias=i32(obj_b, 0xcc))
    used2 = {p.idx for F in lst for p in F.v}
    radius = i32(obj_b, 0xf8)
    lights = [([i32(li_b, 0x10 + 4 * k) for k in range(3)], [i32(li_b, 0x10 + 4 * k) for k in range(3)],
               i32(li_b, 0xc8), i32(li_b, 0xcc))]
    E.light_vertices(vs, used2, obj_m, obj_pos, obj_pos, radius, lights, sc.ambient)
    if check is not None:
        check(vs, used2, lst, T)
    buf = np.zeros((200, 320), np.uint8)             # 0x41127c clears the viewport to 0 first
    tex = np.frombuffer(dat.get('logo/twlogo.raw'), np.uint8).astype(np.int64)
    shd = np.frombuffer(dat.get('logo/logo.shd'), np.uint8)
    draw = E.draw_tex_gouraud(buf, tex, shd, sc)
    for F in E.sort_faces(lst):
        E.clip_and_draw(F.v, sc, draw)
    return buf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dump', default=os.path.join(HERE, 'out', 'logo_dump.pkl'))
    ap.add_argument('--frames', type=int, default=1000)
    ap.add_argument('--png', default='')
    ap.add_argument('--final', action='store_true', help='also draw the mask and text bitmaps (0x402f1c) and '
                    'compare with the finished frame')
    a = ap.parse_args()
    dat = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
    verts, faces = sro.load(dat.get('logo/obuke.sro'))
    fnorm, vnorm = sro.normals(verts, faces)
    D = pickle.load(open(a.dump, 'rb'))
    pairs = [(D[i], D[i + 1], D[i + 2]) for i in range(len(D) - 2)
             if D[i]['when'] == 'before' and D[i + 1]['when'] == 'after' and D[i + 2]['when'] == 'final']
    inmask, inside = dat.get('logo/inmask.raw'), dat.get('logo/inside.raw')
    mshd = np.frombuffer(dat.get('logo/inmask.shd'), np.uint8)
    fbad = 0
    tot = bad_total = 0
    for n, (b, af, fi) in enumerate(pairs[:a.frames]):
        ob = af['obj']
        base = af['obj_addr']
        nv = i32(ob, 0xd4)
        stats = {}

        def check(vs, used, lst, T):
            bad = 0
            for p in vs:
                if p.idx not in used:
                    continue
                o = 0x10c + 0x40 * p.idx
                orig = (i32(ob, o + 0x14), i32(ob, o + 0x18), i32(ob, o + 0x1c), i32(ob, o + 0x20), i32(ob, o + 0x24),
                        struct.unpack_from('<h', ob, o + 0x30)[0])
                mine = (p.sx if p.sx is not None else orig[0], p.sy if p.sy is not None else orig[1], p.zc, p.px,
                        p.py, p.shade)
                if orig != mine:
                    bad += 1
                    if bad <= 3:
                        print('  vertex', p.idx, 'orig', orig, 'model', mine)
            keys = 0
            for F in lst:
                fo = 0x10c + 0x40 * nv + 0x34 * F.idx
                if i32(ob, fo + 4) != F.key:
                    keys += 1
            stats['vbad'], stats['keys'], stats['T'] = bad, keys, (T, [i32(ob, 0xb8 + 4 * k) for k in range(3)])

        buf = render(b, dat, verts, faces, fnorm, vnorm, check)
        orig = np.frombuffer(af['buf'], np.uint8).reshape(200, 320)
        diff = int((buf != orig).sum())
        if a.final:                                   # 0x402f1c: mask (type 4) at (31, 9), text (type 3) at (34, 13)
            E.blit(buf, 4, inmask, 253, 253, 147, 31.0, 9.0, table=mshd)
            if fi['flag42553c']:
                E.blit(buf, 3, inside, 249, 249, 139, 34.0, 13.0)
            fo = np.frombuffer(fi['buf'], np.uint8).reshape(200, 320)
            fd = int((buf != fo).sum())
            fbad += fd
            print('   finished frame (text %d): pixels differing %d' % (fi['flag42553c'], fd))
        tot += 1
        bad_total += diff
        print('frame %d: vertices differing %d, sort keys differing %d, T %s, pixels differing %d' % (
            b['frame'], stats['vbad'], stats['keys'], stats['T'][0] == stats['T'][1], diff))
        if a.png and n == 0:
            from PIL import Image
            pal = (np.frombuffer(dat.get('logo/logo.pal'), np.uint8).reshape(256, 3).astype(np.int32) * 255 // 63)
            img = np.concatenate([pal[orig], pal[buf], np.where((buf != orig)[..., None], 255, 0) *
                                  np.ones(3, np.int32)], axis=1).astype(np.uint8)
            Image.fromarray(img).resize((960 * 2, 400)).save(a.png)
    print('%d frames, %d pixels differ in total (%.4f%%)' % (tot, bad_total, 100.0 * bad_total / (tot * 64000)))
    if a.final:
        print('finished frames: %d pixels differ in total' % fbad)


if __name__ == '__main__':
    main()

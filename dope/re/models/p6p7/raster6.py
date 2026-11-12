"""p6: re-render the 3D layer of a frame from the engine buffers dumped at the start of the drawing loop (2a60) and
compare it with the frame buffer dumped when the loop ends (2d5c). Approximate rasteriser (affine interpolation at
pixel positions, top-left rule); the point is the texel addressing of the two fillers:
  bump-env (1fd1/1db0): t = bump[row = ((u1>>8)&31)*4 | (v1>>14), col = (v1>>6)&127]  (u1/v1 = mesh UV, 8.8)
                        (dy, dx) = table[t] (cs:0010), colour = env[(u2>>8) + dy, (v2>>8) + dx] (u2/v2 = env coords)
  plain env (2780/25f0): colour = env[u2>>8, v2>>8]
usage: raster6.py dumps/0F74_2A60_<F>.bin dumps/0F74_2D5C_<F>.bin [code|plain [out.png]]"""
import struct
import sys
import numpy as np
from PIL import Image

L = 0xF64


def lin(seg, off=0): return (seg + L) * 16 + off


def main():
    a = open(sys.argv[1], 'rb').read()
    bfin = open(sys.argv[2], 'rb').read()
    variant = sys.argv[3] if len(sys.argv) > 3 else 'code'
    png = sys.argv[4] if len(sys.argv) > 4 else None     # optional side-by-side image (engine | model | diff)
    gs = lin(0x667b)
    cs = lin(0x10)
    def w16(base, off): return struct.unpack_from('<H', a, base + off)[0]
    def d32(base, off): return struct.unpack_from('<I', a, base + off)[0]
    table = [struct.unpack_from('<H', a, cs + 0x10 + 2 * i)[0] for i in range(256)]
    tex_segs = [struct.unpack_from('<H', a, cs + 0x210 + 2 * i)[0] for i in range(4)]
    bump_segs = [struct.unpack_from('<H', a, cs + 0x218 + 2 * i)[0] for i in range(4)]
    fbseg = struct.unpack_from('<H', a, cs + 0x177a)[0]
    out = np.frombuffer(a, np.uint8, 320 * 200, fbseg * 16 + 0x140).reshape(200, 320).copy()   # background
    ref = np.frombuffer(bfin, np.uint8, 320 * 200, fbseg * 16 + 0x140).reshape(200, 320)
    mem = a
    i = 0
    nfaces = 0
    while True:
        e = d32(gs, 0x3200 + 4 * i)
        if e & 0xffff == 0xffff:
            break
        f = e & 0xffff
        i += 1
        nfaces += 1
        v = [w16(gs, 0x5140 + 2 * f), w16(gs, 0x5910 + 2 * f), w16(gs, 0x60e0 + 2 * f)]
        bsel = v[1] & 3
        tsel = v[2] & 3
        v = [x & 0xfffc for x in v]
        fsg = bump_segs[bsel]
        dsg = tex_segs[tsel]
        P = []
        for x in v:
            scr = d32(gs, 0x1900 + x)
            sy = struct.unpack('<h', struct.pack('<H', scr & 0xffff))[0]
            sx = struct.unpack('<h', struct.pack('<H', scr >> 16))[0]
            t = d32(gs, x)
            en = d32(gs, 0xc80 + x)
            P.append((sx, sy, t & 0xffff, t >> 16, en & 0xffff, en >> 16))
        draw(out, P, mem, fsg, dsg, table, variant)
    diff = out != ref
    fg = ref != np.frombuffer(a, np.uint8, 320 * 200, fbseg * 16 + 0x140).reshape(200, 320)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            sh = np.roll(np.roll(out, dy, 0), dx, 1)
            print('shift', dy, dx, 'identical on 3D pixels %.3f' % float((sh == ref)[fg].mean()))
    if len(a) >= 0x110300:
        pal6 = np.frombuffer(a[0x110000:0x110300], np.uint8).reshape(256, 3).astype(int)
        print('mean |RGB diff| (0..63) on 3D pixels: %.2f' % float(np.abs(pal6[out] - pal6[ref])[fg].mean()))
    print('faces', nfaces, 'pixels differing', int(diff.sum()), 'of', diff.size,
          'mean |index diff| on 3D pixels', float(np.abs(out.astype(int) - ref.astype(int))[ref >= 16].mean()))
    pal = np.frombuffer(a[0x110000:0x110300], np.uint8).reshape(256, 3) if len(a) >= 0x110300 else None
    if pal is not None and png:
        p = ((pal.astype(np.uint16) * 255 + 31) // 63).astype(np.uint8)
        img = np.concatenate([p[ref], p[out], (diff[..., None] * 255).repeat(3, 2).astype(np.uint8)], 1)
        Image.fromarray(img).resize((960 * 2, 400)).save(png)


def draw(out, P, mem, fsg, dsg, table, variant):
    xs = [p[0] for p in P]; ys = [p[1] for p in P]
    y0, y1 = max(min(ys), 0), min(max(ys), 200)
    (x0, yA, *a0), (x1, yB, *a1), (x2, yC, *a2) = P
    den = (x1 - x0) * (yC - yA) - (x2 - x0) * (yB - yA)
    if den == 0:
        return
    for y in range(y0, y1):
        # span: intersect the three edges with the scanline y
        xsl = []
        for (pa, pb) in ((P[0], P[1]), (P[1], P[2]), (P[2], P[0])):
            if (pa[1] <= y < pb[1]) or (pb[1] <= y < pa[1]):
                xsl.append(pa[0] + (y - pa[1]) * (pb[0] - pa[0]) / (pb[1] - pa[1]))
        if len(xsl) < 2:
            continue
        xl, xr = int(np.ceil(min(xsl))), int(np.ceil(max(xsl)))
        for x in range(max(xl, 0), min(xr, 320)):
            l1 = ((x - x0) * (yC - yA) - (x2 - x0) * (y - yA)) / den
            l2 = ((x1 - x0) * (y - yA) - (x - x0) * (yB - yA)) / den
            att = [a0[k] + l1 * (a1[k] - a0[k]) + l2 * (a2[k] - a0[k]) for k in range(4)]
            u1, v1, u2, v2 = [int(t) & 0xffff for t in att]
            if fsg:
                if variant == 'code':
                    row = (((u1 >> 8) & 31) << 2) | (v1 >> 14)
                    col = (v1 >> 6) & 127
                elif variant == 'plain':
                    row, col = (u1 >> 8) & 127, (v1 >> 8) & 127
                t = mem[fsg * 16 + row * 256 + col]
                o = table[t]
                bx = ((((u2 >> 8) & 0xff) << 8) | ((v2 >> 8) & 0xff)) + o & 0xffff
                c = mem[dsg * 16 + bx]
            else:
                c = mem[dsg * 16 + (((u2 >> 8) & 0xff) << 8 | ((v2 >> 8) & 0xff))]
            if y >= 1:
                out[y - 1, x] = c       # the engine's buffer row y is screen row y - 1 (the copy starts at offset 0x140)


main()

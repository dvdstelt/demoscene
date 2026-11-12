"""p7: re-render the tentacle object from the engine buffers dumped where the drawing loop 2c90 starts and compare with
the frame buffer dumped at 2f8c (approximate rasteriser like raster6.py). Texel addressing (verified by probes):
  t = bump[(u1 >> 8) & 127, (v1 >> 8) & 127]  (u1/v1 = object-space normal x / y, 8.8, wrapped)
  colour = lightmap[64 + (u2 >> 8) + dy, 64 + (v2 >> 8) + dx], (dy, dx) from the static table cs:0000
usage: raster7.py dumps/0F74_2C90_<F>.bin dumps/0F74_2F8C_<F>.bin [out.png]"""
import struct
import sys
import numpy as np
from PIL import Image

L = 0xF64


def main():
    a = open(sys.argv[1], 'rb').read()
    bfin = open(sys.argv[2], 'rb').read()
    cs = (0x10 + L) * 16
    gs = (0x3de1 + L) * 16
    def w16(base, off): return struct.unpack_from('<H', a, base + off)[0]
    def d32(base, off): return struct.unpack_from('<I', a, base + off)[0]
    table = [w16(cs, 2 * i) for i in range(256)]
    tex_segs = [w16(cs, 0x400 + 2 * i) for i in range(4)]
    bump_segs = [w16(cs, 0x408 + 2 * i) for i in range(4)]
    fbseg = w16(cs, 0x19d4)
    bg = np.frombuffer(a, np.uint8, 320 * 200, fbseg * 16 + 0x140).reshape(200, 320)
    out = bg.copy()
    ref = np.frombuffer(bfin, np.uint8, 320 * 200, fbseg * 16 + 0x140).reshape(200, 320)
    i = 0
    while True:
        e = d32(gs, 4 * i)
        if e & 0xffff == 0xffff:
            break
        f = e & 0xffff
        i += 1
        v = [w16(gs, (2 * f - 0x63c0) & 0xffff), w16(gs, (2 * f - 0x5420) & 0xffff), w16(gs, (2 * f - 0x4480) & 0xffff)]
        fsg, dsg = bump_segs[v[1] & 3], tex_segs[v[2] & 3]
        P = []
        for x in (x & 0xfffc for x in v):
            scr = d32(gs, 0x6d60 + x)
            sy = struct.unpack('<h', struct.pack('<H', scr & 0xffff))[0]
            sx = struct.unpack('<h', struct.pack('<H', scr >> 16))[0]
            t = d32(gs, 0x3e80 + x)
            en = d32(gs, 0x55f0 + x)
            P.append((sx, sy, t & 0xffff, t >> 16, en & 0xffff, en >> 16))
        draw(out, P, a, fsg, dsg, table)
    fg = ref != bg
    pal = np.frombuffer(a[0x110000:0x110300], np.uint8).reshape(256, 3).astype(int)
    print('faces', i, '3D pixels', int(fg.sum()), 'identical %.3f' % float((out == ref)[fg].mean()),
          'mean |RGB diff| (0..63) %.2f' % float(np.abs(pal[out] - pal[ref])[fg].mean()))
    p = ((pal * 255 + 31) // 63).astype(np.uint8)
    if len(sys.argv) > 3:
        Image.fromarray(np.concatenate([p[ref], p[out]], 1)).resize((1280, 400)).save(sys.argv[3])


def draw(out, P, mem, fsg, dsg, table):
    ys = [p[1] for p in P]
    (x0, yA, *a0), (x1, yB, *a1), (x2, yC, *a2) = P
    den = (x1 - x0) * (yC - yA) - (x2 - x0) * (yB - yA)
    if den == 0:
        return
    for y in range(max(min(ys), 1), min(max(ys), 201)):
        xsl = []
        for (pa, pb) in ((P[0], P[1]), (P[1], P[2]), (P[2], P[0])):
            if (pa[1] <= y < pb[1]) or (pb[1] <= y < pa[1]):
                xsl.append(pa[0] + (y - pa[1]) * (pb[0] - pa[0]) / (pb[1] - pa[1]))
        if len(xsl) < 2:
            continue
        for x in range(max(int(np.ceil(min(xsl))), 0), min(int(np.ceil(max(xsl))), 320)):
            l1 = ((x - x0) * (yC - yA) - (x2 - x0) * (y - yA)) / den
            l2 = ((x1 - x0) * (y - yA) - (x - x0) * (yB - yA)) / den
            u1, v1, u2, v2 = [int(a0[k] + l1 * (a1[k] - a0[k]) + l2 * (a2[k] - a0[k])) & 0xffff for k in range(4)]
            t = mem[fsg * 16 + ((u1 >> 8) & 127) * 256 + ((v1 >> 8) & 127)]
            bx = (((u2 >> 8) << 8) | (v2 >> 8)) + table[t] & 0xffff
            out[y - 1, x] = mem[dsg * 16 + bx]


main()

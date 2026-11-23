"""How much the in-place order of the feedback blend 0x25eaec matters (entries 12 and 13).

usage: python3 inplace.py OUT.png [--from 8400] [--frames 120]

Runs spiral.py's model from a snapshot twice: as the original (in place, rows from the top, so rows below the
centre read sources this pass has already blended) and double-buffered (every source read from the previous frame),
and writes the two results next to each other with the capture (left: capture, middle: in place, right: double
buffered), at 2x.
"""
import argparse

import numpy as np
from PIL import Image

from common import Mem, snap, frames, indexed, raw, file, pal6_to_rgb
import spiral


def feedback_buffered(buf, zoom, table):
    step = (zoom << 22) & 0xffffffff
    h = step >> 16
    edx = (0x1400000 - 320 * h) & 0xffffffff
    imm = spiral.s32(edx) >> 17
    x0 = edx >> 1
    edx = (0xc80000 - 200 * h) & 0xffffffff
    imm += (spiral.s32(edx) >> 17) * 320
    y0 = edx >> 1
    src = buf.reshape(-1).copy()
    i = np.arange(320, dtype=np.int64)
    j = np.arange(200, dtype=np.int64)
    idx = imm + 320 * ((y0 + j[:, None] * step) >> 32) + ((x0 + i[None, :] * step) >> 32)
    s = src[idx].astype(np.int64)
    buf[:] = table[(s << 8) | buf.astype(np.int64)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--from', dest='frm', type=int, default=8400)
    ap.add_argument('--frames', type=int, default=120)
    a = ap.parse_args()
    raw_frames, n = frames()
    m = Mem(snap('snaps/f%d.snap' % a.frm))
    tpx, tpal = raw('GFX\\TEXTURE.RAW')
    tex16 = tpx[80:96, 16:32].reshape(-1)
    table = np.frombuffer(file('GFX\\SPIRBLUR.PAL'), np.uint8)
    spx, _ = raw('GFX\\SPIRALE.RAW')
    dot = spx[:6, :6]
    s1, s2 = spiral.Spiral(m), spiral.Spiral(m)
    b1 = m.arr(m.u32(0x205e8c), 64000).reshape(200, 320)
    b2 = b1.copy()
    for k in range(a.frames):
        for s, b, fb in ((s1, b1, spiral.feedback), (s2, b2, feedback_buffered)):
            s.retrace()
            spiral.layer1(b, s.roto, s.a, tex16)
            fb(b, s.zoom, table)
            spiral.dots(b, s.qprev, dot)
    px, dac = indexed(raw_frames, a.frm + a.frames)
    lut = pal6_to_rgb(tpal >> 2)
    print('in place vs capture: %d pixels differ; double buffered vs capture: %d' % ((b1 != px).sum(), (b2 != px).sum()))
    img = np.concatenate([lut[px], lut[b1], lut[b2]], axis=1)
    Image.fromarray(img).resize((img.shape[1] * 2, img.shape[0] * 2), Image.NEAREST).save(a.out)


if __name__ == '__main__':
    main()

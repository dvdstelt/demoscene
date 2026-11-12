"""Run the engine model on a 'pre' dump (start of the frame build) and compare with the 'drawn' dump (just before the
frame copy): transformed vertices, face list, sorted keys and the rendered target buffer.

usage: python3 models/engine/check_frame.py <part> <dumpdir>/fNNNNN [--png out.png]
"""
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dumpview import Dump
from engine_model import Engine, PARTS


def main():
    part, stem = sys.argv[1], sys.argv[2]
    png = sys.argv[sys.argv.index('--png') + 1] if '--png' in sys.argv else None
    import glob
    f = lambda name: (glob.glob(stem + '_*_%s.npz' % name) or [None])[0]
    pre = Dump(f('pre'), part)
    mid = Dump(f('mid'), part) if f('mid') else pre
    drawn = Dump(f('drawn'), part)
    c = PARTS[part]
    e = Engine(pre, part, mid)
    r0, r1 = pre.cs32(c['retraces']), drawn.cs32(c['retraces'])
    print('frame %d: retrace counter %d at the start, %d at the end%s' % (
        pre.frame, r0, r1, '' if r0 == r1 else ' (an IRQ ran during the build: object state may differ)'))
    tseg = e.target_seg
    e.buf[:] = mid.b(tseg, 0, 0x10000)
    e.build()
    W = c['work']
    nv, nf = drawn.cs16(c['nvars'][0]), drawn.cs16(c['nvars'][1])
    print('vertices %d (engine %d), visible faces %d (engine %d)' % (e.nvert, nv, e.nface, nf))
    bad = {'tex': 0, 'env': 0, 'scr': 0, 'dep': 0}
    for i in range(nv):
        g = 4 * i
        if drawn.u32(W, c['tex'] + g) != e.vtex[g]:
            bad['tex'] += 1
        if drawn.u32(W, c['env'] + g) != e.venv[g]:
            bad['env'] += 1
        if (drawn.s16(W, c['scr'] + g + 2), drawn.s16(W, c['scr'] + g)) != e.vscr[g]:
            bad['scr'] += 1
        if drawn.s16(W, c['dep'] + g) != e.vdep[g]:
            bad['dep'] += 1
    print('vertex arrays: mismatches', bad)
    fb = 0
    for k, (key, A, B, C) in enumerate(e.faces):
        got = (drawn.u16(W, c['fa'] + 2 * k), drawn.u16(W, c['fb'] + 2 * k), drawn.u16(W, c['fc'] + 2 * k))
        if got != (A, B, C):
            fb += 1
    print('face arrays: mismatches', fb)
    keys = e.sort()
    got = [drawn.u32(W, c['keys'] + 4 * k) for k in range(nf + 1)]
    print('sorted keys: mismatches', sum(1 for a, b in zip(keys + [0xffffffff], got) if a != b),
          'terminator ok' if got[-1] == 0xffffffff else 'terminator %08x' % got[-1])
    e.draw()
    mine = np.frombuffer(bytes(e.buf), np.uint8)[:64320].reshape(201, 320)
    ref = np.frombuffer(drawn.b(tseg, 0, 64320), np.uint8).reshape(201, 320)
    diff = mine != ref
    print('target buffer: %d of %d pixels differ' % (diff.sum(), diff.size))
    if diff.any():
        ys, xs = np.nonzero(diff)
        print('  first differences (y, x, model, emulator):',
              [(int(y), int(x), int(mine[y, x]), int(ref[y, x])) for y, x in list(zip(ys, xs))[:12]])
    if png:
        from PIL import Image
        im = np.concatenate([drawn.rgb(ref), drawn.rgb(mine),
                             np.where(diff[..., None], np.uint8([255, 0, 255]), np.uint8([0, 0, 0]))], 1)
        Image.fromarray(im).resize((im.shape[1] * 2, im.shape[0] * 2), 0).save(png)


if __name__ == '__main__':
    main()

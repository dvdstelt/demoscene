"""Exports one frame of the bit-exact engine model (dope/re/models/engine/engine_model.py) for `enginetest model`:
the object records, the maps, bump table and DAC the frame was drawn with (from the emulator's memory dumps), the
model's per-vertex arrays and face list, and the render target before and after the draw (the model reproduces the
emulator's target with 0 differing pixels, notes-engine 14).

    cd dope/re && python3 ../remake/tools/enginetest/export_model.py p2 models/engine/dumps_p2/f07500 OUT.bin

Run inside the toolbox (numpy). The dumps are generated from the original (models/engine/README.md); OUT goes to a
scratch directory, never into the repository.

Format (little-endian): b'DOPEENG1', u32 part, u32 frame, u32 objs (cs offset of slot 0), u32 objlast, u32 n,
n records of 0x3a bytes (mesh segments as image segments), u8 global env add, u8 global texture add, 4 x u16 env
segments, 4 x u16 texture segments (0: none), 256 x s16 bump table, 768 bytes DAC (6-bit), 4 env maps and 4 textures
of 128 x 128 bytes (the window's columns 0..127 of rows 0..127; zeros for segment 0), u32 nv, nv x (s16 sx, s16 sy,
u32 env, u32 tex, s16 depth), u32 nf, nf x (u16 A, u16 B, u16 C) in slot order, u32 ns, ns x u16 slot in sorted
order, 320 x 201 bytes target before the draw, 320 x 201 bytes after (the model's).
"""
import glob
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.getcwd(), 'models', 'engine'))
from dumpview import Dump  # noqa: E402
from engine_model import Engine, PARTS  # noqa: E402


def main():
    part, stem, out = sys.argv[1], sys.argv[2], sys.argv[3]
    f = lambda name: (glob.glob(stem + '_*_%s.npz' % name) or [None])[0]
    pre = Dump(f('pre'), part)
    mid = Dump(f('mid'), part) if f('mid') else pre
    drawn = Dump(f('drawn'), part)
    c = PARTS[part]
    e = Engine(pre, part, mid)
    tseg = e.target_seg
    before = bytes(mid.b(tseg, 0, 0x10000))
    e.buf[:] = before
    e.build()
    order = [k & 0xffff for k in e.sort()]
    e.draw()
    after = bytes(e.buf)
    ref = drawn.b(tseg, 0, 64320)
    if after[:64320] != ref:
        print('warning: the model differs from the emulator in this frame')

    b = bytearray(b'DOPEENG1')
    b += struct.pack('<IIII', int(part[1]), pre.frame, c['objs'], c['objlast'])
    recs = []
    o = c['objs']
    while True:
        r = bytearray(pre.b(0x10, o, 0x3a))
        s = struct.unpack_from('<H', r, 0)[0]
        if s == 0:
            break
        if s != 0xffff:
            struct.pack_into('<H', r, 0, pre.seg_of(s))
        recs.append(bytes(r))
        o += 0x3a
    b += struct.pack('<I', len(recs)) + b''.join(recs)
    b += struct.pack('<BB', pre.u8(0x10, c['attr_c']), pre.u8(0x10, c['attr_b']))
    b += struct.pack('<4H', *e.segenv) + struct.pack('<4H', *e.segtex)
    b += struct.pack('<256h', *[x - 0x10000 if x & 0x8000 else x for x in e.bump])
    b += bytes(int(x) for x in drawn.dac)
    for s in list(e.segenv) + list(e.segtex):
        for row in range(128):
            b += bytes(mid.b(s, row * 256, 128)) if s else bytes(128)
    b += struct.pack('<I', e.nvert)
    for i in range(e.nvert):
        g = 4 * i
        sx, sy = e.vscr[g]
        b += struct.pack('<hhIIh', sx, sy, e.venv[g], e.vtex[g], e.vdep[g])
    b += struct.pack('<I', len(e.faces))
    for key, A, B, C in e.faces:
        b += struct.pack('<HHH', A, B, C)
    b += struct.pack('<I', len(order)) + struct.pack('<%dH' % len(order), *order)
    b += before[:64320] + after[:64320]
    with open(out, 'wb') as fh:
        fh.write(b)
    print('%s frame %d: %d records, %d vertices, %d faces -> %s' % (part, pre.frame, len(recs), e.nvert,
                                                                   len(e.faces), out))


if __name__ == '__main__':
    main()

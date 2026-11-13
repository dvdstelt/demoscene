"""Exports the engine draws dumped from the emulator (clone/re/models/engine/run_engine.py --dump-draws: the memory at
the entry of the draw 0x4169f8 and the target page before / after it) for enginetest, which reads them without numpy.

    python3 tools/enginetest/export_dumps.py OUTDIR ../re/dump/engine/draw_t*.npz      (inside the toolbox)

Per dump OUTDIR/<name>.bin (little endian; generated from the original: keep it out of the repository):
    "CLONENG1", int32 part, tick, call, float light [0x427f70], window a [0x422534], b [0x422538], int32 map (0..2 =
    A, B, C; -1 none), 65536 dwords: the map's texels (0x00RRGGBB log levels, rows of 256; the 257th row is row 0),
    uint32 nv + nv x 3 floats: the transformed vertices (camera space), uint32 ns + ns x 3 floats: the model /
    object-space vertices, 3 floats: the part's angles (part 9 A0, A1, A2 [0xf73574..]; part 10 A10 [0xf73920]),
    uint32 nn + nn x 3 floats: the vertex normals (parts 7, 8), uint32 nf + per face: 3 uint32 vertex indices,
    3 int32 normal indices (-1: none), 6 floats uv, 3 int32 shades (16.16, +0x3c), uint32 hidden (+0x34), int32 map,
    uint32 nd + nd uint32: the faces in the buckets in draw order (4095 down to 0, each list from its head),
    uint32 ngv + ngv x 3 int16: the geosphere's int16 vertices (0xf61404), uint32 ngf + ngf x 3 int16: its faces
    (0xf6cf84), 64000 dwords before, 64000 dwords after (320 x 200, 0x00RRGGBB).
"""
import os
import struct
import sys

import numpy as np

BASE = 0x400000
BUCKETS = 0xf5d3dc
MAPS = (0xe35108, 0xe3510c, 0xe35110)       # pointers to maps A, B, C
OBJ = {  # part: face array ptr, face count, vertex count, transformed ptr, normals ptr, model / source ptr (addresses)
    7: (0xf613dc, 0xf613e0, 0xf613e4, 0xf613e8, 0xf613ec, 0xf613f0),
    8: (0xf72d4c, 0xf72d50, 0xf72d54, 0xf72d58, 0xf72d60, 0xf72d5c),
    9: (0xf73584, 0x428018, 0x42801c, 0xf73580, None, 0xf73594),
    10: (0xf73908, 0x428154, 0x428158, 0xf73904, None, 0xf7391c),
}


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for path in sys.argv[2:]:
        d = np.load(path)
        m = d['mem']
        u32 = lambda a: struct.unpack_from('<I', m, a - BASE)[0]
        i32 = lambda a: struct.unpack_from('<i', m, a - BASE)[0]
        f32 = lambda a: struct.unpack_from('<f', m, a - BASE)[0]
        part = int(d['part'])
        fp, nfp, nvp, xfp, nrp, srcp = OBJ[part]
        faces, nf, nv, xf = u32(fp), u32(nfp), u32(nvp), u32(xfp)
        nrm = u32(nrp) if nrp else 0
        src = u32(srcp)
        maps = [u32(a) for a in MAPS]
        b = bytearray(b'CLONENG1')
        b += struct.pack('<3i3f', part, int(d['tick']), int(d['call']), f32(0x427f70), f32(0x422534), f32(0x422538))
        used = -1
        for k in range(nf):
            t = u32(faces + 0x48 * k + 0x38)
            if t in maps:
                used = maps.index(t)
                break
        b += struct.pack('<i', used)
        if used >= 0:
            b += bytes(m[maps[used] - BASE:maps[used] - BASE + 4 * 65536])
        else:
            b += bytes(4 * 65536)

        def vecs(base, n):
            r = bytearray(struct.pack('<I', n))
            for i in range(n):
                r += bytes(m[base + 0x34 * i - BASE:base + 0x34 * i + 12 - BASE])
            return r
        b += vecs(xf, nv)
        b += vecs(src, nv)
        ang = {9: (f32(0xf73574), f32(0xf73578), f32(0xf7357c)), 10: (f32(0xf73920), 0.0, 0.0)}.get(part, (0, 0, 0))
        b += struct.pack('<3f', *ang)
        b += vecs(nrm, nv) if nrm else struct.pack('<I', 0)
        b += struct.pack('<I', nf)
        index = {}
        for k in range(nf):
            f = faces + 0x48 * k
            index[f] = k
            vi = []
            for i in range(3):
                q = u32(f + 4 + 4 * i) - xf
                assert q % 0x34 == 0 and 0 <= q // 0x34 < nv, (path, k, q)
                vi.append(q // 0x34)
            ni = []
            for i in range(3):
                p = u32(f + 0x10 + 4 * i)
                ni.append(-1 if not p or not nrm else (p - nrm) // 0x34)
            uv = struct.unpack_from('<6f', m, f + 0x1c - BASE)
            sh = struct.unpack_from('<3i', m, f + 0x3c - BASE)
            t = u32(f + 0x38)
            b += struct.pack('<3I3i6f3iIi', *vi, *ni, *uv, *sh, u32(f + 0x34), maps.index(t) if t in maps else -1)
        order = []
        for k in range(0xfff, -1, -1):
            f = u32(BUCKETS + 4 * k)
            while f:
                order.append(index[f])
                f = u32(f)
        b += struct.pack('<I', len(order)) + struct.pack('<%dI' % len(order), *order)
        if part in (7, 8):
            ngv, ngf = u32(0xf72d44), u32(0xf72d48)
            b += struct.pack('<I', ngv)
            for i in range(ngv):
                b += bytes(m[0xf61404 + 12 * i - BASE:0xf61404 + 12 * i + 6 - BASE])
            b += struct.pack('<I', ngf) + bytes(m[0xf6cf84 - BASE:0xf6cf84 + 6 * ngf - BASE])
        else:
            b += struct.pack('<II', 0, 0)
        b += d['before'].tobytes() + d['after'].tobytes()
        name = os.path.join(out, os.path.basename(path).replace('.npz', '.bin'))
        open(name, 'wb').write(b)
        print('%s: part %d, %d vertices, %d faces, %d in the buckets, map %d' % (name, part, nv, nf, len(order), used))


if __name__ == '__main__':
    main()

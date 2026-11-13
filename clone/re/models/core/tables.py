"""Core precalc tables of CLONE.EXE, recomputed from the notes and compared with snaps/t11.snap.

  0x416806: projection constants (0x414f04), 16x16 bilinear weights at 0x426d8c, exp table 0x426b8c
  0x403f10: signed multiply table 64 x 512 bytes at 0xea740c, pointer [0x4224e8]
  0x40347a: exp table 0x420854
  0x4033ee: 16x16 bilinear weights at 0x420a54

usage (inside the toolbox, from clone/re): python3 models/core/tables.py [snaps/t11.snap]
"""
import math
import os
import pickle
import struct
import sys

import unicorn  # noqa: F401  (the snapshot pickles unicorn objects)

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, RE)
import rep  # noqa: E402


def f32(x):
    """float32 store with the FPU in truncate mode (round toward zero)."""
    b = struct.pack('<f', x)
    y = struct.unpack('<f', b)[0]
    if abs(y) > abs(x):                      # rounded away from zero: step one ulp towards zero
        i = struct.unpack('<I', b)[0] - 1
        y = struct.unpack('<f', struct.pack('<I', i))[0]
    return y


def trunc(x): return int(x)                 # fistp with RC = truncate


def weights():
    """0x416806 / 0x4033ee: entry (i, j) at index (j * 16 + i) * 4 floats: (16-i)(16-j), (16-i)j, i(16-j), ij."""
    out = bytearray(16 * 16 * 16)
    for i in range(16):
        for j in range(16):
            o = (j * 64 + i * 4) * 4
            out[o:o + 16] = struct.pack('<4f', (16 - i) * (16 - j), (16 - i) * j, i * (16 - j), i * j)
    return bytes(out)


def exptab(scale, mult):
    """0x416806 (scale 0.011, mult 128) and 0x40347a (scale 1/23, mult 1): 512 bytes, k = 0..511."""
    out = bytearray(512)
    for k in range(512):
        v = trunc(math.exp((k - 256) * scale) * mult)
        out[k] = 255 if v > 255 else (0 if v < 0 else v)
    return bytes(out)


def multab():
    """0x403f10: row i = 0..63, column j = -256..255: (i * j) / 64 truncated toward zero, low byte."""
    out = bytearray(64 * 512)
    for i in range(64):
        for j in range(-256, 256):
            q = abs(i * j) // 64
            out[i * 512 + j + 256] = (q if i * j >= 0 else -q) & 0xff
    return bytes(out)


def main():
    snap = pickle.load(open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(RE, 'snaps', 't11.snap'), 'rb'))
    mem = snap['mem']

    def m(a, n): return mem[a:a + n]

    checks = [
        ('weights 0x426d8c', m(0x426d8c, 0x1000), weights()),
        ('weights 0x420a54', m(0x420a54, 0x1000), weights()),
        ('exp 0x426b8c', m(0x426b8c, 512), exptab(rep.rdd(0x41a4a8), rep.rdd(0x41a4b0))),
        ('exp 0x420854', m(0x420854, 512), exptab(rep.rdd(0x41a144), 1.0)),
        ('mul 0xea740c', m(0xea740c, 64 * 512), multab()),
    ]
    for name, got, want in checks:
        diff = sum(1 for a, b in zip(got, want) if a != b)
        print(f'{name:18s} {len(want):6d} bytes, {diff} differ')
    print('proj [0x422534] [0x422538] =', struct.unpack('<2f', m(0x422534, 8)), '(t11: last set by 0x416806)')
    print('[0x4224e8] = %#x' % struct.unpack('<I', m(0x4224e8, 4)))
    print('constants: 0.011 = %r, 128 = %r, 1/23 = %r' % (rep.rdd(0x41a4a8), rep.rdd(0x41a4b0), rep.rdd(0x41a144)))
    print('exp 0x426b8c first non-zero / first 255:',
          next(k for k in range(512) if mem[0x426b8c + k]), next(k for k in range(512) if mem[0x426b8c + k] == 255))
    print('exp 0x420854 first non-zero / first 255:',
          next(k for k in range(512) if mem[0x420854 + k]), next(k for k in range(512) if mem[0x420854 + k] == 255))
    nz = sum(1 for k in range(4096) if struct.unpack('<I', m(0xf5d3dc + 4 * k, 4))[0])
    print('0xf5d3dc..: %d non-zero dwords of 4096' % nz)


if __name__ == '__main__':
    main()

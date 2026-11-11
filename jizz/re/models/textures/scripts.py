"""List the 17 texture scripts of JIZZ.EXE (interpreter 0x12e72, driver 0x12c50) as readable op lists.

usage (inside the toolbox, from jizz/re):  python3 models/textures/scripts.py
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
IMG = open(os.path.join(RE, 'jizz_img.bin'), 'rb').read()

# driver 0x12c50: (script address, destination buffer pointer variable, name written by its op 0x26)
BUILDS = [
    (0x12b5d, 0x6582), (0x12439, 0x654e), (0x124c1, 0x658a), (0x12599, 0x6552), (0x12630, 0x6556),
    (0x126fc, 0x655a), (0x1276f, 0x655e), (0x127f0, 0x6562), (0x12919, 0x6566), (0x12993, 0x656a),
    (0x129d6, 0x656e), (0x129ec, 0x6572), (0x129f7, 0x6576), (0x12a7d, 0x657a), (0x12adc, 0x657e),
    (0x12b5d, 0x6582), (0x12bd9, 0x6586),
]

MASKS = ['RGB', 'R', 'G', 'RG', 'B', 'RB', 'GB', 'RGB']   # table 0x12e4a: (first plane, gap, count) per mask & 7

NAMES = {0x00: 'select', 0x01: 'end', 0x02: 'fill', 0x03: 'noise', 0x04: 'blur2', 0x05: 'blur4', 0x06: 'blur8',
         0x07: 'hsv2rgb', 0x08: 'rgb2hsv', 0x09: 'add', 0x0a: 'blobs', 0x0b: 'shift', 0x0c: 'scale_sat',
         0x0d: 'scale_wrap', 0x0e: 'mul', 0x0f: 'addbuf', 0x10: 'mix', 0x11: 'mixmask', 0x12: 'emboss',
         0x13: 'addsat', 0x14: 'sines', 0x15: 'warp', 0x16: 'curve', 0x17: 'curve', 0x18: 'invert',
         0x19: 'copy', 0x1a: 'copyplane', 0x1b: 'addxy', 0x1c: 'cone', 0x1d: 'clouds', 0x1e: 'mblur',
         0x1f: 'angle', 0x20: 'tile320', 0x21: 'to16bit', 0x22: 'rle4', 0x23: 'matrix', 0x24: 'interleave',
         0x25: 'nop', 0x26: 'savetga'}


def sb(v): return v - 256 if v >= 128 else v


def parse(a):
    """Yield (address, op, mask byte, args, length) until op 1 (end)."""
    while True:
        op = IMG[a]
        m = IMG[a + 1]
        if op in (0x01, 0x03, 0x07, 0x08, 0x1b, 0x20, 0x21, 0x24, 0x25): n, args = 1, ()
        elif op in (0x00, 0x04, 0x05, 0x06, 0x18, 0x1f): n, args = 2, ()
        elif op in (0x02, 0x09, 0x0e, 0x0f, 0x15, 0x19, 0x1c, 0x1d): n, args = 3, (IMG[a + 2],)
        elif op == 0x0b: n, args = 3, (IMG[a + 1], IMG[a + 2])
        elif op in (0x0c, 0x0d): n, args = 4, (struct.unpack_from('<h', IMG, a + 2)[0],)
        elif op == 0x0a: n, args = 4, (struct.unpack_from('<H', IMG, a + 2)[0],)
        elif op in (0x10, 0x13, 0x1a): n, args = 4, (IMG[a + 2], IMG[a + 3])
        elif op == 0x11: n, args = 5, (IMG[a + 2], IMG[a + 3], IMG[a + 4])
        elif op == 0x1e: n, args = 5, (IMG[a + 2], IMG[a + 3], IMG[a + 4])
        elif op == 0x12: n, args = 6, (IMG[a + 2], sb(IMG[a + 3]), IMG[a + 4], IMG[a + 5])
        elif op == 0x14: n, args = 9, tuple(sb(x) for x in IMG[a + 2:a + 8]) + (IMG[a + 8],)
        elif op in (0x16, 0x17): n, args = 20, (IMG[a + 2], IMG[a + 3]) + struct.unpack_from('<8h', IMG, a + 4)
        elif op == 0x22: n, args = 6, (struct.unpack_from('<I', IMG, a + 2)[0],)
        elif op == 0x23: n, args = 10, tuple(sb(x) for x in IMG[a + 1:a + 10])
        elif op == 0x26:
            e = IMG.index(0, a + 1)
            n, args = e + 1 - a, (IMG[a + 1:e].decode('latin1'),)
        else: raise ValueError(f'bad op {op:#x} at {a:#x}')
        yield a, op, m, args, n
        if op == 0x01: return
        a += n


if __name__ == '__main__':
    seen = set()
    for i, (s, dst) in enumerate(BUILDS):
        ops = list(parse(s))
        end = ops[-1][0] + 1
        print(f'build {i + 1}: script {s:#x}..{end:#x} ({end - s} bytes) -> [{dst:#x}]'
              + (' (same script as before)' if s in seen else ''))
        seen.add(s)
        for a, op, m, args, n in ops:
            ms = MASKS[m & 7] if n >= 2 and op not in (0x0b, 0x23, 0x26, 0x00) else ''
            if op == 0x00: ms = f'B{m & 7}'
            print(f'  {a:#07x}: {IMG[a:a + n].hex() if n <= 12 else IMG[a:a + 12].hex() + "..":26s} '
                  f'{op:02x} {NAMES[op]:10s} {ms:4s} {args}')


def compact(s):
    """One string per script: the ops in a short notation (used for notes-textures.md)."""
    out = []
    for a, op, m, args, n in parse(s):
        ms = MASKS[m & 7]
        nm = NAMES[op]
        if op == 0x00: t = f'sel B{m & 7}'
        elif op in (0x01, 0x03, 0x07, 0x08, 0x1b, 0x20, 0x21, 0x24, 0x25): t = nm
        elif op in (0x04, 0x05, 0x06, 0x18, 0x1f): t = f'{nm} {ms}'
        elif op in (0x02, 0x09, 0x1c, 0x1d): t = f'{nm} {ms} {args[0]}'
        elif op in (0x0c, 0x0d): t = f'{nm} {ms} {args[0]}'
        elif op == 0x0a: t = f'{nm} {ms} {args[0]}'
        elif op == 0x0b: t = f'shift {args[0]},{args[1]}'
        elif op in (0x0e, 0x0f, 0x15): t = f'{nm} {ms} B{args[0]}'
        elif op == 0x19: t = f'copy {ms}->B{args[0]}'
        elif op == 0x10: t = f'mix {ms} B{args[0]} {args[1]}'
        elif op == 0x11: t = f'mixmask {ms} B{args[0]} by B{args[2]}.{MASKS[args[1] & 7][0]}'
        elif op == 0x12: t = f'emboss {ms[0]} ang {args[0]} s {args[1]} ->B{args[3]}.{MASKS[args[2] & 7][0]}'
        elif op == 0x13: t = f'addsat {ms} B{args[1]}.{MASKS[args[0] & 7][0]}'
        elif op == 0x14: t = f'sines {ms[0]} ({",".join(map(str, args[:6]))}) {args[6]}'
        elif op in (0x16, 0x17): t = f'curve {ms[0]} n={args[0] * 16} size {args[1]} ({",".join(map(str, args[2:]))})'
        elif op == 0x1a: t = f'copyplane {ms[0]}->B{args[1]}.{MASKS[args[0] & 7][0]}'
        elif op == 0x1e: t = f'mblur {ms} {args}'
        elif op == 0x22: t = f'rle4 {ms[0]} @{args[0]:#x}'
        elif op == 0x23: t = f'matrix ({",".join(map(str, args))})'
        elif op == 0x26: t = f'save "{args[0]}"'
        else: t = nm
        out.append(t)
    return '; '.join(out)

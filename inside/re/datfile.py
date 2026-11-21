"""INSIDE.DAT: one LZSS-compressed block holding every data file, with a directory at its end.

Format (the loader 0x4000e8, the decompressor 0x4011c0 / 0x4011d6, the directory 0x4001c4, the lookup 0x40022c):
  dword U (the unpacked size), then the compressed stream. LZSS: a flag byte gives 8 items, bit 0 first; bit = 1:
  one literal byte; bit = 0: two bytes b0, b1: distance = (b1 & 0xf0) << 4 | b0, length = (b1 & 0x0f) + 3; the bytes
  are copied one by one from distance back (overlap allowed). Stops when U bytes are out.
  The unpacked block ends with a dword N (number of files), preceded by N directory entries of 0x30 bytes: the name
  (zero-terminated, with '/' as separator, compared case-sensitively by 0x40b980) and at +0x24 the file's offset in
  the block. Sizes are not stored: a file runs to the next file's offset.

usage: python3 datfile.py ../original/INSIDE.DAT [--extract DIR]
In Python: import datfile; d = datfile.Dat(path); d.files -> {name: (offset, size)}; d.get(name) -> bytes
"""
import struct
import sys


def lzss(src, size):
    out = bytearray()
    i = 0
    flags = 0
    while len(out) < size:
        flags >>= 1
        if not flags & 0x100:
            flags = src[i] | 0xff00
            i += 1
        if flags & 1:
            out.append(src[i])
            i += 1
        else:
            b0, b1 = src[i], src[i + 1]
            i += 2
            dist = (b1 & 0xf0) << 4 | b0
            for _ in range((b1 & 0x0f) + 3):
                out.append(out[-dist])
    return bytes(out[:size]), i


class Dat:
    def __init__(self, path):
        raw = open(path, 'rb').read()
        size = struct.unpack_from('<I', raw)[0]
        self.data, used = lzss(raw[4:], size)
        self.used = used
        n = struct.unpack_from('<I', self.data, size - 4)[0]
        d0 = size - 4 - n * 0x30
        self.dir_offset = d0
        ents = []
        for k in range(n):
            e = self.data[d0 + k * 0x30:d0 + k * 0x30 + 0x30]
            name = e[:0x24].split(b'\0')[0].decode('latin1')
            ents.append((name, struct.unpack_from('<I', e, 0x24)[0], e))
        offs = sorted(set(o for _, o, _ in ents)) + [d0]
        self.files = {}
        self.raw_entries = ents
        for name, o, _ in ents:
            nxt = min(x for x in offs if x > o) if any(x > o for x in offs) else d0
            self.files[name] = (o, nxt - o)

    def get(self, name):
        o, n = self.files[name]
        return self.data[o:o + n]


if __name__ == '__main__':
    d = Dat(sys.argv[1])
    print('unpacked %d bytes (compressed stream %d bytes), %d files, directory at %#x' % (
        len(d.data), d.used, len(d.files), d.dir_offset))
    for name, o, e in d.raw_entries:
        print('%-36s %8x %8d   %s' % (name, o, d.files[name][1], e[0x24:].hex()))
    if '--extract' in sys.argv:
        import os
        out = sys.argv[sys.argv.index('--extract') + 1]
        for name in d.files:
            p = os.path.join(out, name)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, 'wb').write(d.get(name))

"""CLONE.EXE unpacker written from the description in NOTES.md ("File layout", "The veloader", "The decompressor",
"Fixups") and notes-core.md, as a check that the description is enough for the remake's C++ unpacker.

usage (toolbox, from clone/re): python3 models/core/veunpack.py ../original/CLONE.EXE [base]
Compares the result with unpacked/clone_img.bin (relocated for base 0x400000) and prints the hashes.
"""
import hashlib
import os
import struct
import sys

RE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))


def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]


def fnv1a64(b):
    h = 0xcbf29ce484222325
    for x in b:
        h = ((h ^ x) * 0x100000001b3) & 0xffffffffffffffff
    return h


# format constants (the veloader's tables; they follow Deflate's rule, cut to 24 distance and 22 length codes)
DIST_BASE = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073]
DIST_EXTRA = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10]
LEN_BASE = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83]
LEN_EXTRA = [0] * 8 + [1] * 4 + [2] * 4 + [3] * 4 + [4, 0]


class Bits:
    """Big-endian 16-bit words, bits MSB first."""

    def __init__(self, data, pos):
        self.d, self.p, self.buf, self.n = data, pos, 0, 0

    def peek32(self):
        while self.n <= 16:
            w = (self.d[self.p] << 8 | self.d[self.p + 1]) if self.p + 1 < len(self.d) else 0
            self.p += 2
            self.buf |= w << (16 - self.n)       # buf is a left-aligned 32-bit window
            self.n += 16
        return self.buf & 0xffffffff

    def take(self, k):
        if k == 0: return 0
        v = self.peek32() >> (32 - k)
        self.buf = (self.buf << k) & 0xffffffff
        self.n -= k
        return v


class Code:
    """Canonical code: for L = 1..15, the symbols with length L in increasing order get consecutive codes."""

    def __init__(self, lengths):
        self.last = []                        # (L, last code left-aligned with low bits all ones, first code, syms)
        code = 0
        for L in range(1, 16):
            syms = [s for s, l in enumerate(lengths) if l == L]
            if not syms: continue
            first = code
            code += len(syms) << (32 - L)
            self.last.append((L, (code - 1) | ((1 << (32 - L)) - 1), first, syms))
        if code != 1 << 32:
            raise ValueError('code lengths do not fill the code space (the loader exits with code 5)')

    def decode(self, bits):
        w = bits.peek32()
        for L, last, first, syms in self.last:
            if last >= w:
                bits.take(L)
                return syms[(w - first) >> (32 - L)]
        raise ValueError('bad code')


def decompress(src, out_size):
    lc = [(src[i // 2] >> (4 * (i & 1))) & 15 for i in range(16)]            # bytes 0..7: length code
    ln = [(src[8 + i // 2] >> (4 * (i & 1))) & 15 for i in range(22)]        # bytes 8..18: LENGTH code
    bits = Bits(src, 19)
    lcode = Code(lc)
    litdist = Code([lcode.decode(bits) for _ in range(280)])
    lencode = Code(ln)
    out = bytearray()
    while len(out) < out_size:
        s = litdist.decode(bits)
        if s < 256:
            out.append(s)
            continue
        k = s - 256
        dist = DIST_BASE[k] + bits.take(DIST_EXTRA[k])
        j = lencode.decode(bits)
        length = LEN_BASE[j] + bits.take(LEN_EXTRA[j])
        for _ in range(length):
            out.append(out[-dist])
    return bytes(out[:out_size])


def apply_fixups(img, fix, base):
    img = bytearray(img)
    p, n = 0, 0
    while True:
        t = fix[p]; p += 1
        if t == 0: break
        if t >= 4: raise ValueError('fixup type %d' % t)
        count = u16(fix, p) or 65536; p += 2
        site = u32(fix, p); p += 4
        for i in range(count):
            if i:
                delta = 0
                while True:
                    b = fix[p]; p += 1
                    delta = delta << 7 | (b & 0x7f)
                    if not b & 0x80: break
                site += delta
            if t & 1:
                struct.pack_into('<I', img, site, (u32(img, site) + base) & 0xffffffff)
                site += 4
                n += 1
            if t & 2:
                raise ValueError('selector fixups: not in CLONE.EXE')
    return bytes(img), n, p


def unpack(exe, base):
    mz_size = (u16(exe, 4) - 1) * 512 + u16(exe, 2) if u16(exe, 2) else u16(exe, 4) * 512
    le = (mz_size + 7) & ~7
    assert exe[le:le + 2] == b'LE'
    ve = u32(exe, le + 0x80) + u32(exe, le + 0x14) * 0x1000
    if u32(exe, le + 0x2c): ve += u32(exe, le + 0x2c) - 0x1000
    h = exe[ve:ve + 0x44]
    assert h[:2] == b'VE'
    img_ofs, fix_ofs = u32(h, 4), u32(h, 8)
    img_packed, fix_packed = u32(h, 0x14), u32(h, 0x18)
    img_size, fix_size, bss, entry, stack = u32(h, 0x24), u32(h, 0x28), u32(h, 0x2c), u32(h, 0x34), u32(h, 0x38)
    m_img, m_fix = h[0x40], h[0x41]
    print(f'MZ size {mz_size:#x}, LE at {le:#x}, VE at {ve:#x}: image {img_size:#x} (packed {img_packed:#x} at '
          f'{ve + img_ofs:#x}, method {m_img}), fixups {fix_size:#x} (packed {fix_packed:#x} at {ve + fix_ofs:#x}, '
          f'method {m_fix}), BSS {bss:#x}, entry {entry:#x}, stack {stack:#x}')
    src = exe[ve + img_ofs:ve + img_ofs + img_packed]
    img = decompress(src, img_size) if m_img else src[:img_size]
    fsrc = exe[ve + fix_ofs:ve + fix_ofs + fix_packed]
    fix = decompress(fsrc, fix_size) if m_fix else fsrc[:fix_size]
    img, n, used = apply_fixups(img, fix, base)
    print(f'{n} dword fixups, fixup list bytes used {used} of {len(fix)}')
    return img, bss, entry, stack


def main():
    exe = open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(RE, '..', 'original', 'CLONE.EXE'), 'rb').read()
    base = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x400000
    img, bss, entry, stack = unpack(exe, base)
    ref = open(os.path.join(RE, 'unpacked', 'clone_img.bin'), 'rb').read()
    print('size', len(img), 'sha256', hashlib.sha256(img).hexdigest(), 'fnv1a64 %016x' % fnv1a64(img))
    if base == 0x400000:
        diff = sum(1 for a, b in zip(img, ref) if a != b) + abs(len(img) - len(ref))
        print('compared with unpacked/clone_img.bin:', 'identical' if diff == 0 else f'{diff} bytes differ')
    print('entry %#x, ESP %#x' % (base + entry, (base + stack) & ~3))


if __name__ == '__main__':
    main()

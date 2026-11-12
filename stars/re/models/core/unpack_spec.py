"""An unpacker for STARS.EXE written only from the description in notes-core.md ("Unpacking STARS.EXE"), as the
blueprint for a C++ version: no emulation, no code shared with ../../unpack.py. It decrypts the Infiny Loader layer,
decompresses PKLITE with prefix-code tables written as in the notes, reads the overlay directory and unpacks DIET,
then compares everything with unpacked/ (made by unpack.py, which checks itself against the original code).

usage: python3 unpack_spec.py [../../../original/STARS.EXE]"""
import hashlib, os, struct, sys

RE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))


class Bits:
    """16-bit little-endian words, least significant bit first; the next word is fetched as soon as the 16th bit of
    the current one has been taken, so it comes before any byte read after that bit. The first word is fetched
    before the first bit."""
    def __init__(self, data, pos):
        self.d, self.p = data, pos
        self.w = self.word(); self.n = 16

    def word(self):
        v = self.d[self.p] | self.d[self.p + 1] << 8; self.p += 2; return v

    def byte(self):
        v = self.d[self.p]; self.p += 1; return v

    def bit(self):
        b = self.w & 1; self.w >>= 1; self.n -= 1
        if self.n == 0: self.w = self.word(); self.n = 16
        return b

    def code(self, table, maxlen):
        s = ''
        while len(s) < maxlen:
            s += str(self.bit())
            if s in table: return table[s]
        raise ValueError('bad code ' + s)


# ---- stage 1: Infiny Loader (file 0xa0 .. 0x125a0, word by word) ----
def decrypt(exe):
    out = bytearray(exe[0xa0:0x125a0])
    ck = 0
    for i in range(0, len(out), 2):
        w = out[i] | out[i + 1] << 8
        w = ((((w - 0xbc67 - 0x305e + 0x9af9) & 0xffff) ^ 0xc4f5) - 0x7b9b) & 0xffff
        out[i], out[i + 1] = w & 0xff, w >> 8
        ck ^= w
        ck = (ck + (w & 0xff) * (w >> 8)) & 0xffff
    return bytes(out), ck


# ---- stage 2: PKLITE 1.1x, compressed data from image offset 0x290 ----
LEN = {'10': 2, '11': 3, '000': 4, '0010': 5, '0011': 6, '0100': 7, '01010': 8, '01011': 9, '01100': 10,
       '011010': 11, '011011': 12, '0111010': 13, '0111011': 14, '0111100': 15, '01111010': 16, '01111011': 17,
       '01111100': 18, '011111010': 19, '011111011': 20, '011111100': 21, '011111101': 22, '011111110': 23,
       '011111111': 24, '011100': 'special'}
OFS = {'1': 0, '0000': 1, '0001': 2, '00100': 3, '00101': 4, '00110': 5, '00111': 6}
OFS.update({'01' + format(v, '04b'): 7 + v for v in range(7)})                   # 010000 .. 010110 = 7 .. 13
OFS.update({format(v, '07b'): v - 32 for v in range(0b0101110, 0b0111111 + 1)})  # 0101110 .. 0111111 = 14 .. 31


def pklite(img):
    b = Bits(img, 0x290)
    out = bytearray()
    while True:
        if b.bit() == 0:
            out.append(b.byte()); continue
        n = b.code(LEN, 9)
        if n == 'special':
            x = b.byte()
            if x == 0xfe: continue
            if x == 0xff: break
            n = 25 + x
        hi = 0 if n == 2 else b.code(OFS, 7)
        off = hi << 8 | b.byte()
        for _ in range(n): out.append(out[-off])
    p = b.p
    relocs = []
    while True:                      # relocation groups: count byte, segment word, count offset words; 0 ends
        n = img[p]; p += 1
        if n == 0: break
        seg = struct.unpack_from('<H', img, p)[0]; p += 2
        for _ in range(n):
            relocs.append(seg * 16 + struct.unpack_from('<H', img, p)[0]); p += 2
    ss, sp, cs, ip = struct.unpack_from('<4H', img, p)
    return bytes(out), relocs, (ss, sp, cs, ip)


# ---- the overlay ----
def directory(exe):
    ents = []
    for n in range(1, 59):
        num, off, size = struct.unpack_from('<IiI', exe, len(exe) - 12 * n)
        assert num == n
        ents.append((n, len(exe) + off, size))
    return ents


def diet(src):
    assert src[6:9] == b'dlz'
    osize = (src[14] >> 2) << 16 | struct.unpack_from('<H', src, 15)[0]
    b = Bits(src, 0x11)
    out = bytearray()
    while True:
        if b.bit():
            out.append(b.byte()); continue
        x = b.bit()
        lo = b.byte()
        hb = 0xff
        if x == 0:
            if b.bit() == 0:
                if lo != 0xff: n = 2
                elif b.bit(): continue
                else: break
            else:
                for _ in range(3): hb = (hb << 1 | b.bit()) & 0xff
                hb = (hb - 1) & 0xff
                n = 2
        else:
            hb = (hb << 1 | b.bit()) & 0xff
            if b.bit() == 0:
                d = 2
                for _ in range(3):
                    if b.bit(): break
                    hb = (hb << 1 | b.bit()) & 0xff
                    d = (d << 1) & 0xff
                hb = (hb - d) & 0xff
            n = None
            for k, ln in enumerate((3, 4, 5, 6)):
                if b.bit(): n = ln; break
            if n is None:
                if b.bit(): n = 7 + b.bit()
                elif b.bit(): n = b.byte() + 17
                else: n = 9 + (b.bit() << 2 | b.bit() << 1 | b.bit())
        off = (hb << 8 | lo) - 0x10000
        for _ in range(n): out.append(out[off])
    assert len(out) == osize
    return bytes(out)


if __name__ == '__main__':
    exe = open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(RE, '..', 'original', 'STARS.EXE'), 'rb').read()
    img, ck = decrypt(exe)
    print('decrypted PKLITE image, checksum %#06x (expected 0x0981)' % ck)
    prog, relocs, regs = pklite(img)
    ref = open(os.path.join(RE, 'unpacked', 'stars_img.bin'), 'rb').read()
    print('program image %#x bytes, %d relocations, SS SP CS IP %s' % (len(prog), len(relocs), [hex(r) for r in regs]))
    # unpacked/stars_img.bin is relocated for load segment 0x1000 and padded with zeros to 0x840b0
    rel = bytearray(prog)
    for r in relocs:
        struct.pack_into('<H', rel, r, (struct.unpack_from('<H', rel, r)[0] + 0x1000) & 0xffff)
    print('equal to unpacked/stars_img.bin (relocated, zero padded):',
          bytes(rel) + bytes(len(ref) - len(rel)) == ref, '; sha256 of the unrelocated image',
          hashlib.sha256(prog).hexdigest()[:16])
    bad = 0
    for n, pos, size in directory(exe):
        raw = exe[pos:pos + size]
        data = diet(raw) if raw[6:9] == b'dlz' else raw
        if data != open(os.path.join(RE, 'unpacked', 'files', '%02d.bin' % n), 'rb').read():
            bad += 1; print('file %d differs' % n)
    print('overlay: 58 files, %d differ from unpacked/files' % bad)

"""An RNC-1 unpacker and DOPE.EXE chain walker written only from NOTES.md ("File layout" and "RNC ProPack method 1"),
without looking at unrnc.py, to check that the description is complete. It compares every unpacked load module with
unpacked/<name>.bin and checks both CRCs.

usage: python3 models/core/rnc_from_notes.py [../original/DOPE.EXE]     (from dope/re, inside the toolbox)
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
NAMES = ['loader', 'p1', 'p2', 'p3', 'p4', 'p5', 'p6', 'p7', 'term']


def crc16(data):
    """CRC-16/ARC: reflected polynomial 0xA001, initial value 0, table driven"""
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
        table.append(c)
    c = 0
    for b in data:
        c = (c >> 8) ^ table[(c ^ b) & 0xff]
    return c


class Bits:
    """the bit reader of NOTES.md: 32-bit buffer, n valid bits, byte position p of the look-ahead word"""

    def __init__(self, data, start):
        self.d = data
        self.p = start
        self.buf = self.word(start)
        self.n = 16

    def word(self, p):
        return self.d[p] | self.d[p + 1] << 8 if p + 1 < len(self.d) else (self.d[p] if p < len(self.d) else 0)

    def take(self, k):
        v = self.buf & ((1 << k) - 1)
        self.buf >>= k
        self.n -= k
        if self.n < 16:
            self.p += 2
            self.buf |= self.word(self.p) << self.n
            self.n += 16
        return v

    def peek(self, k):
        return self.buf & ((1 << k) - 1)

    def raw_bytes(self, count):
        """literal run: bytes straight from the stream at p, then the look-ahead is replaced"""
        out = self.d[self.p:self.p + count]
        self.p += count
        self.n -= 16
        self.buf &= (1 << self.n) - 1
        self.buf |= self.word(self.p) << self.n
        self.n += 16
        return out


def rev(code, length):
    r = 0
    for _ in range(length):
        r = r << 1 | (code & 1)
        code >>= 1
    return r


def read_table(bits):
    n = bits.take(5)
    lens = [bits.take(4) for _ in range(n)]
    codes = []                                   # (length, reversed code, value)
    counter = 0
    for L in range(1, 17):
        for v in range(n):
            if lens[v] == L:
                codes.append((L, rev(counter, L), v))
                counter += 1
        counter <<= 1
    return codes


def decode(bits, table):
    for L, code, v in table:
        if bits.peek(L) == code:
            bits.take(L)
            return v
    raise ValueError('no code matches')


def value(bits, table):
    v = decode(bits, table)
    if v < 2:
        return v
    return (1 << (v - 1)) | bits.take(v - 1)


def unpack(module):
    """module = the load module of a packed MZ (RNC header + bit stream + stub) -> (data, header fields)"""
    assert module[:4] == b'RNC\x01', module[:4]
    usize, psize, ucrc, pcrc, leeway, chunks = struct.unpack('>IIHHBB', module[4:18])
    assert crc16(module[18:18 + psize]) == pcrc, 'packed CRC'
    bits = Bits(module, 18)
    bits.take(2)                                 # lock and key flags
    out = bytearray()
    for _ in range(chunks):
        lit, dist, ln = read_table(bits), read_table(bits), read_table(bits)
        count = bits.take(16)
        for i in range(count):
            out += bits.raw_bytes(value(bits, lit))
            if i == count - 1:
                break
            d = value(bits, dist) + 1
            m = value(bits, ln) + 2
            for _ in range(m):
                out.append(out[-d])
    assert len(out) == usize, (len(out), usize)
    assert crc16(out) == ucrc, 'unpacked CRC'
    return bytes(out), dict(usize=usize, psize=psize, leeway=leeway, chunks=chunks)


def chain(exe):
    """the chain of MZ executables: each starts where the previous one's MZ size (header bytes 2..5) ends"""
    off = 0
    while off < len(exe):
        assert exe[off:off + 2] == b'MZ', hex(off)
        last, pages, nrel, hdr = struct.unpack('<HHHH', exe[off + 2:off + 10])
        size = pages * 512 - (512 - last if last else 0)
        yield off, size, exe[off + hdr * 16:off + size]
        off += size


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RE, '..', 'original', 'DOPE.EXE')
    exe = open(path, 'rb').read()
    ok = True
    for name, (off, size, module) in zip(NAMES, chain(exe)):
        ref = open(os.path.join(RE, 'unpacked', name + '.bin'), 'rb').read()
        if module[:4] == b'RNC\x01':
            data, h = unpack(module)
            how = 'RNC-1 %d -> %d bytes, %d chunks, leeway %d' % (h['psize'], h['usize'], h['chunks'], h['leeway'])
        else:
            data, how = module, 'not packed'
        same = data == ref
        ok &= same
        print('%-6s at 0x%05x, MZ size 0x%05x: %s; %s unpacked/%s.bin' % (
            name, off, size, how, 'equals' if same else 'DIFFERS FROM', name))
    print('all equal' if ok else 'MISMATCH')


if __name__ == '__main__':
    main()

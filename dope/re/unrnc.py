"""Split DOPE.EXE into its executables and unpack each one (Rob Northen ProPack, method 1, DOS executable stub).

DOPE.EXE is a small MZ loader with seven more MZ executables and a 53-byte "TERM" stub appended to it (the loader
reads them from its own file, see NOTES.md). Every MZ except TERM is packed the same way: the load module starts
with an 18-byte RNC header and the RNC-1 bit stream, and the packer's 16-bit stub (entry CS:000C) sits at the end.

Each packed executable is unpacked twice:
  1. by running its own stub in unicorn (real mode, DOS loads it at segment LOAD), stopping at the stub's final
     `jmp far cs:[0]`, which gives the image in memory plus the original CS:IP and SS:SP;
  2. by rnc1_unpack() below plus the stub's relocation table,
and the two images must be identical. Both RNC CRCs are checked too.

Outputs in OUTDIR (default unpacked/), per executable NAME (loader, p1..p7, term):
  NAME.bin   the load module relocated for load segment 0 (so a far pointer seg:off in the code is at file offset
             seg*16 + off; add the load segment to the relocated words to get the in-memory image)
  NAME.exe   a plain, uncompressed MZ executable of the same program (runs under DOS like the packed one)
  NAME.txt   header values, entry point, stack, relocation sites, hashes
and info.txt: the file layout of DOPE.EXE.

usage: python3 unrnc.py ../original/DOPE.EXE [OUTDIR]
"""
import hashlib
import os
import struct
import sys

LOAD = 0x0800          # load segment used for the emulation (PSP at LOAD - 0x10)
MEMTOP = 0xA000


def mz_header(d, o):
    (sig, cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, csum, ip, cs, lfarlc, ovno) = \
        struct.unpack_from('<14H', d, o)
    assert sig == 0x5a4d, hex(o)
    size = (cp - 1) * 512 + cblp if cblp else cp * 512
    return dict(size=size, crlc=crlc, hdr=cparhdr * 16, minalloc=minalloc, maxalloc=maxalloc, ss=ss, sp=sp,
                ip=ip, cs=cs, lfarlc=lfarlc)


def split(d):
    """The chain of MZ executables: each one starts where the previous one's MZ size ends."""
    out, o = [], 0
    while o < len(d):
        h = mz_header(d, o)
        out.append((o, h))
        o += h['size']
    assert o == len(d)
    return out


# ---- RNC method 1 ---------------------------------------------------------------------------------------------

def crc16(data):
    """RNC's CRC: CRC-16 with the reflected polynomial 0xA001, initial value 0."""
    tab = []
    for i in range(256):
        v = i
        for _ in range(8):
            v = (v >> 1) ^ 0xa001 if v & 1 else v >> 1
        tab.append(v)
    c = 0
    for b in data:
        c = tab[(c ^ b) & 0xff] ^ (c >> 8)
    return c


class Bits:
    """RNC-1 bit reader: 16-bit little-endian words, bits taken from the least significant end. The buffer always
    holds the next whole word (at self.p) above the bits still to be used, like the stub's [bp+16h]/[bp+18h]."""
    def __init__(self, src, p):
        self.src, self.p = src, p
        self.buf = self.word(p)
        self.n = 16

    def word(self, p):
        return self.src[p] | self.src[p + 1] << 8 if p + 1 < len(self.src) else (self.src[p] if p < len(self.src) else 0)

    def peek(self):
        return self.buf

    def skip(self, n):
        self.buf >>= n
        self.n -= n
        if self.n < 16:
            self.p += 2
            self.buf |= self.word(self.p) << self.n
            self.n += 16

    def get(self, n):
        v = self.buf & ((1 << n) - 1)
        self.skip(n)
        return v

    def refill(self):
        """after literal bytes were copied from the stream at self.p: the look-ahead word is re-read there"""
        self.n -= 16
        self.buf &= (1 << self.n) - 1
        self.buf |= self.word(self.p) << self.n
        self.n += 16


def read_table(bits):
    """Huffman table: 5 bits count, count x 4 bits code length; canonical codes, shortest first, values in order,
    stored bit-reversed because the stream is read LSB first."""
    num = bits.get(5)
    lens = [bits.get(4) for _ in range(num)]
    table, code = [], 0
    for ln in range(1, 17):
        for v in range(num):
            if lens[v] == ln:
                rev = int('{:0{w}b}'.format(code, w=ln)[::-1], 2)
                table.append((rev, ln, v))
                code += 1
        code <<= 1
    return table


def read_value(bits, table):
    """A Huffman symbol v; v < 2 is the value itself, otherwise 2^(v-1) + (v-1) more bits."""
    b = bits.peek()
    for code, ln, v in table:
        if b & ((1 << ln) - 1) == code:
            bits.skip(ln)
            if v >= 2:
                v = (1 << (v - 1)) | bits.get(v - 1)
            return v
    raise ValueError('bad Huffman code')


def rnc1_unpack(blob):
    """blob starts with the 18-byte RNC header; returns the unpacked bytes (CRCs checked)."""
    assert blob[:4] == b'RNC\x01'
    usize, psize, ucrc, pcrc, leeway, chunks = struct.unpack_from('>IIHHBB', blob, 4)
    packed = blob[18:18 + psize]
    assert crc16(packed) == pcrc, 'packed CRC'
    bits = Bits(blob, 18)
    bits.skip(2)                            # two flag bits (lock / key), 0 here
    out = bytearray()
    for _ in range(chunks):
        raw = read_table(bits)
        dist = read_table(bits)
        length = read_table(bits)
        count = bits.get(16)                # number of (literal run, match) pairs; the last has no match
        while True:
            n = read_value(bits, raw)
            if n:
                out += blob[bits.p:bits.p + n]  # literal bytes are taken from the byte stream, not the bit buffer
                bits.p += n
                bits.refill()
            count -= 1
            if count <= 0:
                break
            off = read_value(bits, dist) + 1
            n = read_value(bits, length) + 2
            for _ in range(n):
                out.append(out[-off])
    assert len(out) == usize, (len(out), usize)
    assert crc16(out) == ucrc, 'unpacked CRC'
    return bytes(out), dict(usize=usize, psize=psize, ucrc=ucrc, pcrc=pcrc, leeway=leeway, chunks=chunks)


def stub_info(img, h):
    """The stub's own header (at CS:0000 of the packed program) and its relocation table (at CS:028C)."""
    st = h['cs'] * 16
    ip, cs, sp, ss, stublen, packed_paras = struct.unpack_from('<6H', img, st)
    # the table address is the immediate of `mov si, 028Ch` in the stub (same in all of DOPE's executables)
    assert img[st + 0x136] == 0xbe
    t = st + struct.unpack_from('<H', img, st + 0x137)[0]
    relocs = []
    while True:
        n = img[t]
        t += 1
        if n == 0:
            break
        seg = struct.unpack_from('<H', img, t)[0]
        t += 2
        off = 0
        for _ in range(n):
            off += img[t]
            t += 1
            relocs.append((seg, off))
    return dict(ip=ip, cs=cs, sp=sp, ss=ss, stublen=stublen, packed_paras=packed_paras, relocs=relocs,
                table_end=t - st)


def unpack_python(img, h):
    data, rnc = rnc1_unpack(img)
    si = stub_info(img, h)
    return data, rnc, si


# ---- running the stub ------------------------------------------------------------------------------------------

def unpack_emulated(img, h):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_CODE, UcError
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI,
                                   UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_IP, UC_X86_REG_CS,
                                   UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS)
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x110000)
    psp = LOAD - 0x10
    p = bytearray(256)
    p[0:2] = b'\xcd\x20'
    struct.pack_into('<H', p, 2, MEMTOP)
    p[0x81] = 0x0d
    mu.mem_write(psp * 16, bytes(p))
    mu.mem_write(LOAD * 16, img)
    for r, v in ((UC_X86_REG_DS, psp), (UC_X86_REG_ES, psp), (UC_X86_REG_SS, LOAD + h['ss']), (UC_X86_REG_SP, h['sp']),
                 (UC_X86_REG_CS, LOAD + h['cs']), (UC_X86_REG_IP, h['ip']), (UC_X86_REG_AX, 0), (UC_X86_REG_BX, 0),
                 (UC_X86_REG_CX, 0xff), (UC_X86_REG_DX, psp), (UC_X86_REG_SI, h['ip']), (UC_X86_REG_DI, h['sp']),
                 (UC_X86_REG_BP, 0x91c)):
        mu.reg_write(r, v)
    st = {'jmp': False, 'oep': None, 'n': 0}

    def on_code(mu, addr, size, _):
        st['n'] += 1
        if st['jmp']:
            st['oep'] = (mu.reg_read(UC_X86_REG_CS), mu.reg_read(UC_X86_REG_IP), mu.reg_read(UC_X86_REG_SS),
                         mu.reg_read(UC_X86_REG_SP), mu.reg_read(UC_X86_REG_DS), mu.reg_read(UC_X86_REG_ES))
            mu.emu_stop()
            return
        if size == 3 and bytes(mu.mem_read(addr, 3)) == b'\x2e\xff\x2f':      # jmp far cs:[bx]
            st['jmp'] = True

    def on_intr(mu, intno, _):
        print('  unexpected int %02x' % intno)
        mu.emu_stop()

    mu.hook_add(UC_HOOK_CODE, on_code)
    mu.hook_add(UC_HOOK_INTR, on_intr)
    try:
        mu.emu_start((LOAD + h['cs']) * 16 + h['ip'], 0x110000, count=500_000_000)
    except UcError as e:
        print('  emulation error', e)
    assert st['oep'], 'stub did not finish'
    return mu, st


def main():
    path = sys.argv[1]
    outdir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'unpacked')
    os.makedirs(outdir, exist_ok=True)
    d = open(path, 'rb').read()
    chain = split(d)
    names = ['loader'] + ['p%d' % i for i in range(1, len(chain) - 1)] + ['term']
    info = ['DOPE.EXE: %d bytes, sha256 %s' % (len(d), hashlib.sha256(d).hexdigest()), '',
            'name    file offset  MZ size   header  CS:IP      SS:SP      minalloc  packed  unpacked  chunks  '
            'relocs  sha256 of NAME.bin']
    for name, (o, h) in zip(names, chain):
        img = d[o + h['hdr']:o + h['size']]
        if img[:4] != b'RNC\x01':
            open(os.path.join(outdir, name + '.bin'), 'wb').write(img)
            open(os.path.join(outdir, name + '.exe'), 'wb').write(d[o:o + h['size']])
            info.append('%-7s %#9x    %#7x   %#5x   %04x:%04x  %04x:%04x  %#6x    -       %-8d  -       -       %s'
                        % (name, o, h['size'], h['hdr'], h['cs'], h['ip'], h['ss'], h['sp'], h['minalloc'],
                           len(img), hashlib.sha256(img).hexdigest()))
            print('%s: not packed (%d bytes)' % (name, len(img)))
            continue
        data, rnc, si = unpack_python(img, h)
        # relocate for load segment 0: the stub adds the load segment to each site, the packed data has them as 0-based
        print('%s: file %#x, RNC %d -> %d bytes, %d chunks, %d relocations, entry %04x:%04x, stack %04x:%04x' % (
            name, o, rnc['psize'], rnc['usize'], rnc['chunks'], len(si['relocs']), si['cs'], si['ip'], si['ss'], si['sp']))
        mu, st = unpack_emulated(img, h)
        cs, ip, ss, sp, ds, es = st['oep']
        mem = bytes(mu.mem_read(LOAD * 16, len(data)))
        reloc0 = bytearray(mem)
        for seg, off in si['relocs']:
            a = seg * 16 + off
            v = struct.unpack_from('<H', reloc0, a)[0]
            struct.pack_into('<H', reloc0, a, (v - LOAD) & 0xffff)
        assert bytes(reloc0) == data, 'emulated and Python images differ'
        assert (cs - LOAD, ip, ss - LOAD, sp) == (si['cs'], si['ip'], si['ss'], si['sp']), st['oep']
        assert ds == es == LOAD - 0x10
        print('  stub run: %d instructions, jumps to %04x:%04x (load+%04x), SS:SP load+%04x:%04x; images identical'
              % (st['n'], cs, ip, cs - LOAD, ss - LOAD, sp))
        open(os.path.join(outdir, name + '.bin'), 'wb').write(data)
        # plain MZ: header 0x20 + relocation table, padded to 16 bytes
        nrel = len(si['relocs'])
        hdr_len = (0x1c + 4 * nrel + 15) // 16 * 16
        total_paras = (len(img) + 15) // 16 + h['minalloc']          # what the packed program asked for
        need = max(0, total_paras - (len(data) + 15) // 16)
        size = hdr_len + len(data)
        mz = bytearray(hdr_len)
        struct.pack_into('<14H', mz, 0, 0x5a4d, size % 512, (size + 511) // 512, nrel, hdr_len // 16, need,
                         0xffff, si['ss'], si['sp'], 0, si['ip'], si['cs'], 0x1c, 0)
        for i, (seg, off) in enumerate(si['relocs']):
            struct.pack_into('<HH', mz, 0x1c + 4 * i, off, seg)
        open(os.path.join(outdir, name + '.exe'), 'wb').write(bytes(mz) + data)
        sha = hashlib.sha256(data).hexdigest()
        with open(os.path.join(outdir, name + '.txt'), 'w') as f:
            f.write('%s: DOPE.EXE offset %#x, MZ size %#x, header %#x, packed CS:IP %04x:%04x SS:SP %04x:%04x '
                    'minalloc %#x\n' % (name, o, h['size'], h['hdr'], h['cs'], h['ip'], h['ss'], h['sp'], h['minalloc']))
            f.write('RNC-1: unpacked %d (%#x), packed %d, CRC %04x / %04x, leeway %d, chunks %d\n' % (
                rnc['usize'], rnc['usize'], rnc['psize'], rnc['ucrc'], rnc['pcrc'], rnc['leeway'], rnc['chunks']))
            f.write('stub: length %#x, packed data %#x paragraphs, relocation table end CS:%04x\n' % (
                si['stublen'], si['packed_paras'], si['table_end']))
            f.write('entry CS:IP %04x:%04x  SS:SP %04x:%04x (relative to the load segment)\n' % (
                si['cs'], si['ip'], si['ss'], si['sp']))
            f.write('memory asked for by the packed exe: %#x paragraphs (%d bytes)\n' % (total_paras, total_paras * 16))
            f.write('sha256 %s\n' % sha)
            f.write('relocations (%d, seg:off -> file offset):\n' % nrel)
            for seg, off in si['relocs']:
                f.write('  %04x:%04x  %#07x  word %04x\n' % (seg, off, seg * 16 + off,
                                                            struct.unpack_from('<H', data, seg * 16 + off)[0]))
        info.append('%-7s %#9x    %#7x   %#5x   %04x:%04x  %04x:%04x  %#6x    %-6d  %-8d  %-6d  %-6d  %s' % (
            name, o, h['size'], h['hdr'], si['cs'], si['ip'], si['ss'], si['sp'], h['minalloc'], rnc['psize'],
            rnc['usize'], rnc['chunks'], nrel, sha))
    open(os.path.join(outdir, 'info.txt'), 'w').write('\n'.join(info) + '\n')
    print('\n'.join(info))


if __name__ == '__main__':
    main()

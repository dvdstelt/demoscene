"""Unpack REVE.EXE (Pulse, 1996): run its loaders in unicorn, unpack the files it carries, check against its own code.

REVE.EXE (999,999 bytes) holds (details in NOTES.md, "File layout"):

  file 0x0000..0x4fbc   an MZ executable (header 0x20 bytes, no relocations, entry 0000:0000, SS:SP 0CEC:0800):
                        "Infiny Loader" (** INFINY RULEZ **), 64 layers of a self-decrypting copy loop, then a
                        packed loader that unpacks LCA's "FLAT OS V1.0" extender (16-bit start-up, 32-bit kernel)
                        to the load segment and jumps to it
  file 0x4fbc..end      "LCA's Packer" archive: b"LCA's Packer\\x1a", word 1, then 35 records:
                          dword record size (header + name + data), dword unpacked size, dword checksum,
                          byte method (1 = LZW), name (zero-terminated, e.g. "GFX\\TEXTURE.RAW"), data
                        and a closing text line ('"REVE the return" by PULSE').

The extender loads the whole of REVE.EXE into memory and unpacks a file by name with its service int 0FFh AX=0209h
(kernel 0x8f03: lookup 0x9228, decompressor 0x9284). The first file, REVE.LFL, is the demo: a 0x1c-byte header
("LCA's FLAT OS V1.0", 0xe1 0x1a, dword image size, dword entry), the image, then "LCAr" and the relocation table
(image offsets of dwords to which the load address is added).

LZW (0x9284): codes of 9..13 bits, least significant bit first; 256 = end, 257 = reset (back to 9 bits, next code
258); a new code is added after every code but the first after a reset; the width grows when the next free code
reaches 2^width (up to 13 bits, then the table stops growing). Checksum: s = ror32(s + byte, 3) over the output.

Stage 1 (real mode, unicorn): the MZ program runs from its entry until the extender's first instruction (its
int 21h AH=4Ah); the extender is then in memory at segment 0x1000. Stage 2: every record is unpacked in Python and
by the extender's own routine 0x9284 run in unicorn (flat, the kernel's data segment at linear 0), and the two must
agree; both must give the stored size and checksum.

Outputs (in --outdir, default ./unpacked):
  flatos.bin      the extender as it is in memory at its start (0xc400 bytes from linear 0x10000; its 32-bit
                  kernel's segment starts at offset 0xc60, segment 0x10c6)
  reve_lfl.bin    REVE.LFL unpacked (header, image, relocations)
  reve_img.bin    the image relocated to 0x20427c, where the extender puts it in the headless run (run_reve.py):
                  address A of the running demo is at offset A - 0x20427c of this file
  files/<path>    every other file of the archive, unpacked (paths as stored: ZIC\\, GFX\\, 3D\\)
  files.txt       the archive: offset, record size, unpacked size, checksum, sha256 of the unpacked file

usage (inside the toolbox):  python3 unpack.py ../original/REVE.EXE [--outdir unpacked]
"""
import argparse
import hashlib
import os
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_MODE_32, UC_HOOK_INTR, UC_HOOK_CODE, UcError
from unicorn.x86_const import *

LOAD = 0x1000
IMG_BASE = 0x20427c          # where run_reve.py's extender loads the demo's image
ARCHIVE_MAGIC = b"LCA's Packer\x1a"
KERNEL = 0xc60               # the 32-bit kernel's segment (0x10c6) in flatos.bin
K_LOOKUP, K_LZW = 0x9228, 0x9284
V_TABLE, V_SRC, V_SUM, V_OUTSUM = 0x9d28, 0x9d30, 0x9d38, 0x9d34


def run_loader(exe):
    """Run the MZ program in real mode until the extender starts; return the memory from the load segment."""
    f = struct.unpack_from('<14H', exe, 0)
    cblp, cp, crlc, cparhdr, ss, sp, ip, cs = f[1], f[2], f[3], f[4], f[7], f[8], f[10], f[11]
    size = (cp - 1) * 512 + cblp
    assert crlc == 0
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x110000)
    psp = LOAD - 0x10
    mu.mem_write(0x8000, b'PATH=C:\\\x00\x00\x01\x00C:\\REVE.EXE\x00')
    p = bytearray(256)
    p[0:2] = b'\xcd\x20'
    struct.pack_into('<HH', p, 2, 0xa000, 0)
    struct.pack_into('<H', p, 0x2c, 0x800)
    p[0x81] = 0x0d
    mu.mem_write(psp * 16, bytes(p))
    mu.mem_write(LOAD * 16, exe[cparhdr * 16:size])
    mu.mem_write(0xf0000, b'\xcf' * 256)
    for i in range(256):
        mu.mem_write(i * 4, struct.pack('<HH', i, 0xf000))
    for r, v in ((UC_X86_REG_DS, psp), (UC_X86_REG_ES, psp), (UC_X86_REG_SS, LOAD + ss), (UC_X86_REG_SP, sp),
                 (UC_X86_REG_CS, LOAD + cs), (UC_X86_REG_IP, ip)):
        mu.reg_write(r, v)
    st = {'done': False, 'err': None, 'n': 0}

    def on_intr(mu, intno, _):
        o, sg = struct.unpack('<HH', mu.mem_read(intno * 4, 4))
        if sg != 0xf000:
            sp_ = (mu.reg_read(UC_X86_REG_SP) - 6) & 0xffff
            mu.mem_write(mu.reg_read(UC_X86_REG_SS) * 16 + sp_, struct.pack(
                '<HHH', mu.reg_read(UC_X86_REG_IP), mu.reg_read(UC_X86_REG_CS), mu.reg_read(UC_X86_REG_EFLAGS)))
            mu.reg_write(UC_X86_REG_SP, sp_)
            mu.reg_write(UC_X86_REG_CS, sg)
            mu.reg_write(UC_X86_REG_IP, o)
            return
        ah = mu.reg_read(UC_X86_REG_AX) >> 8
        if intno == 0x21 and ah == 0x4a and mu.reg_read(UC_X86_REG_CS) == LOAD:
            st['done'] = True                      # the extender's first call: it is in place
        else:
            st['err'] = 'int %02x ax=%04x at %04x:%04x' % (intno, mu.reg_read(UC_X86_REG_AX),
                                                          mu.reg_read(UC_X86_REG_CS), mu.reg_read(UC_X86_REG_IP))
        mu.emu_stop()

    mu.hook_add(UC_HOOK_INTR, on_intr)
    try:
        mu.emu_start((LOAD + cs) * 16 + ip, 0x110000, count=50_000_000)
    except UcError as e:
        st['err'] = str(e)
    if not st['done']:
        raise RuntimeError('loader emulation failed: %s' % st['err'])
    return bytes(mu.mem_read(LOAD * 16, 0xc400))


def parse_archive(exe):
    a = exe.find(ARCHIVE_MAGIC)
    assert a > 0, 'no LCA archive'
    count = struct.unpack_from('<H', exe, a + len(ARCHIVE_MAGIC))[0]
    p = a + len(ARCHIVE_MAGIC) + 2
    recs = []
    while p + 13 < len(exe):
        rsize, usize, csum, method = struct.unpack_from('<IIIB', exe, p)
        end = exe.find(b'\0', p + 13, p + 13 + 80)
        if end < 0:
            break
        name = exe[p + 13:end].decode('latin1')
        if not name or rsize < end + 1 - p or p + rsize > len(exe):
            break
        recs.append(dict(offset=p, rsize=rsize, size=usize, csum=csum, method=method, name=name,
                         data=exe[end + 1:p + rsize]))
        p += rsize
    return a, count, recs, exe[p:]


def lzw(src, size):
    """LCA's LZW (kernel 0x9284), and the checksum (0x93f4)."""
    out = bytearray()
    pos = 0                       # bit position in src
    s = 0

    def code(nbits):
        nonlocal pos
        b = pos >> 3
        v = int.from_bytes(src[b:b + 4].ljust(4, b'\0'), 'little') >> (pos & 7)
        pos += nbits
        return v & ((1 << nbits) - 1)

    while True:
        nbits, limit, nxt = 9, 0x200, 0x102
        prefix = {}
        char = {}
        c = code(nbits)
        if c == 0x100:
            break
        old = c
        first = c
        out.append(c)
        while True:
            c = code(nbits)
            if c == 0x100:
                return bytes(out), csum(out)
            if c == 0x101:
                break
            if c >= nxt:                         # KwKwK: the code being defined
                seq = [first]
                x = old
            else:
                seq = []
                x = c
            while x >= 0x100:
                seq.append(char[x])
                x = prefix[x]
            seq.append(x)
            seq.reverse()
            first = seq[0]
            out += bytes(seq)
            prefix[nxt] = old
            char[nxt] = first
            nxt += 1
            old = c
            if nxt >= limit and nbits < 13:
                nbits += 1
                limit <<= 1
    return bytes(out), csum(out)


def csum(data):
    s = 0
    for b in data:
        s = (s + b) & 0xffffffff
        s = ((s >> 3) | (s << 29)) & 0xffffffff
    return s


class KernelLZW:
    """The extender's own decompressor 0x9284, run flat with the kernel's data segment at linear 0."""
    TABLE, SRC, OUT, STACK, RET = 0x100000, 0x200000, 0x800000, 0xff000, 0xfff00

    def __init__(self, flatos):
        mu = Uc(UC_ARCH_X86, UC_MODE_32)
        mu.mem_map(0, 0x1000000)
        mu.mem_write(0, flatos[KERNEL:])
        gdt = bytearray(24)
        gdt[8:16] = struct.pack('<HHBBBB', 0xffff, 0, 0, 0x9a, 0xcf, 0)
        gdt[16:24] = struct.pack('<HHBBBB', 0xffff, 0, 0, 0x92, 0xcf, 0)
        mu.mem_write(0xfe000, bytes(gdt))
        mu.reg_write(UC_X86_REG_GDTR, (0, 0xfe000, 23, 0))
        mu.reg_write(UC_X86_REG_CS, 8)
        for r in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_FS, UC_X86_REG_GS):
            mu.reg_write(r, 0x10)
        mu.mem_write(self.RET, b'\xf4')
        self.mu = mu
        self.clean = bytes(mu.mem_read(0, 0x10000))

    def unpack(self, data, size, checksum):
        mu = self.mu
        mu.mem_write(0, self.clean)
        mu.mem_write(self.SRC, data + bytes(8))
        mu.mem_write(self.OUT, bytes(size + 16))
        mu.mem_write(V_TABLE, struct.pack('<I', self.TABLE))
        mu.mem_write(V_SRC, struct.pack('<I', self.SRC))
        mu.mem_write(V_SUM, struct.pack('<I', checksum))
        mu.mem_write(self.STACK, struct.pack('<I', self.RET))
        mu.reg_write(UC_X86_REG_ESP, self.STACK)
        mu.reg_write(UC_X86_REG_EDI, self.OUT)
        mu.reg_write(UC_X86_REG_EFLAGS, 0x2)
        mu.emu_start(K_LZW, self.RET, count=500_000_000)
        cf = mu.reg_read(UC_X86_REG_EFLAGS) & 1
        return bytes(mu.mem_read(self.OUT, size)), struct.unpack('<I', mu.mem_read(V_OUTSUM, 4))[0], cf


def relocate(lfl, base):
    hdr = lfl[:0x1c]
    assert hdr.startswith(b"LCA's FLAT OS V1.0"), hdr
    isize, entry = struct.unpack_from('<II', lfl, 0x14)
    img = bytearray(lfl[0x1c:0x1c + isize])
    assert lfl[0x1c + isize:0x20 + isize] == b'LCAr'
    rel = lfl[0x20 + isize:]
    offs = struct.unpack('<%dI' % (len(rel) // 4), rel[:len(rel) // 4 * 4])
    for o in offs:
        struct.pack_into('<I', img, o, (struct.unpack_from('<I', img, o)[0] + base) & 0xffffffff)
    return bytes(img), entry, offs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('--outdir', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'unpacked'))
    ap.add_argument('--no-kernel-check', action='store_true', help='skip running the extender\'s decompressor')
    a = ap.parse_args()
    exe = open(a.exe, 'rb').read()
    print('REVE.EXE: %d bytes, sha256 %s' % (len(exe), hashlib.sha256(exe).hexdigest()))
    os.makedirs(os.path.join(a.outdir, 'files'), exist_ok=True)

    flatos = run_loader(exe)
    assert flatos[KERNEL + 0x1deb:KERNEL + 0x1dfd] == b"LCA's FLAT OS V1.0", 'not the expected extender'
    open(os.path.join(a.outdir, 'flatos.bin'), 'wb').write(flatos)
    print('FLAT OS: 0x%x bytes, sha256 %s' % (len(flatos), hashlib.sha256(flatos).hexdigest()))

    start, count, recs, tail = parse_archive(exe)
    print('archive at 0x%x, word %d, %d records, tail %r' % (start, count, len(recs), tail))
    kern = None if a.no_kernel_check else KernelLZW(flatos)
    lines = ['# offset record_size unpacked_size checksum method name sha256']
    for r in recs:
        assert r['method'] == 1, r['name']
        out, s = lzw(r['data'], r['size'])
        ok = len(out) == r['size'] and s == r['csum']
        if kern is not None:
            kout, ks, kcf = kern.unpack(r['data'], r['size'], r['csum'])
            assert kout == out and ks == s and kcf == 0, 'kernel and Python disagree on %s' % r['name']
        assert ok, '%s: size %d/%d checksum %08x/%08x' % (r['name'], len(out), r['size'], s, r['csum'])
        h = hashlib.sha256(out).hexdigest()
        lines.append('0x%06x 0x%06x %7d %08x %d %-16s %s' % (r['offset'], r['rsize'], r['size'], r['csum'],
                                                             r['method'], r['name'], h))
        if r['name'] == 'REVE.LFL':
            open(os.path.join(a.outdir, 'reve_lfl.bin'), 'wb').write(out)
            img, entry, offs = relocate(out, IMG_BASE)
            open(os.path.join(a.outdir, 'reve_img.bin'), 'wb').write(img)
            print('REVE.LFL: image 0x%x bytes, entry %#x, %d relocations; reve_img.bin at 0x%x' % (
                len(img), entry, len(offs), IMG_BASE))
        else:
            path = os.path.join(a.outdir, 'files', *r['name'].split('\\'))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, 'wb').write(out)
        print('  %-16s %7d bytes  ok%s' % (r['name'], r['size'], '' if kern is None else ' (kernel agrees)'))
    open(os.path.join(a.outdir, 'files.txt'), 'w').write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()

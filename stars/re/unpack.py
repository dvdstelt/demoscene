"""Unpack STARS.EXE (NoooN, 1995): run its own loaders in unicorn, check them against Python, extract the overlay.

STARS.EXE is three things in one file (details in NOTES.md, "File layout"):

  file 0x0000..0x00a0   MZ header (no relocations, entry 1250:0000, SS:SP 1250:07da)
  file 0x00a0..0x125a0  an encrypted PKLITE 1.1x executable (its load image, 0x12500 bytes, without MZ header)
  file 0x125a0..0x1297a the "Infiny Loader" stub (entry, self-decrypting, anti-debug through int 1 / int 3 / int 8)
  file 0x1297a..end     the overlay: 58 files (56 DIET-compressed) and a directory at the end, read at run time by
                        the program (0x4a9f3 loads file n: directory entry at end - 12 n, then 0x4ac8b unpacks)

Stage 1, the loader chain, is run in unicorn (real mode, IVT dispatch, minimal DOS, load segment 0x1000):
  Infiny Loader: decrypts the PKLITE image word by word (w = ((((w - 0xbc67 - 0x305e + 0x9af9) ^ 0xc4f5) - 0x7b9b),
  checksum 0x0981), applies the PKLITE image's single relocation and jumps to PSP:0100 (the PKLITE entry);
  PKLITE moves itself to the top of memory, decompresses the program to the load segment, applies 17 relocations
  (all in the 16-bit kernel) and jumps to the program's entry 7a21:0000 (the PMODE 3.07 kernel; the 32-bit
  code segment starts at image offset 0). The emulation runs twice (load segments 0x1000 and 0x1700) to check the
  relocation list. An independent Python implementation (decrypt + PKLITE + relocations) must give the same bytes.
Stage 2, the overlay: the directory is parsed, every DIET-compressed file is unpacked in Python and by the
program's own routine 0x4ad47 run in unicorn, and the two must agree.

Outputs (in --outdir, default ./unpacked; prefix stars):
  stars_img.bin  the program image as PKLITE leaves it in memory at load segment 0x1000 (PSP at 0x0ff0):
                 0x840b0 bytes from the load address (image + uninitialised memory up to the end of the stack,
                 SS:SP = 7c0b:8000 relative to the load segment; bytes past the image are zero here)
  stars_unp.txt  sizes, entry, stack, relocation sites, hashes
  stars_pklite.exe  the decrypted PKLITE executable rebuilt with an MZ header (for other tools)
  files/NN.bin   the overlay's 58 files, decompressed (NN = the number the program loads them by)
  files.txt      the overlay directory: number, offset, stored size, DIET or raw, unpacked size, CRC, hash

usage (inside the toolbox):  python3 unpack.py ../original/STARS.EXE [--outdir unpacked]
"""
import argparse
import hashlib
import os
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_CODE, UcError
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI,
                               UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_IP, UC_X86_REG_CS,
                               UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_EFLAGS)

HERE = os.path.dirname(os.path.abspath(__file__))
LOAD = 0x1000            # load segment (PSP at 0x0ff0), as in paper/re/unpklite.py
MEMTOP = 0xA000          # top of conventional memory
ENVSEG = 0x0800          # environment block
BIOS = 0xF000            # every IVT entry points to an iret at F000:n

# Infiny Loader constants (stub offsets relative to its segment, entry 0)
PROT_KEY = (0xbc67, 0x305e, 0x9af9, 0xc4f5, 0x7b9b)   # sub, sub, add, xor, sub (int 3 handler at stub 0x258)
# PKLITE stub offsets (relative to the PKLITE code segment after it moved itself up)
PK_DATA = 0x3d          # xor si, si: DS = the moved compressed data
PK_END = 0x1c8           # end-of-data branch target: ES:DI = end of the decompressed image
PK_RELOC = 0x1dc         # add es:[di], bx (one relocation)
PK_JUMP = 0x205          # retf to the program


def parse_mz(d, off=0):
    f = struct.unpack_from('<14H', d, off)
    sig, cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, csum, ip, cs, lfarlc, ovno = f
    assert sig == 0x5a4d
    size = (cp - 1) * 512 + cblp if cblp else cp * 512
    return dict(size=size, hdr=cparhdr * 16, nreloc=crlc, minalloc=minalloc, maxalloc=maxalloc, ss=ss, sp=sp,
                ip=ip, cs=cs, lfarlc=lfarlc)


# ---------------------------------------------------------------------------------------------------------------
# Emulation of the loader chain
# ---------------------------------------------------------------------------------------------------------------

def emulate(d, load=LOAD, verbose=False):
    mz = parse_mz(d)
    image = d[mz['hdr']:mz['size']]
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x110000)
    psp = load - 0x10
    env = b'PATH=C:\\\x00ULTRASND=240,3,3,7,7\x00\x00\x01\x00C:\\STARS.EXE\x00'
    mu.mem_write(ENVSEG * 16, env)
    p = bytearray(256)
    p[0:2] = b'\xcd\x20'
    struct.pack_into('<H', p, 2, MEMTOP)
    struct.pack_into('<H', p, 0x2c, ENVSEG)
    p[0x81] = 0x0d
    mu.mem_write(psp * 16, bytes(p))
    mu.mem_write(load * 16, image)
    mu.mem_write(BIOS * 16, b'\xcf' * 256)
    for i in range(256):
        mu.mem_write(i * 4, struct.pack('<HH', i, BIOS))
    R = mu.reg_read
    for r, v in ((UC_X86_REG_DS, psp), (UC_X86_REG_ES, psp), (UC_X86_REG_SS, (load + mz['ss']) & 0xffff),
                 (UC_X86_REG_SP, mz['sp']), (UC_X86_REG_CS, (load + mz['cs']) & 0xffff), (UC_X86_REG_IP, mz['ip']),
                 (UC_X86_REG_AX, 0), (UC_X86_REG_BX, 0), (UC_X86_REG_CX, 0xff), (UC_X86_REG_DX, psp),
                 (UC_X86_REG_SI, mz['ip']), (UC_X86_REG_DI, mz['sp']), (UC_X86_REG_BP, 0x91c)):
        mu.reg_write(r, v)
    prot_cs = (load + mz['cs']) & 0xffff
    st = {'stage': 'protector', 'n': 0, 'pk_cs': None, 'pk_entry': None, 'end': None, 'relocs': [],
          'oep': None, 'pk_image': None, 'regs': None, 'err': None}

    def setcf(c):
        f = R(UC_X86_REG_EFLAGS)
        mu.reg_write(UC_X86_REG_EFLAGS, (f | 1) if c else (f & ~1))

    def on_intr(mu, intno, _):
        o, sg = struct.unpack('<HH', mu.mem_read(intno * 4, 4))
        if sg != BIOS:                      # the program installed its own handler: dispatch like a real CPU
            sp = (R(UC_X86_REG_SP) - 6) & 0xffff
            mu.mem_write(R(UC_X86_REG_SS) * 16 + sp, struct.pack('<HHH', R(UC_X86_REG_IP), R(UC_X86_REG_CS),
                                                                 R(UC_X86_REG_EFLAGS) & 0xffff))
            mu.reg_write(UC_X86_REG_SP, sp)
            mu.reg_write(UC_X86_REG_EFLAGS, R(UC_X86_REG_EFLAGS) & ~0x300)
            mu.reg_write(UC_X86_REG_CS, sg)
            mu.reg_write(UC_X86_REG_IP, o)
            return
        ah = R(UC_X86_REG_AX) >> 8
        if intno == 0x21 and ah == 0x30:
            mu.reg_write(UC_X86_REG_AX, 0x0005)
        elif intno == 0x21 and ah == 0x4a:
            setcf(0)
        elif intno == 0x21 and ah in (0x09, 0x4c):
            a = R(UC_X86_REG_DS) * 16 + R(UC_X86_REG_DX)
            st['err'] = 'int 21h/%02x: %r' % (ah, bytes(mu.mem_read(a, 120)).split(b'$')[0])
            mu.emu_stop()
        elif intno not in (1, 3, 8):
            st['err'] = 'int %02x ax=%04x' % (intno, R(UC_X86_REG_AX))
            mu.emu_stop()

    def on_code(mu, addr, size, _):
        st['n'] += 1
        c = R(UC_X86_REG_CS)
        ip = R(UC_X86_REG_IP)
        if st['stage'] == 'protector' and c != prot_cs and c < 0xF000:
            # Infiny Loader jumps to the PKLITE entry: keep the decrypted, relocated PKLITE image
            st['stage'] = 'pklite-entry'
            st['pk_entry'] = (c, ip, R(UC_X86_REG_SS), R(UC_X86_REG_SP))
            st['pk_image'] = bytes(mu.mem_read(load * 16, mz['cs'] * 16))
            st['stub'] = bytes(mu.mem_read(prot_cs * 16, mz['size'] - mz['hdr'] - mz['cs'] * 16))
        elif st['stage'] == 'pklite-entry' and c != st['pk_entry'][0]:
            st['stage'] = 'pklite'
            st['pk_cs'] = c
        elif st['stage'] == 'pklite' and c == st['pk_cs']:
            if ip == PK_DATA and st.get('data') is None:
                st['data'] = bytes(mu.mem_read(R(UC_X86_REG_DS) * 16, 64))
            if ip == PK_END:
                st['end'] = R(UC_X86_REG_ES) * 16 + R(UC_X86_REG_DI)
            elif ip == PK_RELOC:
                st['relocs'].append(R(UC_X86_REG_ES) * 16 + R(UC_X86_REG_DI) - load * 16)
        elif st['stage'] == 'pklite' and c != st['pk_cs'] and c < 0xF000:
            st['stage'] = 'program'
            st['oep'] = (c, ip)
            st['regs'] = {n: R(r) for n, r in (('ss', UC_X86_REG_SS), ('sp', UC_X86_REG_SP), ('ds', UC_X86_REG_DS),
                                                ('es', UC_X86_REG_ES))}
            mu.emu_stop()

    mu.hook_add(UC_HOOK_INTR, on_intr)
    mu.hook_add(UC_HOOK_CODE, on_code)
    try:
        mu.emu_start(prot_cs * 16 + mz['ip'], 0x110000, count=20_000_000)
    except UcError as e:
        st['err'] = 'emulation error %s at %04x:%04x' % (e, R(UC_X86_REG_CS), R(UC_X86_REG_IP))
    if st['err'] or not st['oep']:
        raise RuntimeError('loader emulation failed: %s (stage %s)' % (st['err'], st['stage']))
    st['mem'] = bytes(mu.mem_read(load * 16, 0x90000))
    st['load'] = load
    if verbose:
        print('emulation: %d instructions, PKLITE entry %04x:%04x, PKLITE ran at %04x, program entry %04x:%04x' % (
            st['n'], st['pk_entry'][0], st['pk_entry'][1], st['pk_cs'], st['oep'][0], st['oep'][1]))
    return st


# ---------------------------------------------------------------------------------------------------------------
# Independent Python implementation
# ---------------------------------------------------------------------------------------------------------------

def py_decrypt(enc):
    """Infiny Loader: per 16-bit word, then a checksum over the decrypted words."""
    out = bytearray(len(enc))
    ck = 0
    a1, a2, a3, x, a4 = PROT_KEY
    for i in range(0, len(enc), 2):
        w = struct.unpack_from('<H', enc, i)[0]
        w = ((((w - a1 - a2 + a3) & 0xffff) ^ x) - a4) & 0xffff
        struct.pack_into('<H', out, i, w)
        ck ^= w
        ck = (ck + (w & 0xff) * (w >> 8)) & 0xffff      # mul ah: ax = al * ah
    return bytes(out), ck


def py_prot_header(stub):
    """The PKLITE executable's MZ fields kept by the stub (decrypted stub offsets 0x2d0..)."""
    ss, sp, cs, ip, ck = struct.unpack_from('<5H', stub, 0x2d0)
    nrel = struct.unpack_from('<H', stub, 0x2f6)[0]
    return dict(ss=ss, sp=sp, cs=cs, ip=ip, checksum=ck, nreloc=nrel)


def pklite_decompress(src, load):
    """PKLITE 1.1x (large model, no 'extra' compression) decompressor, a transcription of the stub's 0x35..0x205.

    src: the PKLITE executable's load image (after Infiny Loader). The compressed data starts at image offset
    0x290 (the decompressor at 0x44 computes it as PSP + 0x39 paragraphs: `add bx, 0x39`). Returns (image relocated
    for `load`, the raw image, relocation sites (image offsets), (ss, sp, cs, ip) relative to the load segment,
    end offset of the compressed data and trailer in src).
    """
    p = 0x290
    s = src
    out = bytearray()
    bits = 0
    nb = 0

    def word():
        nonlocal p
        w = s[p] | s[p + 1] << 8
        p += 2
        return w

    def byte():
        nonlocal p
        b = s[p]
        p += 1
        return b

    def bit():
        """shr bp,1; dec dx; if zero: lodsw / xchg bp,ax / mov dl,16 (the refill follows the 16th bit)."""
        nonlocal bits, nb
        b = bits & 1
        bits >>= 1
        nb -= 1
        if nb == 0:
            bits = word()
            nb = 16
        return b

    LEN1 = {0: 4, 2: 5, 3: 6, 4: 7, 10: 8, 11: 9, 12: 10}           # stub table at 0x206
    LEN2 = {2: 11, 3: 12, 4: 25, 10: 13, 11: 14, 12: 15}            # 0x211 (25: + next byte)
    LEN3 = {2: 16, 3: 17, 4: 18, 10: 19, 11: 20, 12: 21, 13: 22, 14: 23, 15: 24}   # 0x21c
    OFS = {0: 1, 1: 2, 4: 3, 5: 4, 6: 5, 7: 6, 16: 7, 17: 8, 18: 9, 19: 10, 20: 11, 21: 12, 22: 13}  # 0x22c

    bits = word()
    nb = 16
    while True:
        if not bit():                                   # 0x7c: literal
            out.append(byte())
            continue
        bx = bit()                                      # 0x84: length
        bx = bx << 1 | bit()
        if bx >= 2:
            ln = bx                                     # '10' = 2, '11' = 3
        else:
            bx = bx << 1 | bit()
            if bx == 0:
                ln = LEN1[0]
            else:
                bx = bx << 1 | bit()
                if bx < 5:
                    ln = LEN1[bx]
                else:
                    bx = bx << 1 | bit()
                    if bx <= 12:
                        ln = LEN1[bx]
                    else:                               # 0x13a
                        bx = (bx & 3) << 1 | bit()
                        if bx >= 5:
                            bx = bx << 1 | bit()
                        if bx <= 12:
                            ln = LEN2[bx]
                        else:                           # 0x1a2
                            bx = (bx & 3) << 1 | bit()
                            if bx >= 5:
                                bx = bx << 1 | bit()
                            ln = LEN3[bx]
                        if ln == 25:                    # 0x16a
                            b = byte()
                            if b >= 0xfe:               # 0xfe: segment normalisation (no output), 0xff: end
                                if b == 0xff:
                                    break
                                continue
                            ln = 25 + b
        hi = 0                                          # 0xc5: offset high byte
        if ln != 2 and not bit():
            bx = bit()
            bx = bx << 1 | bit()
            bx = bx << 1 | bit()
            if bx >= 2:
                bx = bx << 1 | bit()
                if bx >= 8:
                    bx = bx << 1 | bit()
                    if bx >= 0x17:
                        bx = bx << 1 | bit()
                        hi = bx & 0xdf
            if not hi:
                hi = OFS[bx]
        off = hi << 8 | byte()
        for _ in range(ln):
            out.append(out[len(out) - off])
    end = p
    # relocation table: groups of (count byte, segment word, count offset words), count 0 ends; then SS SP CS IP
    relocs = []
    while True:
        n = byte()
        if n == 0:
            break
        seg = word()
        for _ in range(n):
            relocs.append(seg * 16 + word())
    ss, sp, cs, ip = word(), word(), word(), word()
    img = bytearray(out)
    for r in relocs:
        struct.pack_into('<H', img, r, (struct.unpack_from('<H', img, r)[0] + load) & 0xffff)
    return bytes(img), out, relocs, (ss, sp, cs, ip), end


# ---------------------------------------------------------------------------------------------------------------
# The overlay: the demo's files (directory at the end of STARS.EXE, DIET-compressed data)
# ---------------------------------------------------------------------------------------------------------------

DIET_SIG = b'\xb4\x4c\xcd\x21\x9d\x89dlz'


def overlay_dir(d):
    """Directory at the end of the file, read backwards: entry n (1, 2, ...) is the 12 bytes at len - 12 * n:
    dword n, int32 offset from the end of the file (negative), dword size. Returns [(n, file offset, size)]."""
    ents = []
    n = 1
    while 12 * n <= len(d):
        num, off, size = struct.unpack_from('<IiI', d, len(d) - 12 * n)
        if num != n:
            break
        ents.append((n, len(d) + off, size))
        n += 1
    return ents


def diet_header(h):
    """17-byte DIET data header: signature (9), byte 9 = compressed size bits 16-23, word 10 = bits 0-15 (size after
    the header), word 12 = CRC-16 (poly 0xa001, init 0) of the compressed bytes, byte 14 bits 2-7 = original size
    bits 16-21 (bits 0-1 unused, 0 here), word 15 = original size bits 0-15."""
    assert h[:9] == DIET_SIG
    csize = h[9] << 16 | struct.unpack_from('<H', h, 10)[0]
    crc = struct.unpack_from('<H', h, 12)[0]
    osize = (h[14] >> 2) << 16 | struct.unpack_from('<H', h, 15)[0]
    return csize, crc, osize


def crc16(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xa001 if c & 1 else c >> 1
    return c


def diet_decompress(src):
    """DIET 1.4x data decompressor, a transcription of the demo's 0x4ad47 (the same code as DOS32's loader, see
    jizz/re/unadam.py). Bit buffer: 16-bit little-endian words, LSB first, the next word is loaded as soon as the
    16th bit of the current one has been taken. Returns (data, bytes of src used)."""
    out = bytearray()
    p = 0x11
    bits = src[p] | src[p + 1] << 8
    p += 2
    nb = 16

    def byte():
        nonlocal p
        b = src[p]
        p += 1
        return b

    def bit():
        nonlocal bits, nb, p
        b = bits & 1
        bits >>= 1
        nb -= 1
        if nb == 0:
            bits = src[p] | src[p + 1] << 8
            p += 2
            nb = 16
        return b

    while True:
        if bit():                                       # 1: literal
            out.append(byte())
            continue
        b2 = bit()
        bl = byte()
        bh = 0xff
        if b2:                                          # 01 <byte> ...: long match
            bh = (bh << 1 | bit()) & 0xff
            if not bit():
                dh = 2
                for _ in range(3):
                    if bit():
                        break
                    bh = (bh << 1 | bit()) & 0xff
                    dh = (dh << 1) & 0xff
                bh = (bh - dh) & 0xff
            ln = 3
            while ln < 7 and not bit():                 # 1 = 3, 01 = 4, 001 = 5, 0001 = 6
                ln += 1
            if ln == 7:                                 # 0000: then 1x = 7 + x, 01 <byte> = byte + 17, 00 xxx = 9 + x
                if bit():
                    ln = 7 + bit()
                elif bit():
                    ln = byte() + 0x11
                else:
                    x = 0
                    for _ in range(3):
                        x = x << 1 | bit()
                    ln = x + 9
        elif bit():                                     # 001 <byte> xxx: copy 2, offset high byte (0xf8 | xxx) - 1
            for _ in range(3):
                bh = (bh << 1 | bit()) & 0xff
            bh = (bh - 1) & 0xff
            ln = 2
        elif bl != 0xff:                                # 000 <byte>: copy 2 from -0x100 + byte
            ln = 2
        elif bit():                                     # 000 ff 1: nothing
            continue
        else:                                           # 000 ff 0: end
            break
        off = (bh << 8 | bl) - 0x10000
        for _ in range(ln):
            out.append(out[len(out) + off])
    return bytes(out), p


def emulate_diet(img, src):
    """Run the demo's own decompressor 0x4ad47 (ESI = source, EAX = destination) in unicorn, flat 32-bit, image at
    linear 0; it ends with `pop eax; call 0x17c8e` (free the source) at 0x4adaf, where we stop."""
    from unicorn import UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_EAX as EAX, UC_X86_REG_ESI as ESI, UC_X86_REG_EDI as EDI, \
        UC_X86_REG_ESP as ESP
    _, _, osize = diet_header(src)
    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    size = 0x100000 + ((len(src) + osize + 0x10fff) & ~0xffff)
    mu.mem_map(0, size)
    mu.mem_write(0, img)
    SRC = 0x100000
    DST = (SRC + len(src) + 0xfff) & ~0xfff
    mu.mem_write(SRC, src)
    mu.reg_write(ESI, SRC)
    mu.reg_write(EAX, DST)
    mu.reg_write(ESP, 0xff000)
    mu.mem_write(0xff000, struct.pack('<I', 0xdead))
    mu.emu_start(0x4ad47, 0x4adaf, count=200_000_000)
    end = mu.reg_read(EDI)
    return bytes(mu.mem_read(DST, end - DST))


def extract_overlay(d, img, outdir, check=True):
    ents = overlay_dir(d)
    os.makedirs(os.path.join(outdir, 'files'), exist_ok=True)
    rows = []
    for n, pos, size in ents:
        raw = d[pos:pos + size]
        if raw[6:9] == b'dlz':                          # the program's test (0x4ac8c): 'dl' at +6, 'z' at +8
            csize, crc, osize = diet_header(raw)
            assert csize + 0x11 == size and crc16(raw[0x11:]) == crc, n
            data, used = diet_decompress(raw)
            assert used == size and len(data) == osize, (n, used, size, len(data), osize)
            if check:
                assert emulate_diet(img, raw) == data, 'file %d: Python DIET differs from the program' % n
            kind = 'DIET'
        else:
            data, kind, crc = raw, 'raw', 0
        open(os.path.join(outdir, 'files', '%02d.bin' % n), 'wb').write(data)
        rows.append((n, pos, size, kind, len(data), crc, hashlib.sha256(data).hexdigest()[:16]))
    return ents, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('--outdir', default=os.path.join(HERE, 'unpacked'))
    ap.add_argument('--no-check', action='store_true', help="skip running the program's DIET routine per file")
    a = ap.parse_args()
    d = open(a.exe, 'rb').read()
    mz = parse_mz(d)
    print('STARS.EXE: %d bytes, MZ image %#x..%#x (header %#x), entry %04x:%04x, SS:SP %04x:%04x, %d relocations, '
          'overlay %#x..%#x (%d bytes)' % (len(d), mz['hdr'], mz['size'], mz['hdr'], mz['cs'], mz['ip'], mz['ss'],
                                          mz['sp'], mz['nreloc'], mz['size'], len(d), len(d) - mz['size']))
    st = emulate(d, verbose=True)
    st2 = emulate(d, load=0x1700)
    # --- Python: Infiny Loader ---
    enc = d[mz['hdr']:mz['hdr'] + mz['cs'] * 16]
    hdr = py_prot_header(st['stub'])           # the stub decrypts itself in two layers; read its fields from memory
    pk, ck = py_decrypt(enc)
    print('Infiny Loader: %#x bytes decrypted, checksum %#06x (stored %#06x), PKLITE entry %04x:%04x SS:SP %04x:%04x,'
          ' %d relocation(s)' % (len(enc), ck, hdr['checksum'], hdr['cs'], hdr['ip'], hdr['ss'], hdr['sp'],
                                 hdr['nreloc']))
    assert ck == hdr['checksum']
    # the PKLITE image's own relocation: the word at offset 7 (the immediate of the stub's "add ax, <load segment>")
    pkr = bytearray(pk)
    struct.pack_into('<H', pkr, 7, (struct.unpack_from('<H', pkr, 7)[0] + LOAD) & 0xffff)
    assert bytes(pkr) == st['pk_image'], 'Python decryption differs from the emulated loader'
    print('Infiny Loader (Python) = emulation; PKLITE image relocation at offset 7 (+ load segment)')
    # --- Python: PKLITE ---
    img, raw, relocs, (ss, sp, cs, ip), used = pklite_decompress(pk, LOAD)
    n = len(img)
    assert st['end'] - LOAD * 16 == n, (hex(st['end']), hex(n))
    assert sorted(relocs) == sorted(st['relocs']), 'relocation lists differ'
    assert img == st['mem'][:n], 'Python PKLITE output differs from the emulated loader'
    assert (st['oep'][0] - LOAD, st['oep'][1]) == (cs, ip)
    # relocation check with a second load segment: the images differ by the segment delta exactly at the relocations
    a2 = bytearray(st2['mem'][:n])
    for r in relocs:
        struct.pack_into('<H', a2, r, (struct.unpack_from('<H', a2, r)[0] - 0x1700 + LOAD) & 0xffff)
    assert bytes(a2) == img, 'loads at two segments differ outside the relocations'
    print('PKLITE: %#x compressed bytes -> %#x bytes, %d relocations, entry %04x:%04x, SS:SP %04x:%04x '
          '(Python = emulation, at two load segments)' % (used - 0x290, n, len(relocs), cs, ip, ss, sp))
    memsize = ss * 16 + sp
    full = img + bytes(memsize - n)
    os.makedirs(a.outdir, exist_ok=True)
    open(os.path.join(a.outdir, 'stars_img.bin'), 'wb').write(full)
    # PKLITE executable with a rebuilt MZ header (for other tools)
    pkhdr = bytearray(0x20)
    pages, last = (len(pk) + 0x20 + 511) // 512, (len(pk) + 0x20) % 512
    struct.pack_into('<14H', pkhdr, 0, 0x5a4d, last, pages, 1, 2, 0x83fc - 0x1250 - 2, 0xffff, hdr['ss'], hdr['sp'],
                     0, hdr['ip'], hdr['cs'], 0x1c, 0)
    struct.pack_into('<HH', pkhdr, 0x1c, 7, 0)
    open(os.path.join(a.outdir, 'stars_pklite.exe'), 'wb').write(bytes(pkhdr) + pk)
    with open(os.path.join(a.outdir, 'stars_unp.txt'), 'w') as f:
        f.write('# STARS.EXE unpacked by unpack.py (Infiny Loader + PKLITE emulated, checked against Python)\n')
        f.write('load_segment %#x\nimage %#x\nmemory %#x\nentry %04x:%04x\nstack %04x:%04x\n' % (
            LOAD, n, memsize, cs, ip, ss, sp))
        f.write('overlay_offset %#x\noverlay_size %#x\n' % (mz['size'], len(d) - mz['size']))
        f.write('sha256_image %s\n' % hashlib.sha256(img).hexdigest())
        f.write('sha256_image_unrelocated %s\n' % hashlib.sha256(raw).hexdigest())
        f.write('relocations %d\n' % len(relocs))
        for i in range(0, len(relocs), 16):
            f.write('reloc %s\n' % ' '.join('%x' % r for r in relocs[i:i + 16]))
    print('wrote stars_img.bin (%#x bytes: image %#x + zeroed memory up to SS:SP), sha256 %s' % (
        len(full), n, hashlib.sha256(img).hexdigest()))
    # --- the overlay ---
    ents, rows = extract_overlay(d, full, a.outdir, check=not a.no_check)
    with open(os.path.join(a.outdir, 'files.txt'), 'w') as f:
        f.write('# STARS.EXE overlay: n, file offset, stored size, kind, unpacked size, DIET CRC, sha256[:16]\n')
        for r in rows:
            f.write('%2d %#08x %#07x %-4s %#07x %04x %s\n' % r)
    print('overlay: %d files from %#x to the directory at %#x (12 bytes each, read backwards from the end); '
          "%d DIET-compressed (Python = the program's 0x4ad47%s, sizes and CRCs check), %d stored; files/NN.bin" % (
              len(ents), ents[0][1], len(d) - 12 * len(ents), sum(r[3] == 'DIET' for r in rows),
              '' if not a.no_check else ' not checked', sum(r[3] == 'raw' for r in rows)))


if __name__ == '__main__':
    main()

"""Unpack JIZZ.EXE (DOS32 v3.5 "Adam" executable) exactly as the DOS32 kernel loads it.

JIZZ.EXE = DOS32 v3.5 rev 6 MZ stub (the extender, 0x2330 bytes of MZ image) + an "Adam" executable at file
offset 0x2330 (the MZ image size). The Adam header (0x2c bytes, read by the kernel into its data at 0x4760) is:

  +00 'Adam'  +04 dlink version 0x0350  +06 min DOS32 version 0x0200
  +08 contents size (bytes after the header)  +0c image size (header + contents)  +10 header size
  +14 eip  +18 memory size  +1c esp  +20 program size  +24 flags  +28 (unused)

The kernel's loader (stub image offsets, 16-bit code with 32-bit registers, DS = CS = kernel segment):
  0x0f2b  reads the MZ header, seeks to the Adam header (0x110a reads 0x2c bytes to [0x4760])
  0x0f82  flags bit 2 ([0x4786] & 4) set: no banner. Otherwise [0x60] = '$' terminates the banner string.
  0x0fcc  version must be 0x350;  0x0fe5 int 31h AX=EE42 allocates [memory size] bytes -> [0x475c] = base
  0x115b  load: flags bit 0 clear -> read contents raw; set -> 0x119f decompress (needs min version >= 0x140)
          from the file through an 8 KB buffer at 0x2420 (refill 0x1080 reads 0x2000 bytes per call), to base.
          LZ with a 16-bit bit buffer in BP (LSB first, refilled as soon as the 16th bit is used, LZEXE style):
            1                      literal byte
            0 0 0 <byte != ff>     copy 2 from -0x100 + byte
            0 0 0 <byte ff> b      b = 1: no-op, b = 0: end of data
            0 0 1 <byte> bbb       copy 2 from ((0xf8|bbb) - 1) * 0x100 + byte - 0x10000
            0 1 <byte> <hi> <len>  long match: high offset byte from a prefix code (0x11c8..0x11fc),
                                   length 3..8 unary-ish, 9..16 (3 bits), 17..272 (byte + 0x11)
  0x12f4  relocation: the decompressed data continues after [program size] with a byte stream. Each byte is
          rotated left by 5 (by 0 when the banner was printed, [0x60] == '$'): bit 0 = continuation, bits 1-7 =
          7 bits of the displacement, most significant group first. After a displacement: if the next byte is 0
          (consumed) the site gets the 16-bit data selector (mov es:[edi], es; es = [0x13a8]), otherwise the
          dword at the site gets base added.
  0x1350  zero-fills from program size to memory size (overwriting the relocation stream).
  0x1028  ds = es = fs = gs = ss = [0x13a8] (flat data selector), esp = esp + base, jmp far [0x13aa]:eip + base.

This script runs the kernel's own routine 0x115b in unicorn (real mode; QEMU does not check segment limits,
so the es:[edi] accesses with 32-bit EDI reach the whole address space, as in DOS32's protected-mode code),
with int 21h / the real-mode call helper 0x1ba2 serving file reads. It does that twice, at base 0 and at base
0x100000, and checks the two images differ by exactly the base at every offset relocation. It also decompresses
and relocates with an independent Python implementation and checks that it gives the same bytes.

Output (prefix given as the second argument):
  <out>_img.bin   the program image as loaded at linear base 0 (memory size bytes; addresses = image offsets,
                  BSS zeroed). Relocated for base 0, so offset relocations are no-ops; selector sites hold 0x18.
  <out>_unp.txt   header values, sizes, hash, selector fixups and the offset relocations (to load at another base)

usage: python3 unadam.py ../original/JIZZ.EXE jizz
"""
import hashlib
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_CODE, UcError
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI,
                               UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_IP, UC_X86_REG_CS,
                               UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_FS, UC_X86_REG_GS,
                               UC_X86_REG_EFLAGS)

KSEG = 0x8000          # real-mode segment of the kernel (DS = CS)
KLIN = KSEG * 16
SSEG = 0x9000          # stack segment
SENTINEL = 0xfff0      # return IP of the call to 0x115b
HANDLE = 5


def parse(d):
    sig, cblp, cp = struct.unpack_from('<HHH', d, 0)
    assert sig == 0x5a4d
    mzsize = (cp - 1) * 512 + cblp if cblp else cp * 512
    hdr_paras = struct.unpack_from('<H', d, 8)[0]
    stub = d[hdr_paras * 16:mzsize]
    h = d[mzsize:mzsize + 0x2c]
    assert h[:4] == b'Adam', h[:4]
    names = ('contents', 'image', 'header', 'eip', 'memory', 'esp', 'program', 'flags', 'extra')
    ver, minver = struct.unpack_from('<HH', h, 4)
    v = dict(zip(names, struct.unpack_from('<9I', h, 8)))
    v.update(mzsize=mzsize, version=ver, minver=minver, stub=stub, hdr=h)
    return v


def run_loader(d, v, base):
    """Run the kernel's 0x115b with allocation base `base`. Returns (memory image, offset fixups, selector
    fixups, decompressed length)."""
    stub = v['stub']
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    es_sel = struct.unpack_from('<H', stub, 0x13a8)[0]        # data selector (0x18); real mode: base sel*16
    es_lin = es_sel * 16
    top = (max(es_lin + base + v['memory'], 0xa0000) + 0xfffff) & ~0xfffff
    mu.mem_map(0, top)
    k = bytearray(0x10000)
    k[:len(stub)] = stub                                       # kernel image; 0x2330..0x5240 is its BSS (zero)
    k[0x4760:0x4760 + 0x2c] = v['hdr']                         # the Adam header as read by 0x110a
    if not (v['flags'] >> 16) & 4:
        k[0x60] = 0x24                                         # banner printed: '$' at 0x60 (no rotation)
    struct.pack_into('<I', k, 0x475c, base)                    # int 31h EE42 result
    struct.pack_into('<H', k, 0x482c, HANDLE)
    k[0x13cd] = 0                                              # no ctrl-break polling
    mu.mem_write(KLIN, bytes(k))
    pos = [v['mzsize'] + 0x2c]                                 # file position after the header
    st = {'fix32': [], 'fixsel': [], 'declen': None, 'err': None, 'reads': 0}

    def dos(ah, al):
        if ah == 0x42:
            off = mu.reg_read(UC_X86_REG_EDX) & 0xffff | (mu.reg_read(UC_X86_REG_ECX) & 0xffff) << 16
            pos[0] = {0: 0, 1: pos[0], 2: len(d)}[al] + off
            return 0
        if ah == 0x3f:
            n = mu.reg_read(UC_X86_REG_ECX) & 0xffff
            dst = mu.reg_read(UC_X86_REG_DS) * 16 + (mu.reg_read(UC_X86_REG_EDX) & 0xffff)
            chunk = d[pos[0]:pos[0] + n]
            mu.mem_write(dst, chunk)
            pos[0] += len(chunk)
            st['reads'] += 1
            return len(chunk)
        if ah == 0x3e:
            return 0
        raise RuntimeError('int 21h ah=%02x' % ah)

    def setret(ax):
        mu.reg_write(UC_X86_REG_EAX, (mu.reg_read(UC_X86_REG_EAX) & 0xffff0000) | ax)
        mu.reg_write(UC_X86_REG_EFLAGS, mu.reg_read(UC_X86_REG_EFLAGS) & ~1)

    def on_intr(mu, intno, _):
        eax = mu.reg_read(UC_X86_REG_EAX)
        if intno != 0x21:
            st['err'] = 'int %x ax=%04x' % (intno, eax & 0xffff)
            mu.emu_stop()
            return
        setret(dos((eax >> 8) & 0xff, eax & 0xff))

    def on_code(mu, addr, size, _):
        ip = addr - KLIN
        if ip == 0x1ba2:            # real-mode int call helper (int 31h AX=0300): int number pushed, ret 2
            eax = mu.reg_read(UC_X86_REG_EAX)
            setret(dos((eax >> 8) & 0xff, eax & 0xff))
            sp = mu.reg_read(UC_X86_REG_ESP) & 0xffff
            ret = struct.unpack('<H', mu.mem_read(SSEG * 16 + sp, 2))[0]
            mu.reg_write(UC_X86_REG_ESP, sp + 4)
            mu.reg_write(UC_X86_REG_IP, ret)
        elif ip == 0x12f4:          # decompression done: EDI = end of output
            st['declen'] = mu.reg_read(UC_X86_REG_EDI) - base
        elif ip == 0x1341:          # add dword es:[edi], ebp
            st['fix32'].append(mu.reg_read(UC_X86_REG_EDI) - base)
        elif ip == 0x1348:          # mov word es:[edi], es
            st['fixsel'].append(mu.reg_read(UC_X86_REG_EDI) - base)
        elif ip in (0x106a, 0x1075, 0x107b, 0x1ac0):
            st['err'] = 'loader error path %x' % ip
            mu.emu_stop()

    mu.hook_add(UC_HOOK_INTR, on_intr)
    mu.hook_add(UC_HOOK_CODE, on_code, None, KLIN, KLIN + 0x2330)
    for r, val in ((UC_X86_REG_CS, KSEG), (UC_X86_REG_DS, KSEG), (UC_X86_REG_ES, KSEG), (UC_X86_REG_FS, 0),
                   (UC_X86_REG_GS, 0), (UC_X86_REG_SS, SSEG), (UC_X86_REG_ESP, 0xfff0)):
        mu.reg_write(r, val)
    mu.mem_write(SSEG * 16 + 0xfff0, struct.pack('<H', SENTINEL))
    try:
        mu.emu_start(KLIN + 0x115b, KLIN + SENTINEL, count=500_000_000)
    except UcError as e:
        st['err'] = 'emulation error %s at %04x' % (e, mu.reg_read(UC_X86_REG_IP))
    if st['err']:
        raise RuntimeError(st['err'])
    if mu.reg_read(UC_X86_REG_EFLAGS) & 1:
        raise RuntimeError('loader returned carry (error)')
    img = bytes(mu.mem_read(es_lin + base, v['memory']))
    return img, st['fix32'], st['fixsel'], st['declen'], es_sel, st['reads']


def py_decompress(src):
    """Independent implementation of the kernel's decompressor 0x119f..0x12f3."""
    out = bytearray()
    p = 0
    bp = n = 0

    def byte():
        nonlocal p
        b = src[p]
        p += 1
        return b

    def bit():
        nonlocal bp, n, p
        b = bp & 1
        bp >>= 1
        n -= 1
        if n == 0:
            bp = src[p] | src[p + 1] << 8
            p += 2
            n = 16
        return b

    n = 1
    bit()                                   # 0x11bc: loads the first word, the bit itself is discarded
    while True:
        if bit():                           # 0x127d
            out.append(byte())
            continue
        b2 = bit()                          # 0x1296
        bl = byte()
        bh = 0xff
        if b2:                              # 0x11c8: long match
            bh = (bh << 1 | bit()) & 0xff
            if not bit():
                dh = 2
                for _ in range(3):
                    if bit():
                        break
                    bh = (bh << 1 | bit()) & 0xff
                    dh = (dh << 1) & 0xff
                bh = (bh - dh) & 0xff
            dh = 2                          # 0x11fe: length
            ln = None
            for _ in range(4):
                dh += 1
                if bit():
                    ln = dh
                    break
            if ln is None:
                if bit():
                    ln = 7 + bit()          # 0x121c
                elif bit():                 # 0x122f
                    ln = byte() + 0x11
                else:
                    x = 0
                    for _ in range(3):
                        x = x << 1 | bit()
                    ln = x + 9
        elif bit():                         # 0x12b7 -> 0x1261: 3 high bits, length 2
            for _ in range(3):
                bh = (bh << 1 | bit()) & 0xff
            bh = (bh - 1) & 0xff
            ln = 2
        elif bl != 0xff:                    # 0x12c2: short match, length 2
            ln = 2
        elif bit():                         # 0x12c6: ff marker, continue
            continue
        else:
            break                           # end of data
        off = (bh << 8 | bl) - 0x10000
        for _ in range(ln):
            out.append(out[len(out) + off])
    return bytes(out), p


def py_relocate(dec, v, base, sel, rot):
    mem = bytearray(v['memory'])
    mem[:len(dec)] = dec
    end = len(dec)
    esi = v['program']
    edi = 0
    fix32, fixsel = [], []
    done = False
    while not done:                         # 0x1313
        eax = 0
        while True:                         # 0x1316
            assert esi <= end, 'relocation stream overrun'
            if esi == end:
                done = True
                break
            b = mem[esi]
            al = ((b << rot) | (b >> (8 - rot))) & 0xff if rot else b
            esi += 1
            eax = (eax & ~0xff | al) & 0xffffffff
            if not al & 1:
                break
            eax = (eax << 7) & 0xffffffff
        if done:
            break
        edi += eax >> 1                     # 0x132f
        if esi == end or mem[esi] != 0:     # add dword [edi], base
            struct.pack_into('<I', mem, edi, (struct.unpack_from('<I', mem, edi)[0] + base) & 0xffffffff)
            fix32.append(edi)
        else:                               # 0 byte: mov word [edi], data selector
            struct.pack_into('<H', mem, edi, sel)
            fixsel.append(edi)
            esi += 1
    mem[v['program']:] = bytes(v['memory'] - v['program'])
    return bytes(mem), fix32, fixsel


def main(path, out):
    d = open(path, 'rb').read()
    v = parse(d)
    print('MZ image %#x, Adam v%x min %x, contents %#x image %#x header %#x eip %#x memory %#x esp %#x '
          'program %#x flags %#x' % (v['mzsize'], v['version'], v['minver'], v['contents'], v['image'], v['header'],
                                     v['eip'], v['memory'], v['esp'], v['program'], v['flags']))
    assert v['version'] == 0x350 and v['flags'] >> 16 & 1, 'not a compressed v3.5 Adam executable'
    img0, f32, fsel, declen, sel, reads = run_loader(d, v, 0)
    imgB, f32B, fselB, _, _, _ = run_loader(d, v, 0x100000)
    print('loader (emulated): decompressed %#x bytes (program %#x + relocation stream %#x) in %d reads, '
          '%d offset fixups, %d selector fixups (selector %#x)' % (declen, v['program'], declen - v['program'],
                                                                   reads, len(f32), len(fsel), sel))
    assert f32 == f32B and fsel == fselB
    # base check: the two loads differ by exactly 0x100000 at each offset fixup and nowhere else
    a = bytearray(imgB)
    for o in f32:
        struct.pack_into('<I', a, o, (struct.unpack_from('<I', a, o)[0] - 0x100000) & 0xffffffff)
    assert bytes(a) == img0, 'base 0 and base 0x100000 loads differ outside the fixups'
    # independent Python implementation
    src = d[v['mzsize'] + v['header']:v['mzsize'] + v['header'] + v['contents']]
    dec, used = py_decompress(src)
    rot = 0 if not (v['flags'] >> 16) & 4 else 5
    pimg, pf32, pfsel = py_relocate(dec, v, 0, sel, rot)
    assert len(dec) == declen, (len(dec), declen)
    assert pimg == img0 and pf32 == f32 and pfsel == fsel, 'python reimplementation differs'
    print('python reimplementation: identical (%#x of %#x source bytes used)' % (used, len(src)))
    h = hashlib.sha256(img0).hexdigest()
    hp = hashlib.sha256(img0[:v['program']]).hexdigest()
    open(out + '_img.bin', 'wb').write(img0)
    with open(out + '_unp.txt', 'w') as f:
        f.write('# %s unpacked by unadam.py (DOS32 v3.5 loader 0x115b emulated)\n' % path.split('/')[-1])
        f.write('base 0\neip %#x\nesp %#x\nmemory %#x\nprogram %#x\n' % (v['eip'], v['esp'], v['memory'],
                                                                       v['program']))
        f.write('adam_offset %#x\nheader %#x\ncontents %#x\nflags %#x\nversion %#x\nminver %#x\n' % (
            v['mzsize'], v['header'], v['contents'], v['flags'], v['version'], v['minver']))
        f.write('decompressed %#x\nreloc_stream %#x\n' % (declen, declen - v['program']))
        f.write('data_selector %#x\ncode_selector %#x\n' % (sel, struct.unpack_from('<H', v['stub'], 0x13aa)[0]))
        f.write('sha256_image %s\nsha256_program %s\n' % (h, hp))
        f.write('selector_fixups %s\n' % ' '.join('%x' % o for o in fsel))
        f.write('offset_fixups %d\n' % len(f32))
        for i in range(0, len(f32), 16):
            f.write('reloc32 %s\n' % ' '.join('%x' % o for o in f32[i:i + 16]))
    print('wrote %s_img.bin (%d bytes, sha256 %s; program part sha256 %s) and %s_unp.txt' % (
        out, len(img0), h, hp, out))
    print('selector fixups at', ' '.join('%x' % o for o in fsel))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])

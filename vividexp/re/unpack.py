"""Unpack VIVID.EXE: the PMODE/W v1.22 "PMW1" program behind the extender stub.

VIVID.EXE is PMODE/W's own compressed executable format (PMWLITE), like Inside's INSIDE.EXE (this script is
inside/re/unpack.py with Vivid's bases): the MZ stub is the extender (itself self-compressed), and at e_lfanew
(0x2730) a "PMW1" header describes three objects whose data is compressed with the extender's LZ decompressor, and a
fixup table in compressed blocks. Object 1 holds the code and the demo's three file archives (files.py), object 2 a
small table naming the archives, object 3 the data, BSS and stack. See NOTES.md, "Unpacking".

This script:
  1. runs the stub's own self-decompression (real mode) in unicorn to get the extender's code (unpacked/pmodew.bin);
  2. runs the extender's decompressor (kernel offset 0x1e1c, 16-bit code with 32-bit addressing) in unicorn on every
     compressed stream (all three objects, every fixup block), with its file reads answered from VIVID.EXE;
  3. decompresses the same streams with an independent Python decompressor and checks that both agree byte for byte;
  4. applies the fixups like the extender (0x220b..0x22cd, 0x2634) for objects loaded at OBJ_BASE (two runs at
     different bases show that every relocated dword is covered) and writes unpacked/vivid_img.bin: object 1 at
     0x400000, object 2 at 0x810000, object 3 at 0x820000 (its BSS and stack zero), as one image from 0x400000.

usage: python3 unpack.py ../original/VIVID.EXE
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'unpacked')
OBJ_BASE = (0x400000, 0x810000, 0x820000)   # where the image is loaded: object 1 (code and data), 2 (a small one), 3 (data, BSS, stack)
SEL_CODE, SEL_DATA = 0x08, 0x10        # the flat selectors of the headless run (run_vivid.py)


def decompress(src, pos=0):
    """PMODE/W's LZ decompressor (kernel 0x1e1c). Returns (output, bytes of input used)."""
    out = bytearray()
    i = pos
    out.append(src[i]); i += 1               # the first byte is always a literal
    bits = 0
    nbits = 0

    def bit():
        nonlocal bits, nbits, i
        if nbits == 0:                       # dl counts down from 0x1f; refill a little-endian dword
            bits = struct.unpack_from('<I', src, i)[0]; i += 4
            nbits = 32
        nbits -= 1
        b = bits >> 31
        bits = (bits << 1) & 0xffffffff
        return b

    while True:
        if not bit():
            out.append(src[i]); i += 1       # literal
            continue
        lo = src[i]; i += 1                  # low 8 bits of the distance - 1
        if bit():
            hi = 0
            if bit():                        # 2 or 4 more high bits
                n = 2 * (1 + bit())
                for _ in range(n):
                    hi = (hi << 1) | bit()
            length = 0
            for k in range(4):               # unary: 3..6
                if bit():
                    length = 3 + k
                    break
            if not length:
                if bit():
                    length = src[i] + 15; i += 1        # 15..270
                else:
                    v = 0
                    for _ in range(3):
                        v = (v << 1) | bit()
                    length = v + 7                      # 7..14
            dist = (hi << 8 | lo) + 1
        elif bit():
            hi = 0
            for _ in range(3):
                hi = (hi << 1) | bit()
            dist = (hi << 8 | lo) + 1
            length = 2
        else:
            if lo == 0:
                return bytes(out), i - pos   # end marker
            dist = lo
            length = 2
        for _ in range(length):
            out.append(out[-dist])


def parse(exe):
    """Header, object table and the positions of every compressed stream."""
    lfanew = struct.unpack_from('<I', exe, 0x3c)[0]
    h = exe[lfanew:lfanew + 0x28]
    assert h[:4] == b'PMW1', 'not a PMW1 executable'
    (sig, ver, flags, ent_obj, ent_eip, stk_obj, stk_esp, obj_tab, nobj, fix_tab, data_ofs) = struct.unpack(
        '<4sHHIIIIIIII', h)
    objs = []
    for k in range(nobj):
        vsize, csize, oflags, fix_idx, fix_blocks, dsize = struct.unpack_from('<6I', exe, lfanew + obj_tab + k * 24)
        objs.append(dict(vsize=vsize, csize=csize, flags=oflags, fix_idx=fix_idx, fix_blocks=fix_blocks,
                         dsize=dsize))
    pos = lfanew + data_ofs
    for o in objs:
        o['data'] = pos
        pos += o['csize']
    assert pos == len(exe), 'object data does not end at the end of the file'
    for o in objs:
        o['blocks'] = []
        p = lfanew + fix_tab + o['fix_idx']
        for _ in range(o['fix_blocks']):
            csize, dsize = struct.unpack_from('<HH', exe, p)
            o['blocks'].append((p + 4, csize, dsize))
            p += 4 + csize
    return dict(lfanew=lfanew, version=ver, flags=flags, entry=(ent_obj, ent_eip), stack=(stk_obj, stk_esp),
                objs=objs)


def load(exe, info, bases):
    """Decompress the objects and apply the fixups (kernel 0x220b..0x22cd and 0x2634)."""
    mem = {}
    for k, o in enumerate(info['objs']):
        data, used = decompress(exe, o['data'])
        assert used == o['csize'], (k, used, o['csize'])
        assert len(data) == o['dsize'], (k, len(data), o['dsize'])
        mem[k] = bytearray(data + bytes(o['vsize'] - o['dsize']))
    counts = {}
    sites = []
    sels = []
    for k, o in enumerate(info['objs']):
        for p, csize, dsize in o['blocks']:
            rec, used = decompress(exe, p)
            assert used == csize and len(rec) == dsize, (k, used, csize, len(rec), dsize)
            i = 0
            while i < len(rec):
                typ = rec[i]
                src, = struct.unpack_from('<i', rec, i + 1)
                tobj = rec[i + 5]
                tofs, = struct.unpack_from('<I', rec, i + 6)
                i += 10
                assert typ & 0x20 == 0 and (typ & 0xf) <= 8
                counts[typ] = counts.get(typ, 0) + 1
                if src < 0:
                    continue                 # the part before the page start: applied by the previous page
                if typ & 0xf == 7:           # 32-bit offset
                    struct.pack_into('<I', mem[k], src, (bases[tobj - 1] + tofs) & 0xffffffff)
                    sites.append((k, src))
                elif typ & 0xf == 8:         # 32-bit self-relative
                    struct.pack_into('<I', mem[k], src, (bases[tobj - 1] + tofs - (bases[k] + src + 4)) & 0xffffffff)
                    sites.append((k, src))
                elif typ & 0xf == 2:         # 16-bit selector: the flat code selector for an executable object,
                    sel = SEL_CODE if info['objs'][tobj - 1]['flags'] & 4 else SEL_DATA     # else the data one
                    struct.pack_into('<H', mem[k], src, sel)
                    sels.append((k, src, sel))
                else:
                    raise ValueError('fixup type %#x not handled' % typ)
    return mem, counts, sites, sels


def image(mem, bases, info):
    end = bases[-1] + info['objs'][-1]['vsize']
    img = bytearray(end - bases[0])
    for k, m in mem.items():
        img[bases[k] - bases[0]:bases[k] - bases[0] + len(m)] = m
    return bytes(img)


# ---- the original's code in unicorn --------------------------------------------------------------------------------

def run_stub(exe):
    """Run the MZ stub's self-decompression in real mode; return the expanded extender (from its load segment)."""
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UcError
    from unicorn.x86_const import UC_X86_REG_CS, UC_X86_REG_SS, UC_X86_REG_SP, UC_X86_REG_DS, UC_X86_REG_ES
    hdr = struct.unpack_from('<14H', exe)
    cblp, cp, _, cparhdr, _, _, ss, sp, _, ip, cs = hdr[1:12]
    img = exe[cparhdr * 16:(cp - 1) * 512 + cblp]
    seg = 0x1000
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x110000)
    mu.mem_write(seg * 16, img)
    mu.reg_write(UC_X86_REG_CS, seg + cs)
    mu.reg_write(UC_X86_REG_SS, seg + ss)
    mu.reg_write(UC_X86_REG_SP, sp)
    mu.reg_write(UC_X86_REG_DS, seg - 0x10)
    mu.reg_write(UC_X86_REG_ES, seg - 0x10)

    def intr(mu, no, _):                     # the first DOS/BIOS call is in the expanded extender: stop there
        mu.emu_stop()
    mu.hook_add(UC_HOOK_INTR, intr)
    mu.emu_start((seg + cs) * 16 + ip, 0, count=20_000_000)
    return bytes(mu.mem_read(seg * 16, 0x8000))


def run_kernel_decompress(kernel, exe, pos, csize, out_size):
    """Call the extender's decompressor (kernel 0x1e1c) with its variables set up as the PMW1 loader does.

    The code runs in a 16-bit segment at kernel + 0x1b00 (the extender's real-mode code segment 0x11b0 when loaded
    at 0x10000) with 32-bit addressing through ES = 0. Its refill (0x1ec5) reads the file with int 21h AH=3F into
    the 4 KB transfer buffer ([0x12c]); the hook answers from VIVID.EXE."""
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR
    from unicorn.x86_const import (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_SP,
                                   UC_X86_REG_EDI, UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EFLAGS)
    K = 0x10000
    CSEG = (K + 0x1b00) >> 4
    BUF = 0x80000
    DST = 0x100000
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x1000000)
    mu.mem_write(K, kernel)
    mu.mem_write(K + 0x1b00 + 0x12c, struct.pack('<III', BUF, BUF + 0x1000 - 0x10, BUF + 0x1000))
    mu.mem_write(K + 0x1b00 + 0x138, struct.pack('<IH', csize, 5))
    for r in (UC_X86_REG_CS, UC_X86_REG_DS):
        mu.reg_write(r, CSEG)
    mu.reg_write(UC_X86_REG_ES, 0)
    mu.reg_write(UC_X86_REG_SS, 0x9000)
    mu.mem_write(0x9000 * 16 + 0xfffc, struct.pack('<H', 0xfff0))
    mu.reg_write(UC_X86_REG_SP, 0xfffc)
    mu.reg_write(UC_X86_REG_EDI, DST)
    fpos = [pos]

    def intr(mu, no, _):
        assert no == 0x21 and (mu.reg_read(UC_X86_REG_EAX) >> 8) & 0xff == 0x3f
        n = mu.reg_read(UC_X86_REG_ECX)
        at = BUF + (mu.reg_read(UC_X86_REG_EDX) & 0xffff)    # behind the bytes the refill kept
        chunk = exe[fpos[0]:fpos[0] + n]
        fpos[0] += len(chunk)
        mu.mem_write(at, chunk)
        mu.reg_write(UC_X86_REG_EAX, len(chunk))
        mu.reg_write(UC_X86_REG_EFLAGS, mu.reg_read(UC_X86_REG_EFLAGS) & ~1)
    mu.hook_add(UC_HOOK_INTR, intr)
    # the return goes to CSEG:0xfff0, which is hlt; make the return land on our hlt instead
    ret_lin = CSEG * 16 + 0xfff0
    mu.mem_write(ret_lin, b'\xf4')
    mu.emu_start(CSEG * 16 + (0x1e1c - 0x1b00), ret_lin, count=200_000_000)
    end = mu.reg_read(UC_X86_REG_EDI)
    return bytes(mu.mem_read(DST, end - DST)), fpos[0] - pos


def fnv1a64(b):
    h = 0xcbf29ce484222325
    for x in b:
        h = ((h ^ x) * 0x100000001b3) & 0xffffffffffffffff
    return h


def main():
    import hashlib
    exe = open(sys.argv[1], 'rb').read()
    os.makedirs(OUT, exist_ok=True)
    info = parse(exe)
    print('PMW1 at %#x, version %d.%02d, flags %#x, entry object %d + %#x, stack object %d + %#x' % (
        info['lfanew'], info['version'] & 0xff, info['version'] >> 8, info['flags'], *info['entry'], *info['stack']))
    for k, o in enumerate(info['objs']):
        print('object %d: virtual size %#x, data %#x bytes compressed to %#x at file %#x, flags %#x, '
              'fixups: %d blocks from table offset %#x' % (k + 1, o['vsize'], o['dsize'], o['csize'], o['data'],
                                                           o['flags'], o['fix_blocks'], o['fix_idx']))

    kernel = run_stub(exe)
    open(os.path.join(OUT, 'pmodew.bin'), 'wb').write(kernel)
    assert kernel[0x212c:0x2130] == b'PMW1', 'unexpected extender build'
    streams = [(o['data'], o['csize']) for o in info['objs']]
    streams += [(p, c) for o in info['objs'] for p, c, _ in o['blocks']]
    for p, c in streams:
        a, used_a = decompress(exe, p)
        b, used_b = run_kernel_decompress(kernel, exe, p, c, len(a))
        assert a == b, 'decompressor mismatch at %#x' % p
        assert used_a == c, (used_a, c)
    print('%d compressed streams: the Python decompressor matches the extender\'s code' % len(streams))

    mem, counts, sites, sels = load(exe, info, OBJ_BASE)
    alt = (0x1000000, 0x1500000, 0x1600000)
    mem2, _, _, _ = load(exe, info, alt)
    # every difference between the two loads lies in a relocated dword
    for k in mem:
        a, b = mem[k], mem2[k]
        diff = [i for i in range(len(a)) if a[i] != b[i]]
        cover = set()
        for kk, s in sites:
            if kk == k:
                cover.update(range(s, s + 4))
        assert set(diff) <= cover, 'difference outside a fixup'
    print('fixups: %s (type: count), %d offsets applied, %d selectors (%s)' % (
        counts, len(sites), len(sels), ', '.join('%#x=%#x' % (OBJ_BASE[k] + s, v) for k, s, v in sels)))
    img = image(mem, OBJ_BASE, info)
    open(os.path.join(OUT, 'vivid_img.bin'), 'wb').write(img)
    with open(os.path.join(OUT, 'vivid_unp.txt'), 'w') as f:
        f.write('vivid_img.bin: %d bytes from %#x; object 1 at %#x, object 2 at %#x, object 3 at %#x\n' % (
            len(img), OBJ_BASE[0], *OBJ_BASE))
        f.write('sha256 %s\nfnv1a64 %016x\n' % (hashlib.sha256(img).hexdigest(), fnv1a64(img)))
        f.write('entry %#x, esp %#x\n' % (OBJ_BASE[info['entry'][0] - 1] + info['entry'][1],
                                           OBJ_BASE[info['stack'][0] - 1] + info['stack'][1]))
        for k, s in sorted(sites):
            f.write('fixup %#x\n' % (OBJ_BASE[k] + s))
        for k, s, v in sels:
            f.write('selector %#x %#x\n' % (OBJ_BASE[k] + s, v))
    print('wrote %s (%d bytes), sha256 %s' % (os.path.join(OUT, 'vivid_img.bin'), len(img),
                                              hashlib.sha256(img).hexdigest()))


if __name__ == '__main__':
    main()

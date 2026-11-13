"""Unpack CLONE.EXE (Acme & Fudge, Takeover '98): run its own loader in unicorn and check it against Python.

CLONE.EXE is three things in one file (details in NOTES.md, "File layout"):

  file 0x0000..0x27a5   MZ stub = PMODE/W v1.23 (self-compressed real-mode extender, 10,149 bytes)
  file 0x27a8..0x346e   an LE executable (header 0x27a8, data pages at 0x2c00): one 0x86e-byte code object, the
                        "veloader" (resident name), plus a 0x5c4-byte BSS data object and a 0x200-byte stack
  file 0x346e..end      a "VE" container: a 0x42-byte header, then the compressed program image (0xc049 bytes)
                        and the compressed fixup list (0x4ff bytes)

PMODE/W loads and runs the LE (the veloader) as an ordinary flat program. The veloader opens its own executable
(name from the environment), finds the VE header behind the LE's last data page, allocates one DPMI block
(int 31h AX=0501) for image + BSS, reads the compressed image to the END of that block and decompresses it in
place to the start, zeroes the BSS, allocates a second block for the fixups, decompresses them, applies them,
frees the second block (0502) and calls the program's entry with ESP = top of its stack inside the BSS.

This script
  1. emulates PMODE/W's start of the LE in unicorn (flat selectors, PSP / environment selectors, int 21h open /
     seek / read / exit, int 31h 0501 / 0502) and lets the veloader run up to its `call eax` (0x10374): the
     memory block then holds the program as it will run. It does that twice, with the block at two linear
     addresses, which shows every relocated dword;
  2. decompresses and relocates with an independent Python implementation (ve_decompress, ve_fixups) and checks
     that it gives the same bytes at both addresses and the same relocation sites;
  3. writes the image relocated for BASE = 0x400000 (the linear address run_clone.py loads it at; addresses in
     the notes are these linear addresses).

Outputs (in --outdir, default ./unpacked):
  clone_img.bin    the program image relocated for linear base 0x400000: 0x2846c bytes (code and data; the BSS
                   that follows, 0xb6d4cb bytes, is zero and not written). Address A is at file offset A - 0x400000.
  clone_unp.txt    header values, sizes, entry, stack, hashes and the relocation list (image offsets)
  veloader.bin     the veloader's three objects as the LE loader leaves them (for reading the loader)

usage (inside the toolbox):  python3 unpack.py ../original/CLONE.EXE [--outdir unpacked]
"""
import argparse
import hashlib
import os
import struct

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_INTR, UC_HOOK_CODE, UcError
from unicorn.x86_const import *

BASE = 0x400000          # linear address the program block is loaded at (run_clone.py, rep.py, the notes)
ALT_BASE = 0x1230000     # second run, for checking the relocations
PSP = 0x8000             # fake PSP / environment (linear) for the loader emulation
ENV = 0x8200
GDT = 0x9000
SEL_CODE, SEL_DATA, SEL_PSP, SEL_ENV = 0x08, 0x10, 0x18, 0x20
CALL_EAX = 0x10374       # veloader: call eax (the program's entry); LE object 1 at 0x10000 as linked
RAM = 0x2000000


# ---------------------------------------------------------------------------------------------------------------
# File structure
# ---------------------------------------------------------------------------------------------------------------

LE_FIELDS = (('npages', 0x14), ('eip_obj', 0x18), ('eip', 0x1c), ('esp_obj', 0x20), ('esp', 0x24),
             ('page_size', 0x28), ('last_page', 0x2c), ('fixup_size', 0x30), ('loader_size', 0x38),
             ('obj_table', 0x40), ('nobj', 0x44), ('obj_page_table', 0x48), ('resname', 0x58),
             ('entry_table', 0x5c), ('fixup_page_table', 0x68), ('fixup_records', 0x6c), ('data_pages', 0x80),
             ('autods', 0x94))

# VE header (0x42 bytes, read by the veloader to 0x20518): name, offset in the header
VE_FIELDS = (('image_off', 0x04),      # compressed image, relative to the VE header
             ('fixup_off', 0x08),      # compressed fixups, relative to the VE header
             ('image_packed', 0x14), ('fixup_packed', 0x18),
             ('image_size', 0x24),     # unpacked program size (code + data)
             ('fixup_size', 0x28),     # unpacked fixup list size
             ('bss_size', 0x2c),       # zeroed after the image
             ('entry', 0x34), ('stack', 0x38))   # offsets in the image (ESP = (base + stack) & ~3)


def parse(d):
    """MZ stub, LE header and objects, VE header. All offsets are file offsets unless stated."""
    info = {}
    cblp, cp = struct.unpack_from('<HH', d, 2)
    info['mz_size'] = (cp - 1) * 512 + cblp if cblp else cp * 512
    le = struct.unpack_from('<I', d, 0x3c)[0]
    # the veloader computes the LE header position as the MZ size rounded up to 8 (it does not read 0x3c)
    assert le == (info['mz_size'] + 7) & ~7 and d[le:le + 2] == b'LE'
    info['le'] = le
    for name, off in LE_FIELDS:
        info['le_' + name] = struct.unpack_from('<I', d, le + off)[0]
    objs = []
    for i in range(info['le_nobj']):
        vsize, base, flags, pidx, pcnt, _ = struct.unpack_from('<6I', d, le + info['le_obj_table'] + 24 * i)
        objs.append(dict(vsize=vsize, base=base, flags=flags, pidx=pidx, pcnt=pcnt))
    info['objs'] = objs
    n = d[le + info['le_resname']]
    info['le_name'] = d[le + info['le_resname'] + 1:le + info['le_resname'] + 1 + n].decode('latin1')
    # VE header: right after the LE's last data page (veloader 0x100c1..0x100e4)
    ve = info['le_data_pages'] + info['le_npages'] * info['le_page_size']
    if info['le_last_page']:
        ve += info['le_last_page'] - info['le_page_size']
    assert d[ve:ve + 2] == b'VE'
    info['ve'] = ve
    for name, off in VE_FIELDS:
        info['ve_' + name] = struct.unpack_from('<I', d, ve + off)[0]
    info['ve_image_method'], info['ve_fixup_method'] = d[ve + 0x40], d[ve + 0x41]
    return info


def le_load(d, info, bases=None):
    """The LE objects with their (internal, 32-bit offset) fixups, as PMODE/W loads them. bases: object number ->
    linear address (default: the linked bases 0x10000, 0x20000, 0x30000)."""
    le, objs = info['le'], info['objs']
    bases = bases or {i + 1: o['base'] for i, o in enumerate(objs)}
    ps = info['le_page_size']
    mem = {}
    for i, o in enumerate(objs):
        buf = bytearray(max(o['vsize'], o['pcnt'] * ps))
        for k in range(o['pcnt']):
            pg = o['pidx'] + k                                  # 1-based page number = file order here
            src = info['le_data_pages'] + (pg - 1) * ps
            n = info['le_last_page'] if pg == info['le_npages'] else ps
            buf[k * ps:k * ps + n] = d[src:src + n]
        mem[i + 1] = buf
    fpt = le + info['le_fixup_page_table']
    frec = le + info['le_fixup_records']
    offs = [struct.unpack_from('<I', d, fpt + 4 * i)[0] for i in range(info['le_npages'] + 1)]
    for i, o in enumerate(objs):
        for k in range(o['pcnt']):
            g = o['pidx'] - 1 + k
            p, end = frec + offs[g], frec + offs[g + 1]
            while p < end:
                src, flg = d[p], d[p + 1]
                p += 2
                assert src & 0x2f == 7 and flg & 0x43 == 0, 'only internal 32-bit offset fixups are expected'
                so = struct.unpack_from('<h', d, p)[0]
                p += 2
                tobj = d[p]
                p += 1
                if flg & 0x10:
                    toff = struct.unpack_from('<I', d, p)[0]
                    p += 4
                else:
                    toff = struct.unpack_from('<H', d, p)[0]
                    p += 2
                at = k * ps + so
                if 0 <= at <= len(mem[i + 1]) - 4:
                    struct.pack_into('<I', mem[i + 1], at, bases[tobj] + toff)
    return mem, bases


# ---------------------------------------------------------------------------------------------------------------
# Emulation of the veloader under a minimal PMODE/W
# ---------------------------------------------------------------------------------------------------------------

def desc(base, limit, access, flags=0xc):
    return struct.pack('<HHBBBB', limit & 0xffff, base & 0xffff, (base >> 16) & 0xff, access,
                       (flags << 4) | ((limit >> 16) & 0xf), (base >> 24) & 0xff)


def emulate(d, info, block, verbose=False):
    """Run the veloader from its entry to `call eax`. block: linear address returned by the first 0501 (the
    program block). Returns the program block contents (image + BSS), the entry and ESP, and a log."""
    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(0, RAM)
    mem, bases = le_load(d, info)
    for i, buf in mem.items():
        mu.mem_write(bases[i], bytes(buf))
    gdt = (b'\0' * 8 + desc(0, 0xfffff, 0x9a) + desc(0, 0xfffff, 0x92) + desc(PSP, 0xff, 0x92, 4) +
           desc(ENV, 0xffff, 0x92, 4))
    mu.mem_write(GDT, gdt)
    mu.reg_write(UC_X86_REG_GDTR, (0, GDT, len(gdt) - 1, 0))
    psp = bytearray(256)
    struct.pack_into('<H', psp, 0x2c, SEL_ENV)               # PMODE/W puts the environment selector at PSP:2C
    mu.mem_write(PSP, bytes(psp))
    mu.mem_write(ENV, b'PATH=C:\\\x00ULTRASND=240,3,3,7,7\x00\x00\x01\x00C:\\CLONE.EXE\x00')
    mu.reg_write(UC_X86_REG_CS, SEL_CODE)
    for r in (UC_X86_REG_DS, UC_X86_REG_SS, UC_X86_REG_FS, UC_X86_REG_GS):
        mu.reg_write(r, SEL_DATA)
    mu.reg_write(UC_X86_REG_ES, SEL_PSP)
    o = info['objs'][info['le_esp_obj'] - 1]
    mu.reg_write(UC_X86_REG_ESP, bases[info['le_esp_obj']] + info['le_esp'])
    R, W = mu.reg_read, mu.reg_write
    st = {'pos': 0, 'opened': None, 'allocs': [], 'freed': [], 'reads': [], 'err': None, 'next': block}

    def setcf(c):
        f = R(UC_X86_REG_EFLAGS)
        W(UC_X86_REG_EFLAGS, (f | 1) if c else (f & ~1))

    def on_intr(mu, intno, _):
        eax = R(UC_X86_REG_EAX)
        ax, ah = eax & 0xffff, (eax >> 8) & 0xff
        if intno == 0x21 and ax == 0x3d00:
            name = bytes(mu.mem_read(R(UC_X86_REG_EDX), 80)).split(b'\0')[0]
            st['opened'] = name.decode('latin1')
            W(UC_X86_REG_EAX, 5)
            setcf(0)
        elif intno == 0x21 and ax == 0x4200:
            st['pos'] = (R(UC_X86_REG_ECX) & 0xffff) << 16 | R(UC_X86_REG_EDX) & 0xffff
            W(UC_X86_REG_EAX, st['pos'] & 0xffff)
            W(UC_X86_REG_EDX, st['pos'] >> 16)
            setcf(0)
        elif intno == 0x21 and ah == 0x3f:
            n, dst = R(UC_X86_REG_ECX), R(UC_X86_REG_EDX)
            data = d[st['pos']:st['pos'] + n]
            mu.mem_write(dst, data)
            st['reads'].append((st['pos'], len(data), dst))
            st['pos'] += len(data)
            W(UC_X86_REG_EAX, len(data))
            setcf(0)
        elif intno == 0x31 and ax == 0x0501:
            size = (R(UC_X86_REG_EBX) & 0xffff) << 16 | R(UC_X86_REG_ECX) & 0xffff
            lin = st['next']
            st['next'] = (lin + size + 0xffff) & ~0xffff
            st['allocs'].append((lin, size))
            W(UC_X86_REG_EBX, lin >> 16)
            W(UC_X86_REG_ECX, lin & 0xffff)
            h = len(st['allocs'])
            W(UC_X86_REG_ESI, 0)
            W(UC_X86_REG_EDI, h)
            setcf(0)
        elif intno == 0x31 and ax == 0x0502:
            st['freed'].append((R(UC_X86_REG_ESI) & 0xffff) << 16 | R(UC_X86_REG_EDI) & 0xffff)
            setcf(0)
        else:
            st['err'] = 'int %02x ax=%04x at %x' % (intno, ax, R(UC_X86_REG_EIP))
            mu.emu_stop()

    def on_call(mu, addr, size, _):
        mu.emu_stop()

    mu.hook_add(UC_HOOK_INTR, on_intr)
    mu.hook_add(UC_HOOK_CODE, on_call, None, CALL_EAX, CALL_EAX)
    entry = bases[info['le_eip_obj']] + info['le_eip']
    try:
        mu.emu_start(entry, 0xffffffff, count=200_000_000)
    except UcError as e:
        st['err'] = 'emulation error %s at %x' % (e, R(UC_X86_REG_EIP))
    if st['err'] or R(UC_X86_REG_EIP) != CALL_EAX:
        raise RuntimeError('veloader emulation failed: %s (eip %x)' % (st['err'], R(UC_X86_REG_EIP)))
    lin, size = st['allocs'][0]
    st['block'] = bytes(mu.mem_read(lin, size))
    st['entry'] = R(UC_X86_REG_EAX)
    st['esp'] = R(UC_X86_REG_ESP)
    st['veloader'] = b''.join(bytes(mem[i]) for i in sorted(mem))
    if verbose:
        print('emulation at %#x: opened %r, reads %s, allocs %s, freed %s, entry %#x, esp %#x' % (
            block, st['opened'], ['%#x+%#x' % (p, n) for p, n, _ in st['reads']],
            ['%#x+%#x' % a for a in st['allocs']], st['freed'], st['entry'], st['esp']))
    return st


# ---------------------------------------------------------------------------------------------------------------
# Independent Python implementation
# ---------------------------------------------------------------------------------------------------------------

def ve_tables(d, info):
    """The decompressor's base / extra-bits tables, read from the veloader (object 1 offsets 0x788, 0x7e8, 0x840,
    0x858). They follow the usual rule (Deflate's distance table cut at 24 codes, a 22-code length table)."""
    o1 = info['le_data_pages']
    dist_base = struct.unpack_from('<24I', d, o1 + 0x788)
    len_base = struct.unpack_from('<22I', d, o1 + 0x7e8)
    dist_extra = tuple(d[o1 + 0x840:o1 + 0x858])
    len_extra = tuple(d[o1 + 0x858:o1 + 0x86e])
    return dist_base, dist_extra, len_base, len_extra


class Huff:
    """Canonical Huffman code as the veloader builds it (0x10414..0x10472): for each length L = 1..15, the
    symbols with that length in ascending order get consecutive codes; codes are kept left-aligned in 32 bits.
    first[L] = first code of length L, last[L] = its last code (or 0 if there are none), syms[L] = the symbols.
    The code must be complete (the left-aligned code space sums to exactly 2^32), otherwise the loader exits."""
    def __init__(self, lengths):
        self.first, self.last, self.syms = [0] * 17, [0] * 17, [[] for _ in range(17)]
        code, step = 0, 1 << 31
        for L in range(1, 16):
            self.first[L] = code
            for s, n in enumerate(lengths):
                if n == L:
                    self.syms[L].append(s)
                    code += step
            self.last[L] = code - 1 if self.syms[L] else 0
            step >>= 1
        if code != 1 << 32:
            raise ValueError('incomplete Huffman code')
        self.minlen = next(L for L in range(1, 16) if self.last[L])

    def decode(self, bits):
        """bits: the next 32 bits, MSB first. Returns (symbol, length). As the loader: the first length L >=
        minlen whose last code >= bits (lengths without codes have last = 0, so they are passed over)."""
        L = self.minlen
        while bits > self.last[L]:
            L += 1
        return self.syms[L][(bits - self.first[L]) >> (32 - L)], L


def ve_decompress(src, size, tables):
    """The veloader's decompressor (0x103d1): LZ77 with a 4 KB window and two canonical Huffman codes.

    Bit stream: big-endian 16-bit words, bits taken MSB first.
      8 bytes   16 nibbles (low nibble first): code lengths of the 16-symbol "length code" (symbols = 0..15)
      11 bytes  22 nibbles (low nibble first): code lengths of the 22-symbol LENGTH code
      then the 280 code lengths of the literal/distance code, each coded with the length code (no runs)
      then until `size` bytes are out: a literal/distance symbol s;
        s < 256:  literal byte s
        s >= 256: distance = dist_base[s-256] + extra bits (dist_extra[s-256]), then a symbol t of the length
                  code, length = len_base[t] + extra bits (len_extra[t]); copy `length` bytes from `distance`
                  back (byte by byte, so they may overlap)
    Extra bits are read MSB first."""
    dist_base, dist_extra, len_base, len_extra = tables
    p = 0
    nibbles = []
    for b in src[:19]:
        nibbles += [b & 15, b >> 4]
    p = 19
    pos = 0                       # bit position in the word stream that starts at p
    data = src[p:]

    def peek():
        w = pos >> 3
        chunk = data[w:w + 5] + b'\0' * 5
        v = int.from_bytes(chunk[:5], 'big')
        return (v >> (8 - (pos & 7))) & 0xffffffff

    def take(n):
        nonlocal pos
        v = peek() >> (32 - n) if n else 0
        pos += n
        return v

    lcode = Huff(nibbles[:16])
    dlen = nibbles[16:38]
    lens = []
    for _ in range(280):
        s, L = lcode.decode(peek())
        pos += L
        lens.append(s)
    lit = Huff(lens)
    ln = Huff(dlen)
    out = bytearray()
    while len(out) < size:
        s, L = lit.decode(peek())
        pos += L
        if s < 256:
            out.append(s)
            continue
        s -= 256
        dist = dist_base[s] + take(dist_extra[s])
        t, L = ln.decode(peek())
        pos += L
        n = len_base[t] + take(len_extra[t])
        for _ in range(n):
            out.append(out[-dist])
    # the loader reads whole 16-bit words: report the bytes consumed in its terms
    used = 19 + ((pos + 15) // 16) * 2
    return bytes(out[:size]), bytes(out[size:]), used


def ve_fixup_records(fix):
    """The fixup list (applied at 0x10289): records of
         byte type (0 = end; bit 0: add the base to the dword at the site, then site += 4;
                    bit 1: a 16-bit selector at the site, site += 2: CS if the word there is 0, else DS)
         word count (0 = 65536), dword first site (image offset);
       then count - 1 site deltas, each a big-endian base-128 number (bit 7 = more bytes follow), added to the
       site after the previous entry was applied (so the delta counts from the end of the previous entry).
    Returns a list of (type, site) and the bytes used."""
    p, out = 0, []
    while fix[p]:
        typ = fix[p]
        assert typ < 4
        cnt = struct.unpack_from('<H', fix, p + 1)[0] or 0x10000
        site = struct.unpack_from('<I', fix, p + 3)[0]
        p += 7
        while True:
            out.append((typ, site))
            if typ & 1:
                site += 4
            if typ & 2:
                site += 2
            cnt -= 1
            if cnt == 0:
                break
            v = 0
            while True:
                b = fix[p]
                p += 1
                v = (v << 7) + (b & 0x7f)
                if not b & 0x80:
                    break
            site += v
    return out, p + 1


def ve_relocate(img, recs, base, cs=SEL_CODE, ds=SEL_DATA):
    out = bytearray(img)
    for typ, site in recs:
        at = site
        if typ & 1:
            struct.pack_into('<I', out, at, (struct.unpack_from('<I', out, at)[0] + base) & 0xffffffff)
            at += 4
        if typ & 2:
            struct.pack_into('<H', out, at, ds if out[at] else cs)
    return out


def py_unpack(d, info, base):
    tables = ve_tables(d, info)
    ve = info['ve']
    a = ve + info['ve_image_off']
    img, extra, used_i = ve_decompress(d[a:a + info['ve_image_packed']], info['ve_image_size'], tables)
    a = ve + info['ve_fixup_off']
    fix, _, used_f = ve_decompress(d[a:a + info['ve_fixup_packed']], info['ve_fixup_size'], tables)
    recs, nfix = ve_fixup_records(fix)
    return dict(raw=img, fix=fix, recs=recs, nfix=nfix, img=ve_relocate(img, recs, base),
                used=(used_i, used_f))


# ---------------------------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('--outdir', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'unpacked'))
    a = ap.parse_args()
    d = open(a.exe, 'rb').read()
    info = parse(d)
    print('MZ stub %d bytes, LE at %#x (%r, %d pages, data pages at %#x), VE header at %#x' % (
        info['mz_size'], info['le'], info['le_name'], info['le_npages'], info['le_data_pages'], info['ve']))
    for i, o in enumerate(info['objs']):
        print('  LE object %d: base %#x vsize %#x flags %#x pages %d' % (i + 1, o['base'], o['vsize'], o['flags'],
                                                                       o['pcnt']))
    print('  VE: ' + ', '.join('%s %#x' % (k[3:], v) for k, v in info.items() if k.startswith('ve_')))

    em = {b: emulate(d, info, b, verbose=True) for b in (BASE, ALT_BASE)}
    size = info['ve_image_size']
    bss = info['ve_bss_size']
    for b, st in em.items():
        blk = st['block']
        assert len(blk) == size + bss + (0x12 if info['ve_image_method'] and bss < 0x12 else 0)
        assert not any(blk[size:size + bss]), 'BSS not zero'
        assert st['entry'] == b + info['ve_entry'] and st['esp'] == (b + info['ve_stack']) & ~3
    i0, i1 = em[BASE]['block'][:size], em[ALT_BASE]['block'][:size]
    diff = [k for k in range(size) if i0[k] != i1[k]]
    # sites where the two emulations differ: every relocated dword must differ by ALT_BASE - BASE
    py = {b: py_unpack(d, info, b) for b in (BASE, ALT_BASE)}
    recs = py[BASE]['recs']
    types = sorted({t for t, _ in recs})
    offs = [s for t, s in recs if t & 1]
    sels = [s + (4 if t & 1 else 0) for t, s in recs if t & 2]
    for s in offs:
        v0, v1 = struct.unpack_from('<I', i0, s)[0], struct.unpack_from('<I', i1, s)[0]
        assert (v1 - v0) & 0xffffffff == ALT_BASE - BASE, hex(s)
    covered = set()
    for s in offs:
        covered.update(range(s, s + 4))
    assert set(diff) <= covered, 'bytes differ outside relocation sites'
    for b in (BASE, ALT_BASE):
        assert py[b]['img'] == em[b]['block'][:size], 'Python image differs from the emulation at base %#x' % b
    raw = py[BASE]['raw']
    print('emulation and Python agree at both bases: %d fixups (types %s: %d offsets, %d selectors), '
          'fixup list %d of %d bytes used' % (len(recs), types, len(offs), len(sels), py[BASE]['nfix'],
                                              info['ve_fixup_size']))
    # unrelocated image = relocated for base 0
    img0 = ve_relocate(raw, recs, 0)
    os.makedirs(a.outdir, exist_ok=True)
    img = py[BASE]['img']
    open(os.path.join(a.outdir, 'clone_img.bin'), 'wb').write(img)
    open(os.path.join(a.outdir, 'veloader.bin'), 'wb').write(em[BASE]['veloader'])
    h = lambda x: hashlib.sha256(x).hexdigest()
    with open(os.path.join(a.outdir, 'clone_unp.txt'), 'w') as f:
        f.write('CLONE.EXE: %d bytes, sha256 %s\n' % (len(d), h(d)))
        f.write('MZ stub (PMODE/W v1.23): %d bytes; LE header at %#x, resident name %r\n' % (
            info['mz_size'], info['le'], info['le_name']))
        for k, v in info.items():
            if k.startswith('le_') and isinstance(v, int):
                f.write('  %-22s %#x\n' % (k, v))
        for i, o in enumerate(info['objs']):
            f.write('  object %d: base %#x vsize %#x flags %#x page index %d pages %d\n' % (
                i + 1, o['base'], o['vsize'], o['flags'], o['pidx'], o['pcnt']))
        f.write('VE header at file %#x\n' % info['ve'])
        for k, v in info.items():
            if k.startswith('ve_') and k != 've':
                f.write('  %-22s %#x\n' % (k, v))
        f.write('compressed image: file %#x..%#x; compressed fixups: file %#x..%#x\n' % (
            info['ve'] + info['ve_image_off'], info['ve'] + info['ve_image_off'] + info['ve_image_packed'],
            info['ve'] + info['ve_fixup_off'], info['ve'] + info['ve_fixup_off'] + info['ve_fixup_packed']))
        f.write('words read by the decompressor: image %d of %d bytes, fixups %d of %d bytes\n' % (
            py[BASE]['used'][0], info['ve_image_packed'], py[BASE]['used'][1], info['ve_fixup_packed']))
        f.write('program block: image %#x + BSS %#x = %#x bytes; loaded at base B: entry B+%#x, ESP (B+%#x)&~3\n' % (
            size, bss, size + bss, info['ve_entry'], info['ve_stack']))
        f.write('clone_img.bin: relocated for B = %#x, %d bytes, sha256 %s\n' % (BASE, len(img), h(img)))
        f.write('image relocated for base 0: sha256 %s, fnv1a64 %016x\n' % (h(img0), fnv1a(img0)))
        f.write('image relocated for B = %#x: fnv1a64 %016x\n' % (BASE, fnv1a(img)))
        f.write('decompressed fixup list: sha256 %s\n' % h(py[BASE]['fix']))
        f.write('fixups: %d (types %s), %d offset sites, %d selector sites\n' % (len(recs), types, len(offs),
                                                                               len(sels)))
        f.write('selector sites (image offsets): %s\n' % ' '.join('%x' % s for s in sels))
        f.write('offset sites (image offsets):\n')
        for k in range(0, len(offs), 16):
            f.write(' '.join('%x' % s for s in offs[k:k + 16]) + '\n')
    print('wrote %s/clone_img.bin (%d bytes, base %#x), clone_unp.txt, veloader.bin' % (a.outdir, len(img), BASE))


def fnv1a(b):
    h = 0xcbf29ce484222325
    for x in b:
        h = ((h ^ x) * 0x100000001b3) & 0xffffffffffffffff
    return h


if __name__ == '__main__':
    main()

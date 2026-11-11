"""Run JIZZ's 32-bit program headless in unicorn and record what it does.

No DOS and no DOS32 kernel: the program image (jizz_img.bin from unadam.py, relocated for base 0) is loaded at
linear 0 with flat selectors 0x18 (data, the one the selector fixup at 0x186bf loads) and 0x20 (code), so a near
pointer is a linear address. DOS32's services are emulated where the intro calls them: int 31h EE02 (EBX =
selector base 0, ESI = PSP, EDI = environment with ULTRASND=240,3,3,7,7), EE42 (allocate), 0204/0205 (IRQ0
vector), 0800 (map the LFB), 0300 (real-mode int 10h: VESA 4F00/4F01/4F02/4F07/4F0A), int 21h (print, DOS memory,
files for the dump modes, exit) and int 10h. Faked hardware: PIT channel 0 (count, latch reads), PIC, VGA retrace,
keyboard (no key), a Gravis UltraSound at 0x240 (Gus, from paper/re/run_paper.py, with word I/O).

The VESA driver offers 320x200 hicolor with a linear frame buffer at 0xe0000000 (2 MB, 15 pages of 128000
bytes): mode 0x10e (16 bit, 5:6:5, what the intro picks first) and 0x10d (15 bit, 5:5:5). --bpp 15 offers only
the 15-bit mode. The intro renders into 4 pages and flips with 4F07 (BL=0x80: during vertical retrace).

Time: the PIT clock (1193182 Hz) is the time base. Main-line code runs at a fixed speed: --budget instructions
per timer tick of 125 BPM (PIT count 23868 = 0x2d86bf / 125, ~50 Hz), i.e. budget * 50 instructions per second.
The timer interrupt comes when the programmed PIT period has elapsed (MXMPlay programs its tick period; before
the music starts the BIOS rate 0x10000 is used and no handler runs). Main-line code runs in quanta of
--quantum instructions; a PIT latch read returns the counter at the start of the current quantum.
4F07 with BL=0x80 ends the quantum (charged as half a quantum) and waits for the next vertical retrace
(--refresh, 70 Hz) unless --no-vsync. Time spent in the IRQ0 handler is not charged to the main line.

Outputs (prefix given with --out), one record per timer tick:
  <out>.frames  32-byte header (b'JIZZFRM1', then <IIIIII: width 320, height 200, bits 16|15, record bytes 128000,
                header bytes 32, 0), then per tick the visible 320x200 page (16-bit pixels, little endian,
                5:6:5 or 5:5:5 as in the header), zero while the screen is in text mode
  <out>.gus     text log of GUS register / port writes:  tick v<voice> r<reg> value  /  tick p<port> value
  <out>.dram    final GUS DRAM contents (1 MB)
  <out>.ticks   per tick: tick, PIT count of the tick, time (s) at its end, music order, row, sync event [0x6413],
                sync index, part (the routine main is in, see MAIN_CALLS), frames flipped so far, main-line EIP
  <out>.flips   per page flip (4F07): flip number, tick, time (s), the first xmpGetTimer value (PIT clocks, signed)
                read while that frame was drawn (-2^40: none), part

usage: python3 run_jizz.py --out full [--budget 2000000] [--ticks N]
            [--snap-at 430,440,1365,... --snap snaps/t{tick}.snap]      save snapshots
       python3 run_jizz.py --load --snap snaps/t1365.snap --out x --ticks 2000 [--part-budget 17003=1000000]
       python3 run_jizz.py --out d --ticks 440 --cmdline " /pandora"      dump mode (" /mp4": the music)
The full run takes about 10 minutes and ends by itself at tick 9357 (program exit).
"""
import argparse
import os
import pickle
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_INTR, UC_HOOK_CODE, UC_HOOK_INSN, UcError
from unicorn.x86_const import *

HERE = '/home/dennis/src/dvdstelt/demoscene/jizz/re'
sys.path.insert(0, HERE)
IMG = open(os.path.join(HERE, 'jizz_img.bin'), 'rb').read()
EIP0, ESP0 = 0x186bd, 0x32614

PIT_HZ = 1193182
REF_COUNT = 0x2d86bf // 125     # MXMPlay's tick period at 125 BPM
RAM = 0x2000000                 # 32 MB
PSP = 0x80000                   # fake PSP, environment, DOS memory (below VGA)
ENV = 0x80100
DOSMEM = 0x81000
IRET = 0x7f000                  # old IRQ0 handler: iretd
GDT = 0x7f100
SENTINEL = 0x7fff0              # return address for IRQ delivery
HEAP = 0x100000
LFB = 0xe0000000
LFB_SIZE = 0x200000
VBEBUF = 0x70000                # fake VBE mode list (0x1400 bytes, the intro copies that much) and OEM string
PAGE = 320 * 200 * 2

# main (0x186bd) calls these one after the other: call site -> routine. The harness tracks the current one as
# "part" (0x18c25 = the final loop that shows the end picture until the sync table ends).
MAIN_CALLS = {0x186f7: 0x56c3, 0x186fc: 0x5a50, 0x188f3: 0x6592, 0x188f8: 0x6b16, 0x188fd: 0x6cdd, 0x18902: 0x10fa2,
              0x18907: 0x11dfe, 0x1890c: 0x16f4f, 0x18911: 0x180e5, 0x18916: 0x18228, 0x189cb: 0x1f5b8,
              0x18aa5: 0x1114e, 0x18ac2: 0x1632b, 0x18b60: 0x29738, 0x18bd3: 0x1114e, 0x18bd8: 0x12c50,
              0x18bdf: 0x6305, 0x18be9: 0x29db1, 0x18bee: 0x12013, 0x18bf3: 0x17003, 0x18bf8: 0x169e7,
              0x18bfd: 0x15c8b, 0x18c02: 0x1212a, 0x18c07: 0x15d20, 0x18c0c: 0x177bf, 0x18c11: 0x17c8a,
              0x18c16: 0x10fda, 0x18c25: 0x18c25, 0x18c59: 0x18c59}


class Gus:
    """Gravis UltraSound register file (from paper/re/run_paper.py), with the base port as a parameter and
    word I/O to 0x3x2 (voice, register) and 0x3x3 (register, data low)."""
    def __init__(self, log, base=0x240):
        self.dram = bytearray(1 << 20)
        self.base = base
        self.voice = 0
        self.reg = 0
        self.addr = 0
        self.regs = {}
        self.log = log
        self.tick = 0
        self.lo = 0
        self.reads = {}

    def out(self, port, size, val):
        p = port - self.base
        if size == 2 and p == 0x102:        # voice select + register select
            self.voice = val & 31
            self.reg = (val >> 8) & 0xff
        elif p == 0x102:
            self.voice = val & 31
        elif p == 0x103:
            self.reg = val & 0xff
            if size == 2:                   # register select + data low
                self.lo = (val >> 8) & 0xff
        elif p == 0x104:
            if size == 2:                   # 16-bit register
                self._write(self.reg, val & 0xffff, 2)
            else:
                self.lo = val & 0xff
        elif p == 0x105:                    # data high byte (8-bit registers, or high half after a low write)
            self._write(self.reg, val & 0xff, 1)
        elif p == 0x107:                    # DRAM data
            self.dram[self.addr & 0xfffff] = val & 0xff
        else:
            self.log.write('%d p%03x %x\n' % (self.tick, port, val))

    def _write(self, reg, val, size):
        if reg == 0x43:
            self.addr = (self.addr & 0xff0000) | (val & 0xffff)
        elif reg == 0x44:
            self.addr = (self.addr & 0xffff) | ((val & 0xff) << 16)
        else:
            self.regs[(self.voice, reg)] = val
            self.log.write('%d v%d r%02x %x\n' % (self.tick, self.voice, reg, val))

    def inp(self, port, size):
        p = port - self.base
        if p == 0x107:
            return self.dram[self.addr & 0xfffff]
        if p in (0x105, 0x104):
            r = self.reg
            self.reads[r] = self.reads.get(r, 0) + 1
            if r >= 0x80:
                v = self.regs.get((self.voice, r - 0x80), 0)
                if r == 0x80: v |= 1        # voice stopped
                if r == 0x8d: v |= 1        # volume ramp stopped
                if p == 0x105 and size == 1 and r in (0x81, 0x82, 0x83, 0x84, 0x85, 0x89, 0x8a, 0x8b):
                    return (v >> 8) & 0xff
                return v & (0xff if size == 1 else 0xffff)
            return 0
        if p == 0x006:                      # IRQ status
            return 0
        if p == 0x103:
            return self.reg
        if p == 0x102:
            return self.voice
        return 0


def desc(access, flags=0xcf):
    return struct.pack('<HHBBBB', 0xffff, 0, 0, access, flags, 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--ticks', type=int, default=1 << 30, help='stop after this many ticks')
    ap.add_argument('--budget', type=int, default=2000000,
                    help='main-line instructions per 125-BPM timer tick (x50 = instructions per second)')
    ap.add_argument('--part-budget', default='', help='per-part override: ADDR=BUDGET,... (ADDR = part routine '
                    'called from main, see PARTS); applies while that part runs')
    ap.add_argument('--quantum', type=int, default=0, help='instructions per emulation slice (default budget/64)')
    ap.add_argument('--refresh', type=float, default=70.0, help='vertical refresh rate for 4F07 BL=0x80 waits')
    ap.add_argument('--no-vsync', action='store_true', help='4F07 does not wait for the retrace')
    ap.add_argument('--bpp', type=int, default=16, choices=(15, 16), help='offer 320x200x16 (and 15) or only 15')
    ap.add_argument('--cmdline', default='', help='command tail, e.g. " /pandora" or " /mp4" (dump modes)')
    ap.add_argument('--dumpdir', default=os.path.join(HERE, 'dump'), help='where the dump modes write files')
    ap.add_argument('--start-tick', type=int, default=0, help='only record frames from this tick on')
    ap.add_argument('--snap-at', default='', help='save snapshots at these ticks (comma-separated); --snap may '
                    'contain {tick}')
    ap.add_argument('--snap', help='snapshot file to write (with --snap-at) or to resume from (with --load)')
    ap.add_argument('--load', action='store_true', help='resume from --snap')
    ap.add_argument('--freeze-timer', action='store_true', help='analysis: xmpGetTimer returns the value of its first '
                    'call in this run every time, so whatever still moves is stepped per frame (or by sync events)')
    ap.add_argument('--trace', default='', help='comma-separated addresses: print the tick the first time each runs')
    ap.add_argument('--plugin', default='', help='python file exec()d after loading, with mu, st, a, reg '
                    'in scope; it may define atexit_plugin() and on_tick(t)')
    a = ap.parse_args()

    part_budget = {}
    for item in filter(None, a.part_budget.split(',')):
        k, v = item.split('=')
        part_budget[int(k, 16)] = int(v)

    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(0, RAM)
    mu.mem_map(LFB, LFB_SIZE)
    mu.mem_write(0, IMG)
    psp = bytearray(256)
    struct.pack_into('<H', psp, 0x2c, ENV >> 4)
    tail = a.cmdline.encode('latin1')[:126]
    psp[0x80] = len(tail)
    psp[0x81:0x81 + len(tail)] = tail
    psp[0x81 + len(tail)] = 0x0d
    mu.mem_write(PSP, bytes(psp))
    mu.mem_write(ENV, b'ULTRASND=240,3,3,7,7\x00\x00')
    mu.mem_write(IRET, b'\xcf')
    mu.mem_write(GDT, b'\x00' * 8 + desc(0x9a) + desc(0x92) + desc(0x92) + desc(0x9a))
    mu.reg_write(UC_X86_REG_GDTR, (0, GDT, 5 * 8 - 1, 0))
    mu.reg_write(UC_X86_REG_CS, 0x20)
    for r in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_FS, UC_X86_REG_GS):
        mu.reg_write(r, 0x18)
    mu.reg_write(UC_X86_REG_ESP, ESP0)

    glog = open(a.out + '.gus', 'w')
    flog = open(a.out + '.flips', 'w')
    flog.write('# flip tick time_s timer part  (timer = first xmpGetTimer value read while drawing this frame)\n')
    gus = Gus(glog)
    pal = bytearray(768)
    # st: everything that goes into a snapshot
    st = {'tick': 0, 'now': 0.0, 'next_irq': float(0x10000), 'pit_count': 0x10000, 'pit_start': 0.0,
          'pit_w': 0, 'pit_lo': 0, 'latch': None, 'latch_n': 0, 'vec8': None, 'heap': HEAP, 'dos': DOSMEM,
          'gfx': False, 'mode': 3, 'disp': 0, 'flips': 0, 'vsync': False, 'vs_until': None, 'retrace': 0,
          'part': 0, 'ftimer': None, 'stop': False, 'files': {}, 'nfile': 5, 'text': '', 'dac_w': 0, 'in_irq': False}
    outs = {}
    ins = {}

    def reg(r): return mu.reg_read(r)
    def setcf(c):
        f = mu.reg_read(UC_X86_REG_EFLAGS)
        mu.reg_write(UC_X86_REG_EFLAGS, (f | 1) if c else (f & ~1))

    def pit_counter():
        c = st['pit_count']
        el = int(st['now'] - st['pit_start']) % c
        return (c - el) & 0xffff

    def cstr(addr, end=b'$', n=400):
        return bytes(mu.mem_read(addr, n)).split(end)[0].decode('latin1')

    def text(s):
        st['text'] += s
        while '\n' in st['text']:
            line, st['text'] = st['text'].split('\n', 1)
            print('text: %s' % line.rstrip('\r')[-200:], flush=True)

    # ---- VESA (real-mode int 10h AX=4Fxx) -------------------------------------------------------------------
    modes = [0x10e, 0x10d] if a.bpp == 16 else [0x10d]

    def vesa(r):
        ax = r['eax'] & 0xffff
        lin = (r['es'] << 4) + (r['edi'] & 0xffff)
        if ax == 0x4f00:
            blk = bytearray(512)
            blk[0:4] = b'VESA'
            struct.pack_into('<HIIIH', blk, 4, 0x0200, (VBEBUF >> 4) << 16 | 0x100, 0,
                             (VBEBUF >> 4) << 16 | 0x0, LFB_SIZE >> 16)
            mu.mem_write(lin, bytes(blk))
            ml = b''.join(struct.pack('<H', m) for m in modes) + b'\xff\xff'
            mu.mem_write(VBEBUF, ml + bytes(0x1400 - len(ml)))
            mu.mem_write(VBEBUF + 0x100, b'run_jizz VBE\x00')
            r['eax'] = 0x4f
        elif ax == 0x4f01:
            m = r['ecx'] & 0x1ff
            if m not in modes:
                r['eax'] = 0x014f
                return
            b = bytearray(256)
            bits = 16 if m == 0x10e else 15
            struct.pack_into('<HBBHHHHIH', b, 0, 0x9b, 7, 0, 64, 64, 0xa000, 0, 0, 640)
            struct.pack_into('<HHBBBBBBBBB', b, 0x12, 320, 200, 8, 8, 1, 16, 1, 6, 0, LFB_SIZE // PAGE - 1, 1)
            if bits == 16:
                b[0x1f:0x27] = bytes((5, 11, 6, 5, 5, 0, 0, 0))
            else:
                b[0x1f:0x27] = bytes((5, 10, 5, 5, 5, 0, 1, 15))
            struct.pack_into('<I', b, 0x28, LFB)
            mu.mem_write(lin, bytes(b))
            r['eax'] = 0x4f
        elif ax == 0x4f02:
            st['mode'] = r['ebx'] & 0xffff
            st['gfx'] = True
            st['bits'] = 16 if (st['mode'] & 0x1ff) == 0x10e else 15
            print('VBE set mode %#x' % st['mode'], flush=True)
            r['eax'] = 0x4f
        elif ax == 0x4f07:
            bl = r['ebx'] & 0xff
            if bl in (0, 0x80):
                st['disp'] = (r['edx'] & 0xffff) * 640 + (r['ecx'] & 0xffff) * 2
                st['flips'] += 1
                flog.write('%d %d %.5f %d %x\n' % (st['flips'], st['tick'], st['now'] / PIT_HZ,
                                                   -(1 << 40) if st['ftimer'] is None else st['ftimer'], st['part']))
                st['ftimer'] = None
                if bl == 0x80 and not a.no_vsync:
                    st['vsync'] = True          # end the quantum here, then wait for the retrace
                    mu.emu_stop()
            else:
                r['ecx'] = (st['disp'] % 640) // 2
                r['edx'] = st['disp'] // 640
                r['ebx'] &= ~0xffff
            r['eax'] = 0x4f
        elif ax == 0x4f05:
            r['eax'] = 0x4f
            r['edx'] &= ~0xffff
        elif ax == 0x4f0a:
            r['eax'] = 0x0100                   # no protected-mode interface
        else:
            print('VBE %04x unhandled' % ax, flush=True)
            r['eax'] = 0x0100

    def int10(r):
        ax = r['eax'] & 0xffff
        ah = ax >> 8
        if ah == 0x4f:
            vesa(r)
        elif ah == 0x00:
            st['mode'] = ax & 0xff
            st['gfx'] = False
            print('int 10h set mode %#x' % st['mode'], flush=True)
        elif ah == 0x03:
            r['edx'] &= ~0xffff
            r['ecx'] = (r['ecx'] & ~0xffff) | 0x0607
        elif ah in (0x02, 0x0e, 0x09):
            if ah == 0x0e:
                text(chr(ax & 0xff))
        else:
            print('int 10h ax=%04x unhandled' % ax, flush=True)

    def realint(intno, r):
        if intno == 0x10:
            int10(r)
        else:
            print('real-mode int %x ax=%04x unhandled' % (intno, r['eax'] & 0xffff), flush=True)

    RMS = (('edi', 0, 4), ('esi', 4, 4), ('ebp', 8, 4), ('ebx', 0x10, 4), ('edx', 0x14, 4), ('ecx', 0x18, 4),
           ('eax', 0x1c, 4), ('flags', 0x20, 2), ('es', 0x22, 2), ('ds', 0x24, 2))

    def int31():
        eax = reg(UC_X86_REG_EAX)
        ax = eax & 0xffff
        if ax == 0xee02:
            mu.reg_write(UC_X86_REG_EBX, 0)
            mu.reg_write(UC_X86_REG_ESI, PSP)
            mu.reg_write(UC_X86_REG_EDI, ENV)
            setcf(0)
        elif ax == 0xee42:
            size = reg(UC_X86_REG_EDX)
            p = (st['heap'] + 0xfff) & ~0xfff
            if p + size > RAM:
                setcf(1)
                print('EE42: out of memory for %#x' % size, flush=True)
                return
            st['heap'] = p + size
            mu.reg_write(UC_X86_REG_EDX, p)
            mu.reg_write(UC_X86_REG_EAX, size)
            print('EE42 alloc %#x -> %#x' % (size, p), flush=True)
            setcf(0)
        elif ax == 0x0300:
            edi = reg(UC_X86_REG_EDI)
            blk = bytes(mu.mem_read(edi, 0x32))
            r = {k: int.from_bytes(blk[o:o + n], 'little') for k, o, n in RMS}
            realint(reg(UC_X86_REG_EBX) & 0xff, r)
            b = bytearray(blk)
            for k, o, n in RMS:
                b[o:o + n] = (r[k] & ((1 << (8 * n)) - 1)).to_bytes(n, 'little')
            mu.mem_write(edi, bytes(b))
            setcf(0)
        elif ax == 0x0800:
            phys = (reg(UC_X86_REG_EBX) & 0xffff) << 16 | reg(UC_X86_REG_ECX) & 0xffff
            print('DPMI map physical %#x' % phys, flush=True)
            setcf(0)                            # identity: BX:CX unchanged
        elif ax == 0x0204:
            mu.reg_write(UC_X86_REG_ECX, 0x20)
            mu.reg_write(UC_X86_REG_EDX, IRET)
            setcf(0)
        elif ax == 0x0205:
            bl = reg(UC_X86_REG_EBX) & 0xff
            edx = reg(UC_X86_REG_EDX)
            if bl == 8:
                st['vec8'] = None if edx == IRET else edx
            print('set PM vector %#x -> %x (tick %d)' % (bl, edx, st['tick']), flush=True)
            setcf(0)
        else:
            print('int 31h ax=%04x unhandled at %x' % (ax, reg(UC_X86_REG_EIP)), flush=True)
            setcf(1)

    def int21():
        eax = reg(UC_X86_REG_EAX)
        ah = (eax >> 8) & 0xff
        edx = reg(UC_X86_REG_EDX)
        if ah == 0x09:
            text(cstr(edx))
        elif ah == 0x48:
            paras = reg(UC_X86_REG_EBX) & 0xffff
            seg = st['dos'] >> 4
            st['dos'] += paras * 16
            mu.reg_write(UC_X86_REG_EAX, (eax & ~0xffff) | seg)
            setcf(0)
        elif ah == 0x4c:
            print('exit code %d at tick %d' % (eax & 0xff, st['tick']), flush=True)
            st['stop'] = True
            mu.emu_stop()
        elif ah == 0x3c:
            name = cstr(edx, b'\x00', 80)
            os.makedirs(a.dumpdir, exist_ok=True)
            h = st['nfile']
            st['nfile'] += 1
            st['files'][h] = os.path.join(a.dumpdir, os.path.basename(name.replace('\\', '/')))
            open(st['files'][h], 'wb').close()
            print('create %r -> handle %d' % (name, h), flush=True)
            mu.reg_write(UC_X86_REG_EAX, (eax & ~0xffff) | h)
            setcf(0)
        elif ah == 0x40:
            h = reg(UC_X86_REG_EBX) & 0xffff
            n = reg(UC_X86_REG_ECX)
            if h in st['files']:
                with open(st['files'][h], 'ab') as f:
                    f.write(bytes(mu.mem_read(edx, n)))
            elif h in (1, 2):
                text(bytes(mu.mem_read(edx, n)).decode('latin1'))
            mu.reg_write(UC_X86_REG_EAX, n)
            setcf(0)
        elif ah == 0x3e:
            st['files'].pop(reg(UC_X86_REG_EBX) & 0xffff, None)
            setcf(0)
        else:
            print('int 21h ah=%02x unhandled at %x' % (ah, reg(UC_X86_REG_EIP)), flush=True)
            setcf(1)

    def on_intr(mu, intno, _):
        if intno == 0x31:
            int31()
        elif intno == 0x21:
            int21()
        elif intno == 0x10:
            r = {'eax': reg(UC_X86_REG_EAX), 'ebx': reg(UC_X86_REG_EBX), 'ecx': reg(UC_X86_REG_ECX),
                 'edx': reg(UC_X86_REG_EDX), 'edi': reg(UC_X86_REG_EDI), 'es': 0}
            int10(r)
            for k, rr in (('eax', UC_X86_REG_EAX), ('ebx', UC_X86_REG_EBX), ('ecx', UC_X86_REG_ECX),
                          ('edx', UC_X86_REG_EDX)):
                mu.reg_write(rr, r[k] & 0xffffffff)
        else:
            print('int %x at %x: stop' % (intno, reg(UC_X86_REG_EIP)), flush=True)
            print(' '.join('%s=%x' % (n, reg(r)) for n, r in (
                ('eax', UC_X86_REG_EAX), ('ebx', UC_X86_REG_EBX), ('ecx', UC_X86_REG_ECX), ('edx', UC_X86_REG_EDX),
                ('esi', UC_X86_REG_ESI), ('edi', UC_X86_REG_EDI), ('ebp', UC_X86_REG_EBP), ('esp', UC_X86_REG_ESP),
                ('cs', UC_X86_REG_CS), ('ds', UC_X86_REG_DS), ('es', UC_X86_REG_ES), ('ss', UC_X86_REG_SS))),
                flush=True)
            st['stop'] = True
            mu.emu_stop()

    def on_in(mu, port, size, _):
        ins['%x' % port] = ins.get('%x' % port, 0) + 1
        if port == 0x40:
            if st['latch'] is None:
                st['latch'] = pit_counter()
                st['latch_n'] = 0
            v = st['latch'] >> (8 * st['latch_n'])
            st['latch_n'] += 1
            if st['latch_n'] == 2:
                st['latch'] = None
            return v & 0xff
        if port == 0x3da:
            st['retrace'] ^= 0x09
            return st['retrace']
        if 0x240 <= port < 0x350:
            return gus.inp(port, size)
        return 0

    def on_out(mu, port, size, val, _):
        outs[port] = outs.get(port, 0) + 1
        if port == 0x43:
            if val & 0xc0 == 0 and val & 0x30 == 0:      # latch counter 0
                st['latch'] = pit_counter()
                st['latch_n'] = 0
            elif val & 0xc0 == 0:
                st['pit_w'] = 0
        elif port == 0x40:
            if st['pit_w'] == 0:
                st['pit_lo'] = val & 0xff
                st['pit_w'] = 1
            else:
                c = st['pit_lo'] | (val & 0xff) << 8
                st['pit_count'] = c or 0x10000
                st['pit_start'] = st['now']
                st['next_irq'] = st['now'] + st['pit_count']
                st['pit_w'] = 0
        elif port == 0x3c8:
            st['dac_w'] = (val & 0xff) * 3
        elif port == 0x3c9:
            pal[st['dac_w'] % 768] = val & 0x3f
            st['dac_w'] += 1
        elif 0x240 <= port < 0x350:
            gus.out(port, size, val)

    def on_part(mu, addr, size, _):
        p = MAIN_CALLS.get(addr)
        if p is not None and p != st['part']:
            st['part'] = p
            print('part %x at tick %d (time %.2f s, flips %d)' % (p, st['tick'], st['now'] / PIT_HZ,
                                                                 st['flips']), flush=True)

    def on_timer(mu, addr, size, _):
        # xmpGetTimer (0x29f44) returns here with EAX = time in PIT clocks; keep the first value of each frame
        if a.freeze_timer:
            if st.get('frozen') is None:
                st['frozen'] = reg(UC_X86_REG_EAX)
            mu.reg_write(UC_X86_REG_EAX, st['frozen'])
        if st['ftimer'] is None:
            v = reg(UC_X86_REG_EAX)
            st['ftimer'] = v - (1 << 32) if v >= 1 << 31 else v

    traced = set()
    def on_trace(mu, addr, size, _):
        if addr not in traced:
            traced.add(addr)
            print('trace %x tick %d time %.3f' % (addr, st['tick'], st['now'] / PIT_HZ), flush=True)

    for x in filter(None, a.trace.split(',')):
        mu.hook_add(UC_HOOK_CODE, on_trace, None, int(x, 16), int(x, 16))
    # Few code hooks: with emu_start(count=...) unicorn checks every code hook's range on every instruction.
    mu.hook_add(UC_HOOK_CODE, on_part, None, min(MAIN_CALLS), max(MAIN_CALLS))
    mu.hook_add(UC_HOOK_CODE, on_timer, None, 0x29f61, 0x29f61)
    mu.hook_add(UC_HOOK_INSN, on_in, None, 1, 0, UC_X86_INS_IN)
    mu.hook_add(UC_HOOK_INSN, on_out, None, 1, 0, UC_X86_INS_OUT)
    mu.hook_add(UC_HOOK_INTR, on_intr)

    eip = EIP0
    if a.load:
        snap = pickle.load(open(a.snap, 'rb'))
        mu.mem_write(0, snap['mem'])
        mu.mem_write(LFB, snap['lfb'])
        mu.context_restore(snap['ctx'])
        pal[:] = snap['pal']
        st.update(snap['st'])
        st.setdefault('ftimer', None)
        gus.__dict__.update(snap['gus'])
        gus.__dict__.setdefault('reads', {})
        gus.log = glog
        eip = snap['eip']
        print('resumed at tick', st['tick'])

    plug = {}
    if a.plugin:
        plug = {'mu': mu, 'st': st, 'a': a, 'reg': reg, 'struct': struct}
        exec(open(a.plugin).read(), plug)
    frames = open(a.out + '.frames', 'wb')
    hdr_bits = st.get('bits', a.bpp)
    frames.write(b'JIZZFRM1' + struct.pack('<IIIIII', 320, 200, hdr_bits, PAGE, 32, 0))
    ticks = open(a.out + '.ticks', 'w')
    ticks.write('# tick pit_count time_s order row event sync_index part flips eip\n')
    snap_ticks = {int(x) for x in a.snap_at.split(',') if x}
    zero = bytes(PAGE)

    def ips():
        b = part_budget.get(st['part'], a.budget)
        return b * PIT_HZ / REF_COUNT

    def deliver_irq():
        ctx = mu.context_save()
        esp = reg(UC_X86_REG_ESP) - 12
        mu.mem_write(esp, struct.pack('<III', SENTINEL, 0x20, 0x202))
        mu.reg_write(UC_X86_REG_ESP, esp)
        st['in_irq'] = True
        try:
            mu.emu_start(st['vec8'], SENTINEL, count=50_000_000)
        except UcError as e:
            print('error in irq at %x: %s' % (reg(UC_X86_REG_EIP), e), flush=True)
            st['stop'] = True
        st['in_irq'] = False
        mu.context_restore(ctx)

    def record(eip):
        t = st['tick']
        xmp = struct.unpack('<I', mu.mem_read(0x6546, 4))[0] + 0xfc000
        order = row = -1
        if st['vec8'] is not None or st['flips']:
            row = mu.mem_read(xmp + 6, 1)[0]
            order = mu.mem_read(xmp + 0x12, 1)[0]
        ev = struct.unpack('<i', mu.mem_read(0x6413, 4))[0]
        si = (struct.unpack('<I', mu.mem_read(0x651b, 4))[0] - 0x6417) // 4
        ticks.write('%d %d %.5f %d %d %d %d %x %d %x\n' % (t, st['pit_count'], st['now'] / PIT_HZ, order, row, ev,
                                                          si, st['part'], st['flips'], eip))
        if t >= a.start_tick:
            if st['gfx']:
                frames.write(bytes(mu.mem_read(LFB + st['disp'], PAGE)))
            else:
                frames.write(zero)

    while not st['stop'] and st['tick'] < a.ticks:
        if st['now'] >= st['next_irq'] - 1e-6:
            # timer tick: deliver IRQ0 (if the intro's handler is installed) and record
            t = st['tick']
            gus.tick = t
            if t in snap_ticks:
                g = {k: v for k, v in gus.__dict__.items() if k != 'log'}
                pickle.dump({'mem': bytes(mu.mem_read(0, RAM)), 'lfb': bytes(mu.mem_read(LFB, LFB_SIZE)),
                             'ctx': mu.context_save(), 'pal': bytes(pal), 'st': dict(st), 'gus': g, 'eip': eip},
                            open(a.snap.format(tick=t), 'wb'))
                print('snapshot at tick', t, flush=True)
            st['now'] = st['next_irq']
            st['pit_start'] = st['now']
            st['next_irq'] = st['now'] + st['pit_count']     # mode 2 reload; the handler may reprogram
            if st['vec8'] is not None:
                deliver_irq()
            record(eip)
            if 'on_tick' in plug: plug['on_tick'](t)
            st['tick'] += 1
            if t % 500 == 0:
                print('tick %d time %.2f part %x flips %d eip %x' % (t, st['now'] / PIT_HZ, st['part'],
                                                                    st['flips'], eip), flush=True)
            continue
        if st['vs_until'] is not None:
            if st['vs_until'] <= st['next_irq']:
                st['now'] = st['vs_until']
                st['vs_until'] = None
            else:
                st['now'] = st['next_irq']
            continue
        rate = ips()
        q = a.quantum or max(1000, a.budget // 64)
        n = int(min(q, (st['next_irq'] - st['now']) * rate / PIT_HZ + 1))
        try:
            mu.emu_start(eip, 0xffffffff, count=n)
        except UcError as e:
            print('error in main at %x: %s' % (reg(UC_X86_REG_EIP), e), flush=True)
            break
        eip = reg(UC_X86_REG_EIP)
        if st['vsync']:
            # stopped at a flip somewhere inside the quantum: unicorn does not say how many instructions ran,
            # charge half of it
            st['now'] += (n // 2) * PIT_HZ / rate
            st['vsync'] = False
            period = PIT_HZ / a.refresh
            st['vs_until'] = (int(st['now'] / period) + 1) * period
        else:
            st['now'] += n * PIT_HZ / rate
    if 'atexit_plugin' in plug: plug['atexit_plugin']()
    open(a.out + '.dram', 'wb').write(bytes(gus.dram))
    glog.close()
    flog.close()
    print('ended at tick %d, time %.2f s, flips %d' % (st['tick'], st['now'] / PIT_HZ, st['flips']))
    print('out ports', {hex(k): v for k, v in sorted(outs.items())})
    print('in ports', dict(sorted(ins.items())))
    print('GUS register reads', {hex(k): v for k, v in sorted(gus.reads.items())})
    print('text output:', repr(st['text']))


if __name__ == '__main__':
    main()

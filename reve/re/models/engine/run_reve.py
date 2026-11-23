"""Run REVE.EXE (Pulse, 1996) headless in unicorn, from its MZ entry, and record what it does.

(The engine scope's copy of reve/re/run_reve.py: it adds --dumps A,B,... --dump-max N --dump-from F --dumpdir D,
which saves the whole memory (zlib, with the registers, the refresh and the drawn-frame count) each time execution
reaches one of the addresses: models/engine/dumps.py reads the files.)

Unlike the other remakes' runners this one does not skip the extender. REVE.EXE is a small real-mode program
(Infiny Loader, then a packed loader) that unpacks LCA's "FLAT OS V1.0" extender; the extender finds XMS, sees that
the CPU is in real mode (no EMM386, no DPMI) and switches to protected mode by itself (its "raw" mode), loads
REVE.LFL (the demo, packed inside REVE.EXE) and calls it. Unicorn runs all of that, mode switches included: the
runner only answers what DOS, the BIOS and HIMEM would (int 21h, int 10h, int 15h, int 16h, int 1Ah, int 2Fh 43xxh
and the XMS entry point) and fakes the hardware.

Interrupts are delivered as the CPU would: in real mode through the IVT, in protected mode through the extender's
IDT (32-bit interrupt or trap gates at the same privilege level), with the frame pushed on the interrupted stack.
Software interrupts in protected mode go through the IDT too, so the extender's own services (int 0FFh) and its
reflection of int 21h / int 10h to real mode run as written; only when a real-mode interrupt reaches the BIOS
stubs at F000:xxxx does the runner answer it.

Faked hardware:
  PIC      both 8259s: initialisation words (the extender moves IRQ 0..7 to 80h..87h and 8..15 to 88h..8Fh), masks,
           in-service bits and EOI (non-specific and specific), so a handler is not re-entered before its EOI.
  PIT      channel 0 in modes 0, 2 and 3, latch reads.
  VGA      mode 13h at 0xa0000, the DAC, 0x3da (70.086 Hz, 449 lines; bit 3 during lines 412..413, bit 0 during
           400..448). A loop that reads 0x3da at the same place and gets the same value is spinning: time jumps to
           the next change. Sequencer, CRTC and graphics controller writes are counted and logged.
  GUS      at 0x240 (ULTRASND=240,3,3,7,7): register file, 1 MB DRAM, DMA uploads, timers 1 and 2 with their IRQ
           (the Gus class of stars/re/run_stars.py).
  DMA      both 8237s (address, count, page, mode).

Time: the PIT clock (1,193,182 Hz). Code runs at --ips instructions per second in slices of --quantum instructions;
handlers are charged like any other code.

Outputs (prefix --out):
  <out>.frames  header b'REVEFRM1' + <IIII: width 320, height 200, record bytes 64768, header bytes 24; then one
                record per screen refresh at the start of the vertical retrace: 64,000 bytes of VGA memory, 768 of DAC
  <out>.ticks   per refresh: frame, time (s), PIT interrupts, GUS timer interrupts, protected mode, main EIP
  <out>.gus     GUS writes, stamped with the number of GUS timer interrupts so far
  <out>.dram    final GUS DRAM
  stdout        services, mode switches, files, text output

usage: python3 run_reve.py --out full [--ips 100e6] [--frames N]
"""
import argparse
import os
import pickle
import struct
import sys
import time as htime

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_INTR, UC_HOOK_CODE, UC_HOOK_INSN, UcError
from unicorn.x86_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
while not os.path.exists(os.path.join(HERE, 'run_reve.py')) or os.path.basename(HERE) != 're':
    if os.path.dirname(HERE) == HERE:
        break
    HERE = os.path.dirname(HERE)
ORIG = os.path.join(os.path.dirname(HERE), 'original')

PIT_HZ = 1193182
VGA_HZ = 25175000 / (800 * 449)
FRAME = PIT_HZ / VGA_HZ
LINES = 449
LINE = FRAME / LINES
RAM = 0x2000000                 # 32 MB: 16 MB as the machine has it, then the moved interrupt stacks
XMS_END = 0x1000000             # HIMEM hands out 0x110000..16 MB
IRQ_STACKS = 0x1800000          # the extender's four interrupt stacks, moved here (see "set_irq_stacks")
K_STACK_SLOTS = 0x10c60 + 0xd4  # kernel 0xd4..0xe0: the stack tops its IRQ entry 0xf8e takes, first free first
K_SLOTS_SET = 0x10c60 + 0x1d3   # right after the kernel set them (0x1b0..0x1ce) to 0x140c8 + 0x200 i
SORT = 0x21e6ec                 # the 3D engine's recursive face sort (its stack depth is logged)
# main 0x204770 (NOTES.md "The script table"): the loop over the script table 0x205ed0
MAIN_ENTRY = 0x204787           # push esi: an entry begins (ESI = its address); init follows
MAIN_SET = 0x20479c             # its retrace routine has just been set ([0x205e9c])
MAIN_FRAME = 0x2047b1           # call [esi + 0xc]: one drawn frame
SCRIPT = 0x205ed0
LOAD = 0x1000                   # load segment of REVE.EXE (PSP at 0x0ff0)
ENVSEG = 0x0800
BIOS = 0xF000                   # every BIOS / DOS vector points to an iret at F000:n
XMS_ENTRY = 0xF0300
XMS_BASE = 0x110000             # extended memory handed out by the fake HIMEM
DOS_TOP = 0x9F00                # top of conventional memory (paragraphs)
# LCA's MOD player in REVE.LFL as the extender loads it here (image at linear 0x20427c): order and row
MUS_ORDER, MUS_ROW = 0x26042d, 0x26042c
ENV = b'PATH=C:\\\x00COMSPEC=C:\\COMMAND.COM\x00ULTRASND=240,3,3,7,7\x00\x00\x01\x00C:\\REVE\\REVE.EXE\x00'

R16 = {'ax': UC_X86_REG_AX, 'bx': UC_X86_REG_BX, 'cx': UC_X86_REG_CX, 'dx': UC_X86_REG_DX, 'si': UC_X86_REG_SI,
       'di': UC_X86_REG_DI, 'bp': UC_X86_REG_BP, 'sp': UC_X86_REG_SP}
REGS32 = (('eax', UC_X86_REG_EAX), ('ebx', UC_X86_REG_EBX), ('ecx', UC_X86_REG_ECX), ('edx', UC_X86_REG_EDX),
          ('esi', UC_X86_REG_ESI), ('edi', UC_X86_REG_EDI), ('ebp', UC_X86_REG_EBP), ('esp', UC_X86_REG_ESP),
          ('cs', UC_X86_REG_CS), ('ds', UC_X86_REG_DS), ('es', UC_X86_REG_ES), ('ss', UC_X86_REG_SS),
          ('fs', UC_X86_REG_FS), ('gs', UC_X86_REG_GS), ('eip', UC_X86_REG_EIP), ('efl', UC_X86_REG_EFLAGS))


class Gus:
    """Gravis UltraSound: register file, 1 MB DRAM, timers, DMA (stars/re/run_stars.py)."""
    def __init__(self, log, base=0x240):
        self.dram = bytearray(1 << 20)
        self.base = base
        self.voice = 0
        self.reg = 0
        self.addr = 0
        self.regs = {}
        self.glob = {}
        self.log = log
        self.tick = 0
        self.lo = 0
        self.reads = {}
        self.adlib_idx = 0
        self.t_ctrl = 0
        self.t1 = 0
        self.t2 = 0
        self.t_start = 0
        self.irq_status = 0
        self.dma_pending = False
        self.dma = None
        self.raise_irq = None
        self.dma_count = 0
        self.dma_bytes = 0

    def out(self, port, size, val):
        p = port - self.base
        if size == 2 and p == 0x102:
            self.voice = val & 31
            self.reg = (val >> 8) & 0xff
        elif p == 0x102:
            self.voice = val & 31
        elif p == 0x103:
            self.reg = val & 0xff
            if size == 2:
                self.lo = (val >> 8) & 0xff
        elif p == 0x104:
            if size == 2:
                self._write(self.reg, val & 0xffff, 2)
            else:
                self.lo = val & 0xff
        elif p == 0x105:
            self._write(self.reg, val & 0xff, 1)
        elif p == 0x107:
            self.dram[self.addr & 0xfffff] = val & 0xff
        elif p == 0x008:
            self.adlib_idx = val & 0xff
            self.log.write('%d p%03x %x\n' % (self.tick, port, val))
        elif p == 0x009:
            if self.adlib_idx == 4:
                if val & 0x80:
                    self.irq_status &= ~0x0c
                else:
                    self.t_start = val & 3
            self.log.write('%d p%03x %x\n' % (self.tick, port, val))
        else:
            self.log.write('%d p%03x %x\n' % (self.tick, port, val))

    def _write(self, reg, val, size):
        if reg == 0x43:
            self.addr = (self.addr & 0xff0000) | (val & 0xffff)
        elif reg == 0x44:
            self.addr = (self.addr & 0xffff) | ((val & 0xff) << 16)
        elif reg == 0x45:
            self.t_ctrl = val & 0xff
            if not val & 4:
                self.irq_status &= ~4
            if not val & 8:
                self.irq_status &= ~8
            self.log.write('%d g r45 %x\n' % (self.tick, val))
        elif reg == 0x41:
            self.glob[reg] = val
            self.log.write('%d g r41 %x\n' % (self.tick, val))
            if val & 1 and self.dma is not None:
                r42 = self.glob.get(0x42, 0)
                if val & 4:
                    dst = ((r42 & 0xc000) << 4) | ((r42 & 0x1fff) << 5)
                else:
                    dst = r42 << 4
                data = bytearray(self.dma(val))
                if val & 0x80:
                    for i in range(1 if val & 0x40 else 0, len(data), 2 if val & 0x40 else 1):
                        data[i] ^= 0x80
                self.dram[dst:dst + len(data)] = data[:max(0, (1 << 20) - dst)]
                self.dma_count += 1
                self.dma_bytes += len(data)
                self.dma_pending = True
                self.irq_status |= 0x80
                if val & 0x20 and self.raise_irq:
                    self.raise_irq()
        elif reg == 0x46:
            self.t1 = val & 0xff
            self.log.write('%d g r46 %x\n' % (self.tick, val))
        elif reg == 0x47:
            self.t2 = val & 0xff
            self.log.write('%d g r47 %x\n' % (self.tick, val))
        elif reg >= 0x40:
            self.glob[reg] = val
            self.log.write('%d g r%02x %x\n' % (self.tick, reg, val))
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
            if 0x80 <= r < 0x90:
                v = self.regs.get((self.voice, r - 0x80), 0)
                if r == 0x80:
                    v |= 1
                if r == 0x8d:
                    v |= 1
                if p == 0x105 and size == 1 and r in (0x81, 0x82, 0x83, 0x84, 0x85, 0x89, 0x8a, 0x8b):
                    return (v >> 8) & 0xff
                return v & (0xff if size == 1 else 0xffff)
            if r == 0x8f:
                return 0xe0
            if r == 0x41:
                v = (self.glob.get(0x41, 0) & ~0x40) | (0x40 if self.dma_pending else 0)
                self.dma_pending = False
                self.irq_status &= ~0x80
                return v
            if r in (0x49, 0x45, 0x4c, 0x8e):
                return self.glob.get(r & 0x7f, 0) & 0xff
            return 0
        if p == 0x006:
            return self.irq_status
        if p == 0x008:
            s = 0
            if self.irq_status & 4:
                s |= 0xc0
            if self.irq_status & 8:
                s |= 0xa0
            return s
        if p == 0x103:
            return self.reg
        if p == 0x102:
            return self.voice
        return 0xff

    def period(self, which):
        if which == 1:
            return (256 - self.t1) * 80e-6 * PIT_HZ
        return (256 - self.t2) * 320e-6 * PIT_HZ


class Pic:
    def __init__(self, base):
        self.base = base
        self.mask = 0xff
        self.isr = 0
        self.icw = 0
        self.vec = base
        self.read_isr = False
        self.irr = 0

    def inp(self, port):
        if port & 1:
            return self.mask
        return self.isr if self.read_isr else self.irr

    def out(self, port, val):
        if port & 1 == 0:
            if val & 0x10:
                self.icw = 1
                self.isr = 0
            elif val & 0x08:                           # OCW3: bit 1 = 1 selects the register a read returns
                if val & 2:
                    self.read_isr = bool(val & 1)
            elif val & 0x20:                           # EOI
                if val & 0x40:
                    self.isr &= ~(1 << (val & 7))
                elif self.isr:
                    low = self.isr & -self.isr
                    self.isr &= ~low
        else:
            if self.icw == 1:
                self.vec = val & 0xf8
                self.icw = 2
            elif self.icw == 2:
                self.icw = 3
            elif self.icw == 3:
                self.icw = 0
            else:
                self.mask = val & 0xff


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default=os.path.join(ORIG, 'REVE.EXE'))
    ap.add_argument('--out', required=True)
    ap.add_argument('--args', default='', help='command line (NOSOUND, ...)')
    ap.add_argument('--frames', type=int, default=1 << 30, help='stop at this screen refresh')
    ap.add_argument('--ips', type=float, default=100e6, help='instructions per second')
    ap.add_argument('--quantum', type=int, default=0)
    ap.add_argument('--snap-at', default='', help='snapshot at these frames (comma-separated)')
    ap.add_argument('--snapdir', default=os.path.join(HERE, 'snaps'))
    ap.add_argument('--snap', help='snapshot to resume from (with --load)')
    ap.add_argument('--load', action='store_true')
    ap.add_argument('--trace', default='', help='linear addresses: print when each first runs')
    ap.add_argument('--verbose', action='store_true', help='print every real-mode service call')
    ap.add_argument('--no-frames', action='store_true')
    ap.add_argument('--dumps', default='', help='engine scope: linear addresses at which to dump memory')
    ap.add_argument('--dump-max', type=int, default=8, help='dumps per address')
    ap.add_argument('--dump-from', type=int, default=0, help='first refresh at which to dump')
    ap.add_argument('--dumpdir', default='dumps')
    ap.add_argument('--history', type=int, default=0, help='keep the last N instructions, print them on an error')
    a = ap.parse_args()

    exe = open(a.exe, 'rb').read()
    # UC_MODE_32 started in real mode: in UC_MODE_16 unicorn sets EIP = (start - CS * 16) & 0xffff on every
    # emu_start, which breaks as soon as the extender runs 32-bit code above 64 KB. Here emu_start gets EIP.
    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(0, RAM)
    mu.reg_write(UC_X86_REG_CR0, 0x10)

    glog = open(a.out + '.gus', 'w')
    gus = Gus(glog)
    pic1, pic2 = Pic(8), Pic(0x70)
    st = {'frame': 0, 'now': 0.0, 'pit_mode': 3, 'pit_count': 0x10000, 'pit_start': 0.0, 'pit_w': 0,
          'pit_lo': 0, 'latch': None, 'latch_n': 0, 'next_pit': None, 'next_t1': None, 'next_t2': None,
          'next_vs': FRAME * 412 / LINES, 'pending': set(), 'stop': False, 'text': '', 'files': {}, 'nextfh': 5,
          'dac_w': 0, 'dac_r': 0, 'dac_c': 0, 'dac': bytearray(768), 'spin': None, 'skip': False, 'mode': 3,
          'pit_irqs': 0, 'gus_irqs': 0, 'dos_next': 0, 'xms': {}, 'xms_next': XMS_BASE, 'pm': False,
          'seq': {}, 'crtc': {}, 'gc': {}, 'seq_i': 0, 'crtc_i': 0, 'gc_i': 0, 'vga_log': 0, 'main_pc': 0, 'entry': -1,
          'drawn': 0}
    outs, ins, services = {}, {}, {}

    def R(r): return mu.reg_read(r)

    def W(r, v): mu.reg_write(r, v)

    def setflag(bit, c):
        f = R(UC_X86_REG_EFLAGS)
        W(UC_X86_REG_EFLAGS, (f | bit) if c else (f & ~bit))

    def setcf(c): setflag(1, c)

    def count(name):
        services[name] = services.get(name, 0) + 1

    def regs():
        return ' '.join('%s=%x' % (n, R(r)) for n, r in REGS32)

    def text(s):
        st['text'] += s
        while '\n' in st['text'] or '\r' in st['text']:
            i = min(x for x in (st['text'].find('\n'), st['text'].find('\r')) if x >= 0)
            line, st['text'] = st['text'][:i], st['text'][i + 1:]
            if line.strip():
                print('text: %s' % line.rstrip()[-200:], flush=True)

    # ---- load the MZ executable as DOS does -------------------------------------------------------------------
    f = struct.unpack_from('<14H', exe, 0)
    _, cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, csum, ip, cs, lfarlc, ovno = f
    size = (cp - 1) * 512 + cblp if cblp else cp * 512
    assert crlc == 0
    image = exe[cparhdr * 16:size]
    psp = LOAD - 0x10
    mu.mem_write(ENVSEG * 16, ENV)
    p = bytearray(256)
    p[0:2] = b'\xcd\x20'
    struct.pack_into('<H', p, 2, DOS_TOP)
    struct.pack_into('<H', p, 0x2c, ENVSEG)
    cmd = (b' ' + a.args.encode()) if a.args else b''
    p[0x80] = len(cmd)
    p[0x81:0x81 + len(cmd) + 1] = cmd + b'\r'
    mu.mem_write(psp * 16, bytes(p))
    mu.mem_write(LOAD * 16, image)
    mu.mem_write(BIOS * 16, b'\xcf' * 256)
    mu.mem_write(XMS_ENTRY, b'\xcb')
    for i in range(256):
        mu.mem_write(i * 4, struct.pack('<HH', i, BIOS))
    bda = bytearray(256)
    struct.pack_into('<H', bda, 0x08, 0x378)
    struct.pack_into('<H', bda, 0x10, 0x0026)
    struct.pack_into('<H', bda, 0x13, 640)
    bda[0x49] = 3
    struct.pack_into('<H', bda, 0x4a, 80)
    struct.pack_into('<H', bda, 0x4c, 0x1000)
    struct.pack_into('<H', bda, 0x63, 0x3d4)
    bda[0x84] = 24
    mu.mem_write(0x400, bytes(bda))
    st['dos_next'] = 0                         # set at the first int 21h 4Ah
    for r, v in ((UC_X86_REG_DS, psp), (UC_X86_REG_ES, psp), (UC_X86_REG_SS, LOAD + ss), (UC_X86_REG_SP, sp),
                 (UC_X86_REG_CS, LOAD + cs), (UC_X86_REG_IP, ip)):
        W(r, v)
    pc = (LOAD + cs) * 16 + ip

    # ---- descriptor tables ------------------------------------------------------------------------------------
    def pm():
        return R(UC_X86_REG_CR0) & 1

    def descriptor(sel):
        if sel & 4:
            ldtr = R(UC_X86_REG_LDTR)
            base = ldtr[1]
        else:
            base = R(UC_X86_REG_GDTR)[1]
        d = bytes(mu.mem_read(base + (sel & 0xfff8), 8))
        lo, hi = struct.unpack('<II', d)
        b = (lo >> 16) | ((hi & 0xff) << 16) | (hi & 0xff000000)
        lim = (lo & 0xffff) | (hi & 0xf0000)
        if hi & 0x800000:
            lim = (lim << 12) | 0xfff
        return b, lim, (hi >> 8) & 0xff, (hi >> 20) & 0xf

    def seg_base(r):
        sel = R(r)
        if not pm():
            return sel << 4
        if sel & 0xfffc == 0:
            return 0
        return descriptor(sel)[0]

    def lin(segreg, off):
        return (seg_base(segreg) + off) & 0xffffffff

    def push_frame(words, big):
        """Push words (oldest first) on SS:(E)SP, as 32-bit values if big, else 16-bit."""
        ss_b = seg_base(UC_X86_REG_SS)
        stack32 = bool(pm() and R(UC_X86_REG_SS) & 0xfffc and descriptor(R(UC_X86_REG_SS))[3] & 4)
        esp = R(UC_X86_REG_ESP) if stack32 else R(UC_X86_REG_SP)
        n = 4 if big else 2
        for w in words:
            esp = (esp - n) & (0xffffffff if stack32 else 0xffff)
            mu.mem_write(ss_b + esp, struct.pack('<I' if big else '<H', w & (0xffffffff if big else 0xffff)))
        if stack32:
            W(UC_X86_REG_ESP, esp)
        else:
            W(UC_X86_REG_ESP, (R(UC_X86_REG_ESP) & 0xffff0000) | esp)

    def dispatch(intno, eip_next):
        """Enter interrupt intno as the CPU would, returning to eip_next."""
        fl = R(UC_X86_REG_EFLAGS)
        if not pm():
            o, sg = struct.unpack('<HH', mu.mem_read(intno * 4, 4))
            push_frame([fl, R(UC_X86_REG_CS), eip_next], False)
            W(UC_X86_REG_EFLAGS, fl & ~0x300)
            W(UC_X86_REG_CS, sg)
            W(UC_X86_REG_IP, o)
            return sg * 16 + o
        idtr = R(UC_X86_REG_IDTR)
        if intno * 8 + 7 > idtr[2]:
            raise RuntimeError('int %#x beyond the IDT limit' % intno)
        g = bytes(mu.mem_read(idtr[1] + intno * 8, 8))
        off = struct.unpack_from('<H', g, 0)[0] | struct.unpack_from('<H', g, 6)[0] << 16
        sel = struct.unpack_from('<H', g, 2)[0]
        typ = g[5] & 0x1f
        cpl = R(UC_X86_REG_CS) & 3
        dpl = descriptor(sel)[2] >> 5 & 3
        if typ not in (0x0e, 0x0f) or cpl != dpl:
            raise RuntimeError('int %#x: gate type %#x, CPL %d -> DPL %d not handled' % (intno, typ, cpl, dpl))
        push_frame([fl, R(UC_X86_REG_CS), eip_next], True)
        W(UC_X86_REG_EFLAGS, fl & ~(0x100 | 0x4000 | 0x10000 | (0x200 if typ == 0x0e else 0)))
        W(UC_X86_REG_CS, sel)
        W(UC_X86_REG_EIP, off)
        return descriptor(sel)[0] + off

    # ---- DOS, BIOS, HIMEM ---------------------------------------------------------------------------------------
    def r8(name):
        return R(R16[name]) & 0xff

    def set16(name, v):
        r = R16[name]
        W(r, (R(r) & 0xffff0000) | (v & 0xffff))

    def ds_dx():
        return (R(UC_X86_REG_DS) << 4) + R(UC_X86_REG_DX)

    def int21():
        ax = R(UC_X86_REG_AX)
        ah, al = ax >> 8, ax & 0xff
        count('int21/%02x' % ah)
        if a.verbose:
            print('int 21h ax=%04x bx=%04x cx=%04x dx=%04x ds=%04x' % (ax, R(UC_X86_REG_BX), R(UC_X86_REG_CX),
                                                                    R(UC_X86_REG_DX), R(UC_X86_REG_DS)), flush=True)
        if ah == 0x30:
            set16('ax', 0x0006)
            set16('bx', 0)
        elif ah == 0x4a:
            bx = R(UC_X86_REG_BX)
            seg = R(UC_X86_REG_ES)
            st['dos_next'] = max(st['dos_next'], seg + bx + 1)
            setcf(0)
        elif ah == 0x48:
            bx = R(UC_X86_REG_BX)
            free = DOS_TOP - st['dos_next'] - 1
            if bx > free:
                set16('ax', 8)
                set16('bx', free)
                setcf(1)
            else:
                set16('ax', st['dos_next'])
                print('DOS alloc %d paragraphs at %04x' % (bx, st['dos_next']), flush=True)
                st['dos_next'] += bx + 1
                setcf(0)
        elif ah == 0x49:
            setcf(0)
        elif ah in (0x09,):
            text(bytes(mu.mem_read(ds_dx(), 2000)).split(b'$')[0].decode('cp437'))
            setcf(0)
        elif ah == 0x02:
            text(chr(R(UC_X86_REG_DX) & 0xff))
        elif ah == 0x4c:
            print('exit code %d at frame %d (time %.2f s)' % (al, st['frame'], st['now'] / PIT_HZ), flush=True)
            st['stop'] = True
            mu.emu_stop()
        elif ah == 0x3d:
            name = bytes(mu.mem_read(ds_dx(), 128)).split(b'\0')[0].decode('latin1')
            n = name.replace('\\', '/').split('/')[-1].upper()
            path = None
            if os.path.isdir(ORIG):
                for fn in os.listdir(ORIG):
                    if fn.upper() == n:
                        path = os.path.join(ORIG, fn)
            if n == 'REVE.EXE':
                path = a.exe
            print('open %r -> %s' % (name, path), flush=True)
            if path is None:
                set16('ax', 2)
                setcf(1)
            else:
                h = st['nextfh']
                st['nextfh'] += 1
                st['files'][h] = [open(path, 'rb').read(), 0, name]
                set16('ax', h)
                setcf(0)
        elif ah == 0x3e:
            st['files'].pop(R(UC_X86_REG_BX), None)
            setcf(0)
        elif ah == 0x3f:
            fo = st['files'].get(R(UC_X86_REG_BX))
            if fo is None:
                set16('ax', 6)
                setcf(1)
                return
            n = R(UC_X86_REG_CX)
            data = fo[0][fo[1]:fo[1] + n]
            fo[1] += len(data)
            mu.mem_write(ds_dx(), data)
            set16('ax', len(data))
            setcf(0)
        elif ah == 0x40:
            h = R(UC_X86_REG_BX)
            n = R(UC_X86_REG_CX)
            if h in (1, 2):
                text(bytes(mu.mem_read(ds_dx(), n)).decode('cp437'))
            set16('ax', n)
            setcf(0)
        elif ah == 0x42:
            fo = st['files'].get(R(UC_X86_REG_BX))
            if fo is None:
                set16('ax', 6)
                setcf(1)
                return
            off = R(UC_X86_REG_CX) << 16 | R(UC_X86_REG_DX)
            if off >= 0x80000000:
                off -= 1 << 32
            fo[1] = [off, fo[1] + off, len(fo[0]) + off][al]
            set16('ax', fo[1] & 0xffff)
            set16('dx', fo[1] >> 16)
            setcf(0)
        elif ah == 0x44:
            if al == 0:
                set16('dx', 0x80d3 if R(UC_X86_REG_BX) < 5 else 0x0002)
                setcf(0)
            else:
                set16('ax', 1)
                setcf(1)
        elif ah == 0x35:
            o, sg = struct.unpack('<HH', mu.mem_read(al * 4, 4))
            W(UC_X86_REG_ES, sg)
            set16('bx', o)
        elif ah == 0x25:
            mu.mem_write(al * 4, struct.pack('<HH', R(UC_X86_REG_DX), R(UC_X86_REG_DS)))
        elif ah == 0x2c:
            set16('cx', 0x0c00)
            set16('dx', 0)
        elif ah == 0x2a:
            set16('cx', 1996)
            set16('dx', 0x0510)
        elif ah == 0x19:
            set16('ax', (ax & 0xff00) | 2)
        elif ah == 0x47:
            mu.mem_write((R(UC_X86_REG_DS) << 4) + R(UC_X86_REG_SI), b'REVE\x00')
            setcf(0)
        elif ah == 0x0b:
            set16('ax', ax & 0xff00)
        elif ah == 0x62 or ah == 0x51:
            set16('bx', psp)
        elif ah == 0x2f:
            W(UC_X86_REG_ES, psp)
            set16('bx', 0x80)
        elif ah == 0x1a:
            pass
        else:
            print('int 21h ax=%04x unhandled: %s' % (ax, regs()), flush=True)
            setcf(1)

    def int10():
        ax = R(UC_X86_REG_AX)
        ah = ax >> 8
        count('int10/%02x' % ah)
        if ah == 0x00:
            m = ax & 0x7f
            st['mode'] = m
            print('int 10h set mode %#x (frame %d, time %.3f s)' % (m, st['frame'], st['now'] / PIT_HZ), flush=True)
            if m == 0x13 and not ax & 0x80:
                mu.mem_write(0xa0000, bytes(65536))
            mu.mem_write(0x449, bytes([m]))
        elif ah == 0x0f:
            set16('ax', 0x2800 | st['mode'] if st['mode'] == 0x13 else 0x5000 | st['mode'])
            set16('bx', 0)
        elif ah == 0x10:
            pass
        elif ah == 0x1a:
            set16('ax', 0x001a)
            set16('bx', 0x0008)
        elif ah in (0x01, 0x02, 0x03, 0x06, 0x09, 0x0e, 0x11, 0x12):
            if ah == 0x0e:
                text(chr(ax & 0xff))
        else:
            print('int 10h ax=%04x unhandled' % ax, flush=True)

    def xms():
        ah = R(UC_X86_REG_AX) >> 8
        count('xms/%02x' % ah)
        if ah == 0:
            set16('ax', 0x0300)
            set16('bx', 0x0300)
            set16('dx', 0)
        elif ah in (3, 4, 5, 6, 7):
            set16('ax', 1)
            set16('bx', 0)
        elif ah == 8:
            free = (XMS_END - st['xms_next']) // 1024
            set16('ax', free)
            set16('dx', free)
            set16('bx', 0)
        elif ah == 9:
            kb = R(UC_X86_REG_DX)
            h = len(st['xms']) + 1
            st['xms'][h] = (st['xms_next'], kb * 1024)
            print('XMS alloc %d KB at %#x (handle %d)' % (kb, st['xms_next'], h), flush=True)
            st['xms_next'] += kb * 1024
            set16('ax', 1)
            set16('dx', h)
        elif ah == 0x0c:
            b = st['xms'][R(UC_X86_REG_DX)][0]
            set16('ax', 1)
            set16('dx', b >> 16)
            set16('bx', b & 0xffff)
        elif ah in (0x0a, 0x0d):
            set16('ax', 1)
        else:
            print('XMS function %#x unhandled' % ah, flush=True)
            set16('ax', 0)
            set16('bx', 0x80)

    def bios(intno):
        ax = R(UC_X86_REG_AX)
        if intno == 0x21:
            int21()
        elif intno == 0x10:
            int10()
        elif intno == 0x2f:
            count('int2f/%04x' % ax)
            if ax == 0x4300:
                set16('ax', 0x4380)
            elif ax == 0x4310:
                W(UC_X86_REG_ES, XMS_ENTRY >> 4 & 0xf000)
                set16('bx', XMS_ENTRY & 0xffff)
        elif intno == 0x16:
            count('int16/%04x' % ax)
            if ax >> 8 in (0x01, 0x11):
                setflag(0x40, 1)
            else:
                set16('ax', 0)
        elif intno == 0x15:
            count('int15/%04x' % ax)
            setcf(1)
            set16('ax', 0x8600)
        elif intno == 0x11:
            set16('ax', 0x0026)
        elif intno == 0x12:
            set16('ax', 640)
        elif intno == 0x1a:
            count('int1a/%04x' % ax)
            t = int(st['now'] / 65536)
            set16('cx', t >> 16)
            set16('dx', t & 0xffff)
            set16('ax', 0)
        elif intno == 0x67:
            set16('ax', 0x8400)
        elif intno == 0x33:
            set16('ax', 0)
        else:
            print('real-mode int %#x ax=%04x unhandled: %s' % (intno, ax, regs()), flush=True)

    def on_intr(mu, intno, _):
        try:
            if not pm():
                o, sg = struct.unpack('<HH', mu.mem_read(intno * 4, 4))
                if sg == BIOS:
                    bios(intno)
                    return
                dispatch(intno, R(UC_X86_REG_IP))
                return
            if intno not in (0x21, 0xff, 0x10, 0x31) and intno < 0x20:
                print('exception %#x in protected mode: %s' % (intno, regs()), flush=True)
                for h in st.get('hist', [])[-a.history:]:
                    print('  %x -> %x  %04x:%08x esp=%x' % h)
                st['stop'] = True
                mu.emu_stop()
                return
            count('pm-int/%02x' % intno)
            if intno == 0xff:
                flat_service(R(UC_X86_REG_EAX) & 0xffff)
            dispatch(intno, R(UC_X86_REG_EIP))
        except Exception as e:
            print('interrupt %#x failed: %s; %s' % (intno, e, regs()), flush=True)
            st['stop'] = True
            mu.emu_stop()

    def on_return(linear, fn):
        h = []

        def cb(mu, addr, size, _):
            fn()
            mu.hook_del(h[0])
        h.append(mu.hook_add(UC_HOOK_CODE, cb, None, linear, linear))

    def flat_service(ax):
        """Log the FLAT OS calls the demo makes (int 0FFh, AX = function)."""
        count('flat/%04x' % ax)
        ret = seg_base(UC_X86_REG_CS) + R(UC_X86_REG_EIP)
        if ax == 0x209:
            name = bytes(mu.mem_read(seg_base(UC_X86_REG_DS) + R(UC_X86_REG_ESI), 80)).split(b'\0')[0]
            dst = R(UC_X86_REG_EDI)

            def done():
                print('FLAT 0209 load %r (edi in %x) -> edi %x ecx %x cf %d (frame %d)' % (
                    name.decode('latin1'), dst, R(UC_X86_REG_EDI), R(UC_X86_REG_ECX), R(UC_X86_REG_EFLAGS) & 1,
                    st['frame']), flush=True)
                st.setdefault('loaded', []).append((name.decode('latin1'), R(UC_X86_REG_EDI), R(UC_X86_REG_ECX)))
            on_return(ret, done)
        elif st.get('flat_log', 0) < 400:
            st['flat_log'] = st.get('flat_log', 0) + 1
            print('FLAT %04x ebx=%x ecx=%x edx=%x esi=%x edi=%x (frame %d)' % (
                ax, R(UC_X86_REG_EBX), R(UC_X86_REG_ECX), R(UC_X86_REG_EDX), R(UC_X86_REG_ESI), R(UC_X86_REG_EDI),
                st['frame']), flush=True)

    def on_code_xms(mu, addr, size, _):
        if not pm():
            xms()

    # ---- ports ---------------------------------------------------------------------------------------------------
    def pit_counter():
        el = st['now'] - st['pit_start']
        c = st['pit_count']
        if st['pit_mode'] == 3:
            return int(c - 2 * el) % c & 0xffff if c else 0
        if st['pit_mode'] == 2:
            return int(c - el) % c & 0xffff
        return int(c - el) & 0xffff

    def vga_status(t):
        line = (t % FRAME) / LINE
        v = 0
        if line >= 400:
            v |= 1
        if 412 <= line < 414:
            v |= 8
        return v

    def vga_next_change(t):
        ph = t % FRAME
        b = t - ph
        for edge in (400 * LINE, 412 * LINE, 414 * LINE, FRAME):
            if ph < edge - 1e-6:
                return b + edge + 1e-4
        return b + FRAME + 1e-4

    DMA_PAGE = {0x87: 0, 0x83: 1, 0x81: 2, 0x82: 3, 0x8f: 4, 0x8b: 5, 0x89: 6, 0x8a: 7}
    dma = {'addr': [0] * 8, 'count': [0] * 8, 'page': [0] * 8, 'ff': [0, 0], 'mode': [0] * 8, 'mask': [1] * 8}

    def dma_out(port, val):
        if port < 0x10:
            c, ch0, pp = 0, 0, port
        else:
            c, ch0, pp = 1, 4, (port - 0xc0) >> 1
        if pp < 8:
            ch = ch0 + (pp >> 1)
            key = 'addr' if pp % 2 == 0 else 'count'
            if dma['ff'][c] == 0:
                dma[key][ch] = (dma[key][ch] & 0xff00) | val
            else:
                dma[key][ch] = (dma[key][ch] & 0x00ff) | (val << 8)
            dma['ff'][c] ^= 1
        elif pp == 0xc:
            dma['ff'][c] = 0
        elif pp == 0xb:
            dma['mode'][ch0 + (val & 3)] = val
        elif pp == 0xa:
            dma['mask'][ch0 + (val & 3)] = val >> 2 & 1

    def dma_read(r41):
        # the GUS register 0x41 does not name the channel: take the unmasked 8237 channel
        chans = [c for c in range(8) if not dma['mask'][c]] or [1]
        ch = chans[0]
        n = dma['count'][ch] + 1
        if ch < 4:
            l = dma['page'][ch] << 16 | dma['addr'][ch]
        else:
            l = (dma['page'][ch] & 0xfe) << 16 | dma['addr'][ch] << 1
            n *= 2
        return bytes(mu.mem_read(l, n))

    def on_in(mu, port, size, _):
        ins[port] = ins.get(port, 0) + 1
        if port == 0x3da:
            t = st['now']
            v = vga_status(t)
            eip = R(UC_X86_REG_EIP)
            if st['spin'] == (eip, v):
                st['now'] = vga_next_change(t)
                v = vga_status(st['now'])
                st['skip'] = True
                mu.emu_stop()
            st['spin'] = (eip, v)
            return v
        st['spin'] = None
        if port == 0x40:
            if st['latch'] is None:
                st['latch'] = pit_counter()
                st['latch_n'] = 0
            v = st['latch'] >> (8 * st['latch_n'])
            st['latch_n'] += 1
            if st['latch_n'] == 2:
                st['latch'] = None
            return v & 0xff
        if port == 0x3c9:
            v = st['dac'][st['dac_r'] * 3 + st['dac_c']]
            st['dac_c'] += 1
            if st['dac_c'] == 3:
                st['dac_c'] = 0
                st['dac_r'] = (st['dac_r'] + 1) & 0xff
            return v
        if port in (0x20, 0x21):
            pic1.irr = sum(1 << i for i in st['pending'] if i < 8)
            return pic1.inp(port)
        if port in (0xa0, 0xa1):
            pic2.irr = sum(1 << (i - 8) for i in st['pending'] if i >= 8)
            return pic2.inp(port)
        if port == 0x60:
            return 0x81
        if port == 0x64:
            return 0x14
        if port == 0x61:
            return 0x20 if int(st['now'] / 18) & 1 else 0
        if port == 0x3c5:
            return st['seq'].get(st['seq_i'], 0)
        if port == 0x3cf:
            return st['gc'].get(st['gc_i'], 0)
        if port == 0x3d5:
            return st['crtc'].get(st['crtc_i'], 0)
        if gus.base <= port < gus.base + 0x10 or gus.base + 0x100 <= port < gus.base + 0x108:
            return gus.inp(port, size)
        return 0xff if size == 1 else 0xffff

    def vga_reg(kind, val):
        st['vga_log'] += 1
        if st['vga_log'] < 200:
            print('VGA %s write %s (frame %d)' % (kind, val, st['frame']), flush=True)

    def on_out(mu, port, size, val, _):
        outs[port] = outs.get(port, 0) + 1
        if port == 0x43:
            if val & 0xc0 == 0:
                if val & 0x30 == 0:
                    st['latch'] = pit_counter()
                    st['latch_n'] = 0
                else:
                    st['pit_w'] = 0
                    m = (val >> 1) & 7
                    st['pit_mode'] = m - 4 if m > 5 else m
        elif port == 0x40:
            if st['pit_w'] == 0:
                st['pit_lo'] = val & 0xff
                st['pit_w'] = 1
            else:
                c = st['pit_lo'] | (val & 0xff) << 8
                st['pit_count'] = c or 0x10000
                st['pit_start'] = st['now']
                st['next_pit'] = st['now'] + st['pit_count']
                st['pit_w'] = 0
                if st['frame'] < 20: print('PIT mode %d count %#x (%.3f Hz) at frame %d' % (st['pit_mode'], st['pit_count'],
                                                                     PIT_HZ / st['pit_count'], st['frame']),
                      flush=True)
        elif port == 0x3c8:
            st['dac_w'] = val & 0xff
            st['dac_c'] = 0
        elif port == 0x3c7:
            st['dac_r'] = val & 0xff
            st['dac_c'] = 0
        elif port == 0x3c9:
            st['dac'][st['dac_w'] * 3 + st['dac_c']] = val & 0x3f
            st['dac_c'] += 1
            if st['dac_c'] == 3:
                st['dac_c'] = 0
                st['dac_w'] = (st['dac_w'] + 1) & 0xff
        elif port in (0x20, 0x21):
            pic1.out(port, val & 0xff)
        elif port in (0xa0, 0xa1):
            pic2.out(port, val & 0xff)
        elif port in (0x3c4, 0x3c5, 0x3d4, 0x3d5, 0x3ce, 0x3cf):
            kind = {0x3c4: 'seq', 0x3d4: 'crtc', 0x3ce: 'gc'}[port & ~1]
            if size == 2:
                st[kind + '_i'] = val & 0xff
                st[kind][val & 0xff] = val >> 8 & 0xff
                vga_reg(kind, '%02x=%02x' % (val & 0xff, val >> 8 & 0xff))
            elif port & 1 == 0:
                st[kind + '_i'] = val & 0xff
            else:
                st[kind][st[kind + '_i']] = val & 0xff
                vga_reg(kind, '%02x=%02x' % (st[kind + '_i'], val & 0xff))
        elif port < 0x10 or 0xc0 <= port < 0xe0:
            dma_out(port, val & 0xff)
        elif port in DMA_PAGE:
            dma['page'][DMA_PAGE[port]] = val & 0xff
        elif gus.base <= port < gus.base + 0x10 or gus.base + 0x100 <= port < gus.base + 0x108:
            old = gus.t_start
            gus.out(port, size, val)
            for i in (1, 2):
                bit = 1 << (i - 1)
                if gus.t_start & bit and not old & bit:
                    st['next_t%d' % i] = st['now'] + gus.period(i)
                elif not gus.t_start & bit:
                    st['next_t%d' % i] = None

    gus.dma = dma_read
    gus.raise_irq = lambda: st['pending'].add(7)

    traced = set()

    def on_trace(mu, addr, size, _):
        if addr not in traced:
            traced.add(addr)
            print('trace %x frame %d time %.3f' % (addr, st['frame'], st['now'] / PIT_HZ), flush=True)

    mu.hook_add(UC_HOOK_CODE, on_code_xms, None, XMS_ENTRY, XMS_ENTRY)

    def set_irq_stacks(mu, addr, size, _):
        # The extender keeps four 512-byte interrupt stacks at 0x140c8..0x148c8, right below the stack the demo
        # runs on (from about 0x148c8 down). The 3D engine's recursive face sort (0x21e6ec) can go deeper than
        # 0x800 bytes; an interrupt during such a sort then overwrites the sort's return addresses, and the demo
        # crashes (on a real PC as here: it depends on when the interrupt comes). The stacks are moved out of the
        # way, which changes nothing else the demo computes.
        if pm():
            for i in range(4):
                mu.mem_write(K_STACK_SLOTS + 4 * i, struct.pack('<I', IRQ_STACKS + 0x1000 * (i + 1)))
            print('interrupt stacks moved to %#x..%#x' % (IRQ_STACKS, IRQ_STACKS + 0x4000), flush=True)
    mu.hook_add(UC_HOOK_CODE, set_irq_stacks, None, K_SLOTS_SET, K_SLOTS_SET)

    def on_sort(mu, addr, size, _):
        e = R(UC_X86_REG_ESP)
        if e < st.get('sort_min', 0x14000 + 0x800):
            st['sort_min'] = e
            print('face sort stack down to %#x (frame %d)' % (e, st['frame']), flush=True)
    mu.hook_add(UC_HOOK_CODE, on_sort, None, SORT, SORT)

    def on_main(mu, addr, size, _):
        if addr == MAIN_FRAME:
            st['drawn'] += 1
            return
        n = (R(UC_X86_REG_ESI) - SCRIPT) // 0x14
        o, r = mu.mem_read(MUS_ORDER, 1)[0], mu.mem_read(MUS_ROW, 1)[0]
        if addr == MAIN_ENTRY:
            print('entry %d: init at frame %d (time %.4f s, music %d:%02d, drawn %d)' % (
                n, st['frame'], st['now'] / PIT_HZ, o, r, st['drawn']), flush=True)
        else:
            st['entry'] = n
            print('entry %d: retrace routine set at frame %d (time %.4f s, music %d:%02d)' % (
                n, st['frame'], st['now'] / PIT_HZ, o, r), flush=True)
    for x in (MAIN_ENTRY, MAIN_SET, MAIN_FRAME):
        mu.hook_add(UC_HOOK_CODE, on_main, None, x, x)
    hist = []
    st['hist'] = hist

    last = [0, 0]

    def on_hist(mu, addr, size, _):
        if addr != last[0] + last[1]:
            hist.append((last[0], addr, R(UC_X86_REG_CS), R(UC_X86_REG_EIP), R(UC_X86_REG_ESP)))
        last[0], last[1] = addr, size
        if len(hist) > 2 * a.history:
            del hist[:a.history]
    if a.history:
        mu.hook_add(UC_HOOK_CODE, on_hist)
    for x in filter(None, a.trace.split(',')):
        mu.hook_add(UC_HOOK_CODE, on_trace, None, int(x, 16), int(x, 16))
    mu.hook_add(UC_HOOK_INSN, on_in, None, 1, 0, UC_X86_INS_IN)
    mu.hook_add(UC_HOOK_INSN, on_out, None, 1, 0, UC_X86_INS_OUT)
    mu.hook_add(UC_HOOK_INTR, on_intr)

    dumped = {}

    def on_dump(mu, addr, size, _):
        if st['frame'] < a.dump_from or dumped.get(addr, 0) >= a.dump_max:
            return
        k = dumped.get(addr, 0)
        dumped[addr] = k + 1
        import zlib
        os.makedirs(a.dumpdir, exist_ok=True)
        path = os.path.join(a.dumpdir, 'd%06d_%x_%d.dump' % (st['frame'], addr, k))
        regs32 = {n: R(r) for n, r in REGS32}
        pickle.dump({'frame': st['frame'], 'addr': addr, 'k': k, 'regs': regs32, 'drawn': st['drawn'],
                     'mem': zlib.compress(bytes(mu.mem_read(0, XMS_END)), 1)}, open(path, 'wb'))
    for x in filter(None, a.dumps.split(',')):
        mu.hook_add(UC_HOOK_CODE, on_dump, None, int(x, 16), int(x, 16))

    if a.load:
        snap = pickle.load(open(a.snap, 'rb'))
        mu.mem_write(0, snap['mem'])
        if 'irqstacks' in snap:
            mu.mem_write(IRQ_STACKS, snap['irqstacks'])
        mu.context_restore(snap['ctx'])
        st.update(snap['st'])
        st['hist'] = hist
        gus.__dict__.update(snap['gus'])
        gus.log = glog
        gus.dma = dma_read
        gus.raise_irq = lambda: st['pending'].add(7)
        dma.update(snap['dma'])
        pic1.__dict__.update(snap['pic1'])
        pic2.__dict__.update(snap['pic2'])
        pc = snap['pc']
        print('resumed at frame %d' % st['frame'])

    frames = None if a.no_frames else open(a.out + '.frames', 'wb')
    if frames is not None:
        frames.write(b'REVEFRM1' + struct.pack('<IIII', 320, 200, 64768, 24))
    ticks = open(a.out + '.ticks', 'w')
    ticks.write('# frame time_s pit_irqs gus_irqs order row main_pc entry drawn\n')
    snap_frames = {int(x) for x in a.snap_at.split(',') if x}

    def save_snap(name):
        os.makedirs(a.snapdir, exist_ok=True)
        g = {k: v for k, v in gus.__dict__.items() if k not in ('log', 'dma', 'raise_irq')}
        path = os.path.join(a.snapdir, name)
        pickle.dump({'mem': bytes(mu.mem_read(0, XMS_END)), 'irqstacks': bytes(mu.mem_read(IRQ_STACKS, 0x4000)),
                     'ctx': mu.context_save(), 'st': {k: v for k, v in st.items() if k != 'hist'}, 'gus': g,
                     'dma': dma, 'pic1': dict(pic1.__dict__), 'pic2': dict(pic2.__dict__), 'pc': pc},
                    open(path, 'wb'))
        print('snapshot %s at frame %d' % (path, st['frame']), flush=True)

    def cur_pc():
        if not pm():
            return (R(UC_X86_REG_CS) << 4) + R(UC_X86_REG_IP)
        return seg_base(UC_X86_REG_CS) + R(UC_X86_REG_EIP)

    def irq_ok(irq):
        if irq < 8:
            if pic1.mask >> irq & 1:
                return False
            return not (pic1.isr & ((2 << irq) - 1))
        if pic2.mask >> (irq - 8) & 1 or pic1.mask & 4:
            return False
        return not (pic2.isr & ((2 << (irq - 8)) - 1)) and not (pic1.isr & 7)

    t_host = htime.time()
    q0 = a.quantum or int(a.ips / 70 / 32)
    was_pm = False
    while not st['stop'] and st['frame'] < a.frames:
        evs = [('vs', st['next_vs'])]
        if st['next_pit'] is not None:
            evs.append(('pit', st['next_pit']))
        for i in (1, 2):
            if st['next_t%d' % i] is not None:
                evs.append(('t%d' % i, st['next_t%d' % i]))
        name, t_ev = min(evs, key=lambda e: e[1])
        if st['now'] >= t_ev - 1e-6:
            if name == 'vs':
                f = st['frame']
                if f in snap_frames:
                    save_snap('f%d.snap' % f)
                order, row = mu.mem_read(MUS_ORDER, 1)[0], mu.mem_read(MUS_ROW, 1)[0]
                ticks.write('%d %.5f %d %d %d %d %x %d %d\n' % (f, st['now'] / PIT_HZ, st['pit_irqs'], st['gus_irqs'],
                                                                order, row, st['main_pc'], st['entry'], st['drawn']))
                if frames is not None:
                    frames.write(bytes(mu.mem_read(0xa0000, 64000)) if st['mode'] == 0x13 else bytes(64000))
                    frames.write(bytes(st['dac']))
                st['frame'] += 1
                st['next_vs'] += FRAME
                if f % 700 == 0:
                    print('frame %d time %.2f pit %d gus %d pc %x (host %.0f s)' % (
                        f, st['now'] / PIT_HZ, st['pit_irqs'], st['gus_irqs'], pc, htime.time() - t_host),
                        flush=True)
            elif name == 'pit':
                if st['pit_mode'] == 0:
                    st['next_pit'] = None
                else:
                    st['pit_start'] = st['next_pit']
                    st['next_pit'] += st['pit_count']
                st['pit_irqs'] += 1
                st['pending'].add(0)
            else:
                i = int(name[1])
                st['next_t%d' % i] += gus.period(i)
                if gus.t_ctrl & (2 << i):
                    gus.irq_status |= 2 << i
                    st['gus_irqs'] += 1
                    gus.tick = st['gus_irqs']
                    st['pending'].add(7)
            continue
        if pm() != was_pm:
            was_pm = pm()
            print('%s mode at frame %d, pc %x' % ('protected' if was_pm else 'real', st['frame'], cur_pc()),
                  flush=True)
        if st['pending'] and R(UC_X86_REG_EFLAGS) & 0x200:
            for irq in sorted(st['pending']):
                if irq_ok(irq):
                    st['pending'].discard(irq)
                    if irq < 8:
                        pic1.isr |= 1 << irq
                        vec = pic1.vec + irq
                    else:
                        pic2.isr |= 1 << (irq - 8)
                        pic1.isr |= 4
                        vec = pic2.vec + irq - 8
                    try:
                        pc = dispatch(vec, R(UC_X86_REG_EIP) if pm() else R(UC_X86_REG_IP))
                    except Exception as e:
                        print('IRQ %d delivery failed: %s; %s' % (irq, e, regs()), flush=True)
                        st['stop'] = True
                    break
        if st['stop']:
            break
        n = int(min(q0, (t_ev - st['now']) * a.ips / PIT_HZ + 1))
        st['skip'] = False
        pc = cur_pc()
        try:
            mu.emu_start(R(UC_X86_REG_EIP), 0xffffffffffff, count=n)
        except UcError as e:
            print('error at %x: %s; %s cr0=%x' % (cur_pc(), e, regs(), R(UC_X86_REG_CR0)), flush=True)
            for h in hist[-a.history:]:
                print('  %x -> %x  %04x:%08x esp=%x' % h)
            open(a.out + '.mem', 'wb').write(bytes(mu.mem_read(0, XMS_END)))
            break
        pc = cur_pc()
        if not pic1.isr:
            st['main_pc'] = pc
        if not st['skip']:
            st['now'] += n * PIT_HZ / a.ips
    open(a.out + '.dram', 'wb').write(bytes(gus.dram))
    glog.close()
    ticks.close()
    if frames is not None:
        frames.close()
    print('ended at frame %d, time %.2f s, PIT %d, GUS timer %d' % (st['frame'], st['now'] / PIT_HZ,
                                                                    st['pit_irqs'], st['gus_irqs']))
    print('out ports', {hex(k): v for k, v in sorted(outs.items())})
    print('in ports', {hex(k): v for k, v in sorted(ins.items())})
    print('services', dict(sorted(services.items())))
    print('GUS register reads', {hex(k): v for k, v in sorted(gus.reads.items())})
    print('GUS DMA transfers %d, %d bytes' % (gus.dma_count, gus.dma_bytes))
    print('text output:', repr(st['text']))


if __name__ == '__main__':
    main()

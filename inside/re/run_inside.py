"""Run INSIDE's 32-bit program headless in unicorn and record what it does.

No DOS and no PMODE/W: the program image (unpacked/inside_img.bin from unpack.py: object 1 at 0x400000, object 2 at
0x420000) is loaded with flat selectors (CS 0x08, DS = SS = ES = FS = GS 0x10, base 0), ESP = 0x4ba6a0, and started
at its entry 0x40aa48 (Watcom's start-up). PMODE/W's services are answered where the program calls them (int 21h,
int 31h, int 10h, int 16h; see the handlers below; anything unknown is printed and stops the run). Files are read
from ../original (INSIDE.DAT, EYE.AMF, SOUND.CFG and the DSMI driver SDIGUS.DSD it names): the program loads the
driver itself and calls into it, so the music runs through DSMI's own code.

Faked hardware:
  VGA      mode 13h at 0xa0000, the DAC (0x3c7 / 0x3c8 / 0x3c9) and the input status register 0x3da. The screen
           refreshes at 70.086 Hz (25.175 MHz / (800 x 449)); bit 3 (vertical retrace) is set for 2 of the 449 lines,
           bit 0 (display disabled) during the 49 blanked lines. A loop that reads 0x3da at the same place and gets the
           same value again is spinning: time jumps to the next change of the register (the rest of the slice of
           instructions is dropped), so retrace waits cost no emulation time.
  PIT      channel 0 in modes 0 (one shot: the demo's retrace timer reprograms it in every interrupt) and 2 / 3
           (periodic; mode 3 counts down by 2), latch reads. IRQ0 runs the handler installed with int 21h AH=25h.
  GUS      at the port SOUND.CFG names (0x220), the register file with 1 MB DRAM, DMA uploads through the 8237
           and the DMA-complete IRQ, timers (unused). (Gus class: stars/re/run_stars.py.)

The demo's own timer interrupt fires 4 times per screen refresh (280 Hz), locked to the retrace (NOTES.md "Clock").

Time: the PIT clock (1,193,182 Hz). Main-line code runs at --ips instructions per second in slices of --quantum
instructions; port reads see the time at the start of the slice. Interrupt handlers run instantly, except that a
retrace wait inside a handler lets time pass.

Outputs (prefix --out):
  <out>.frames  header b'INSDFRM1' + <IIII: width 320, height 200, record bytes 64768, header bytes 24; then per
                screen refresh, at the start of its first displayed line (the timer interrupt copies a finished frame
                just after the retrace before it), the 64,000 bytes of VGA memory and the 768 bytes of the DAC (6 bits
                per entry). Record k is the picture shown from time k / 70.086 s on.
  <out>.ticks   per screen refresh: frame, time (s), music order, row, demo ticks [0x424880] (float, 280 Hz), part
                ticks [0x4248c8], script index [0x4248bc], frame routine [0x424888], tick routine [0x4248b4],
                frames shown [0x42488c], timer interrupts so far, main EIP
  <out>.flips   per frame copied to the screen by the timer interrupt (0x400694): number, frame, time, order, row,
                demo ticks, part ticks, frame routine
  <out>.gus     GUS register writes: irq v<voice> r<reg> value / irq g r<reg> value / irq p<port> value, stamped
                with the number of timer interrupts so far (the player runs in the timer interrupt)
  <out>.dram    final GUS DRAM (1 MB)
  stdout        text output, service calls, script events (redirect it to <out>.log)

usage: python3 run_inside.py --out full [--ips 150e6] [--frames N] [--snap-parts]
       python3 run_inside.py --load --snap snaps/f1234.snap --out x --frames 2000
"""
import argparse
import os
import pickle
import struct
import sys

import numpy as np
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_INTR, UC_HOOK_CODE, UC_HOOK_INSN, UcError
from unicorn.x86_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
while not os.path.exists(os.path.join(HERE, 'unpacked', 'inside_img.bin')) and os.path.dirname(HERE) != HERE:
    HERE = os.path.dirname(HERE)              # a copy in models/<scope>/ finds inside/re
IMG = open(os.path.join(HERE, 'unpacked', 'inside_img.bin'), 'rb').read()
ORIG = os.path.join(os.path.dirname(HERE), 'original')
BASE = 0x400000
ENTRY = 0x40aa48
ESP0 = 0x4ba6a0

PIT_HZ = 1193182
VGA_HZ = 25175000 / (800 * 449)
FRAME = PIT_HZ / VGA_HZ          # PIT clocks per screen refresh (17024.6)
LINE = FRAME / 449
RAM = 0x4000000                  # 64 MB
IRET = 0x7f000                   # default interrupt handlers: iretd
EXIT = 0x7f010                   # return address of the entry: int 21h 4C00
SENTINEL = 0x7fff0               # return address for IRQ delivery
GDT = 0x7e000
PSP = 0x80000
ENV = 0x80100
DOSMEM = 0x81000                 # DOS memory blocks (int 31h 0100) up to 0x9f000
HEAP = 0x1000000                 # extended memory blocks
SEL_CODE, SEL_DATA, SEL_PSP, SEL_ENV = 0x08, 0x10, 0x18, 0x20
W, H = 320, 200

# demo variables (NOTES.md)
V_TICKS = 0x424880               # float, +1 per timer callback (280 Hz)
V_PART_TICKS = 0x4248c8          # int, +1 per timer callback, reset when a part sets its routines
V_SCRIPT = 0x4248bc              # index into the script table 0x4222e4
V_FRAME_FN = 0x424888            # routine main calls per drawn frame
V_TICK_FN = 0x4248b4             # routine the timer callback calls
V_SHOWN = 0x42488c               # float, +1 per frame copied to the screen
COPY = 0x400694                  # timer callback: copy the finished buffer to VGA
MUS_ORDER, MUS_ROW, MUS_FLAGS = 0x422a5d, 0x422a61, 0x423399


class Gus:
    """Gravis UltraSound register file (stars/re/run_stars.py), with DMA uploads and the DMA IRQ."""
    def __init__(self, log, base=0x220):
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
        self.irq_status = 0
        self.dma_pending = False
        self.dma = None
        self.raise_irq = None
        self.dma_count = 0
        self.dma_bytes = 0
        self.mix = 0

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
        else:
            if p == 0x000:
                self.mix = val
            self.log.write('%d p%03x %x\n' % (self.tick, port, val))

    def _write(self, reg, val, size):
        if reg == 0x43:
            self.addr = (self.addr & 0xff0000) | (val & 0xffff)
        elif reg == 0x44:
            self.addr = (self.addr & 0xffff) | ((val & 0xff) << 16)
        elif reg == 0x41:
            self.glob[reg] = val
            self.log.write('%d g r41 %x\n' % (self.tick, val))
            if val & 1 and self.dma is not None:
                r42 = self.glob.get(0x42, 0)
                ch = self.glob.get('ch', 1)
                if val & 4:
                    dst = ((r42 & 0xc000) << 4) | ((r42 & 0x1fff) << 5)
                else:
                    dst = r42 << 4
                data = bytearray(self.dma(ch))
                if val & 0x40:
                    for i in range(1 if val & 0x80 else 0, len(data), 2 if val & 0x80 else 1):
                        data[i] ^= 0x80
                for i, b in enumerate(data):
                    self.dram[(dst + i) & 0xfffff] = b
                self.dma_count += 1
                self.dma_bytes += len(data)
                self.dma_pending = True
                self.irq_status |= 0x80
                if val & 0x20 and self.raise_irq:
                    self.raise_irq()
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
                if r == 0x80: v |= 1        # voice stopped
                if r == 0x8d: v |= 1        # volume ramp stopped
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
            if r in (0x49, 0x45, 0x4c):
                return self.glob.get(r & 0x7f, 0) & 0xff
            return 0
        if p == 0x006:
            return self.irq_status
        if p == 0x008:
            return 0
        if p == 0x103:
            return self.reg
        if p == 0x102:
            return self.voice
        return 0xff


def desc(base, limit, access, flags=0xc):
    return struct.pack('<HHBBBB', limit & 0xffff, base & 0xffff, (base >> 16) & 0xff, access,
                       (flags << 4) | ((limit >> 16) & 0xf), (base >> 24) & 0xff)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--frames', type=int, default=1 << 30, help='stop at this screen refresh')
    ap.add_argument('--ips', type=float, default=150e6, help='main-line instructions per second')
    ap.add_argument('--quantum', type=int, default=0, help='instructions per slice (default ips/20000)')
    ap.add_argument('--snap-at', default='', help='save snapshots at these frames (comma-separated)')
    ap.add_argument('--snap-parts', action='store_true', help='save a snapshot at the first refresh after the frame '
                    'routine changes')
    ap.add_argument('--snapdir', default=os.path.join(HERE, 'snaps'))
    ap.add_argument('--snap', help='snapshot to resume from (with --load)')
    ap.add_argument('--load', action='store_true')
    ap.add_argument('--trace', default='', help='comma-separated addresses: print when each first runs')
    ap.add_argument('--no-frames', action='store_true')
    a = ap.parse_args()

    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(0, RAM)
    mu.mem_write(BASE, IMG)
    psp = bytearray(256)
    struct.pack_into('<H', psp, 0x2c, SEL_ENV)
    struct.pack_into('<H', psp, 0x02, 0x9fff)
    psp[0x80] = 0
    psp[0x81] = 0x0d
    mu.mem_write(PSP, bytes(psp))
    mu.mem_write(ENV, b'PATH=C:\\\x00COMSPEC=C:\\COMMAND.COM\x00\x00\x01\x00C:\\INSIDE\\INSIDE.EXE\x00')
    mu.mem_write(IRET, b'\xcf')
    mu.mem_write(EXIT, b'\x66\xb8\x00\x4c\xcd\x21')
    gdt = bytearray(256 * 8)
    gdt[0x08:0x10] = desc(0, 0xfffff, 0x9a)
    gdt[0x10:0x18] = desc(0, 0xfffff, 0x92)
    gdt[0x18:0x20] = desc(PSP, 0xff, 0x92, 4)
    gdt[0x20:0x28] = desc(ENV, 0xffff, 0x92, 4)
    mu.mem_write(GDT, bytes(gdt))
    mu.reg_write(UC_X86_REG_GDTR, (0, GDT, len(gdt) - 1, 0))
    mu.reg_write(UC_X86_REG_CS, SEL_CODE)
    for r in (UC_X86_REG_DS, UC_X86_REG_SS, UC_X86_REG_FS, UC_X86_REG_GS):
        mu.reg_write(r, SEL_DATA)
    mu.reg_write(UC_X86_REG_ES, SEL_PSP)
    mu.mem_write(ESP0 - 4, struct.pack('<I', EXIT))
    mu.reg_write(UC_X86_REG_ESP, ESP0 - 4)

    glog = open(a.out + '.gus', 'w')
    flog = open(a.out + '.flips', 'w')
    flog.write('# copy frame time_s order row ticks part_ticks frame_fn\n')
    gus = Gus(glog)
    st = {'frame': 0, 'now': 0.0, 'pit_mode': 3, 'pit_count': 0x10000, 'pit_start': 0.0, 'pit_armed': False,
          'next_irq': None, 'pit_w': 0, 'pit_lo': 0, 'latch': None, 'latch_n': 0, 'vec': {}, 'dos': DOSMEM,
          'heap': HEAP, 'sels': {}, 'nsel': 0x28, 'gfx': False, 'stop': False, 'text': '', 'files': {},
          'nextfh': 5, 'dac_w': 0, 'dac_r': 0, 'dac_c': 0, 'dac': bytearray(768), 'spin': None,
          'in_irq': False, 'skip': False, 'copies': 0, 'snapped_fn': None, 'pic_mask': 0xb8, 'pic2_mask': 0xff,
          'blocks': {}, 'irqs': 0}
    outs = {}
    ins = {}
    services = {}

    def reg(r): return mu.reg_read(r)

    def setflag(bit, c):
        f = mu.reg_read(UC_X86_REG_EFLAGS)
        mu.reg_write(UC_X86_REG_EFLAGS, (f | bit) if c else (f & ~bit))

    def setcf(c): setflag(1, c)

    def count(name):
        services[name] = services.get(name, 0) + 1

    def rd32(addr): return struct.unpack('<I', mu.mem_read(addr, 4))[0]

    def rdf(addr): return struct.unpack('<f', mu.mem_read(addr, 4))[0]

    def cstr(addr, end=b'\0', n=400):
        return bytes(mu.mem_read(addr, n)).split(end)[0].decode('latin1')

    def text(s):
        st['text'] += s
        while '\n' in st['text'] or '\r' in st['text']:
            i = min(x for x in (st['text'].find('\n'), st['text'].find('\r')) if x >= 0)
            line, st['text'] = st['text'][:i], st['text'][i + 1:]
            if line.strip():
                print('text: %s' % line.rstrip()[-200:], flush=True)

    def sel_base(sel):
        sel &= 0xfff8
        if sel in (SEL_CODE, SEL_DATA):
            return 0
        if sel == SEL_PSP:
            return PSP
        if sel == SEL_ENV:
            return ENV
        return st['sels'].get(sel, (0, 0))[0]

    def new_sel(base, limit):
        sel = st['nsel']
        st['nsel'] += 8
        st['sels'][sel] = (base, limit)
        mu.mem_write(GDT + sel, desc(base, min(limit, 0xfffff), 0x92, 4 if limit < 0x100000 else 0xc))
        return sel

    # ---- time -----------------------------------------------------------------------------------------------
    def pit_counter():
        el = st['now'] - st['pit_start']
        c = st['pit_count']
        if st['pit_mode'] == 3:
            return int(c - 2 * el) % c & 0xffff if c else 0
        if st['pit_mode'] == 2:
            return int(c - el) % c & 0xffff
        return int(c - el) & 0xffff               # mode 0: counts on through 0

    def vga_status(t):
        ph = t % FRAME
        line = ph / LINE                          # line 0 = first displayed line
        v = 0
        if line >= 400:
            v |= 1                                # blanking (approximately)
        if 412 <= line < 414:
            v |= 8
        return v

    def vga_next_change(t):
        ph = t % FRAME
        base = t - ph
        for edge in (400 * LINE, 412 * LINE, 414 * LINE, FRAME):
            if ph < edge - 1e-6:
                return base + edge + 1e-4            # just inside the new state
        return base + FRAME + 1e-4

    # ---- DOS / PMODE/W services ----------------------------------------------------------------------------
    def open_file(name, mode):
        n = name.replace('\\', '/').split('/')[-1]
        path = None
        for f in os.listdir(ORIG):
            if f.lower() == n.lower():
                path = os.path.join(ORIG, f)
        if path is None:
            return None
        h = st['nextfh']
        st['nextfh'] += 1
        st['files'][h] = [path, 0]
        return h

    def int21():
        eax = reg(UC_X86_REG_EAX)
        ah = (eax >> 8) & 0xff
        ax = eax & 0xffff
        edx = reg(UC_X86_REG_EDX)
        ebx = reg(UC_X86_REG_EBX)
        ecx = reg(UC_X86_REG_ECX)
        count('int21/%02x' % ah)
        ds = sel_base(reg(UC_X86_REG_DS))
        if ah == 0x09:
            text(cstr(ds + edx, b'$'))
            setcf(0)
        elif ah == 0x02:
            text(chr(edx & 0xff))
        elif ah == 0x4c:
            print('exit code %d at frame %d (time %.2f s)' % (eax & 0xff, st['frame'], st['now'] / PIT_HZ),
                  flush=True)
            st['stop'] = True
            mu.emu_stop()
        elif ah == 0x30:
            mu.reg_write(UC_X86_REG_EAX, (eax & ~0xffff) | 0x0006)       # DOS 6.0
            mu.reg_write(UC_X86_REG_EBX, ebx & ~0xffff)
            setcf(0)
        elif ah == 0x35:
            n = eax & 0xff
            mu.reg_write(UC_X86_REG_ES, SEL_CODE)
            mu.reg_write(UC_X86_REG_EBX, st['vec'].get(n, IRET))
            setcf(0)
        elif ah == 0x25:
            n = eax & 0xff
            st['vec'][n] = edx
            print('set PM vector %#x -> %x (frame %d, time %.3f s)' % (n, edx, st['frame'], st['now'] / PIT_HZ),
                  flush=True)
            setcf(0)
        elif ah == 0x4a:
            setcf(0)
        elif ax == 0xff00:
            mu.reg_write(UC_X86_REG_EAX, eax | 0xff)                     # PMODE/W answers like DOS/4G (GS = 0)
            mu.reg_write(UC_X86_REG_GS, 0)
        elif ah == 0x48:
            if eax & 0x80000000:                  # extended: EBX bytes -> EAX linear
                size = ebx
            else:
                size = (ebx & 0xffff) * 16
            lin = st['heap']
            st['heap'] = (st['heap'] + size + 0xfff) & ~0xfff
            if st['heap'] > RAM:
                setcf(1)
                mu.reg_write(UC_X86_REG_EAX, 8)
                print('int 21h 48h: out of memory (%d bytes)' % size, flush=True)
                return
            st['blocks'][lin] = size
            if eax & 0x80000000:
                mu.reg_write(UC_X86_REG_EAX, lin)
            else:
                sel = new_sel(lin, size - 1)
                mu.reg_write(UC_X86_REG_EAX, sel)
            print('int 21h 48h eax=%x: %d bytes at %#x' % (eax, size, lin), flush=True)
            setcf(0)
        elif ah == 0x49:
            setcf(0)
        elif ah == 0x3d or ah == 0x3c:
            name = cstr(ds + edx)
            h = open_file(name, eax & 0xff) if ah == 0x3d else None
            print('open %r -> %s' % (name, h), flush=True)
            if h is None:
                setcf(1)
                mu.reg_write(UC_X86_REG_EAX, 2)
            else:
                mu.reg_write(UC_X86_REG_EAX, h)
                setcf(0)
        elif ah == 0x3e:
            st['files'].pop(ebx & 0xffff, None)
            setcf(0)
        elif ah == 0x3f:
            f = st['files'].get(ebx & 0xffff)
            if f is None:
                setcf(1)
                mu.reg_write(UC_X86_REG_EAX, 6)
                return
            data = open(f[0], 'rb').read()[f[1]:f[1] + ecx]
            f[1] += len(data)
            mu.mem_write(ds + edx, data)
            mu.reg_write(UC_X86_REG_EAX, len(data))
            setcf(0)
        elif ah == 0x40:
            h = ebx & 0xffff
            data = bytes(mu.mem_read(ds + edx, ecx))
            if h in (1, 2):
                text(data.decode('latin1'))
            mu.reg_write(UC_X86_REG_EAX, ecx)
            setcf(0)
        elif ah == 0x42:
            f = st['files'].get(ebx & 0xffff)
            if f is None:
                setcf(1)
                return
            off = ((ecx & 0xffff) << 16) | (edx & 0xffff)
            if off >= 0x80000000:
                off -= 1 << 32
            size = os.path.getsize(f[0])
            f[1] = [off, f[1] + off, size + off][eax & 0xff]
            mu.reg_write(UC_X86_REG_EAX, (eax & ~0xffff) | (f[1] & 0xffff))
            mu.reg_write(UC_X86_REG_EDX, (edx & ~0xffff) | ((f[1] >> 16) & 0xffff))
            setcf(0)
        elif ah == 0x44:
            al = eax & 0xff
            if al == 0:
                h = ebx & 0xffff
                mu.reg_write(UC_X86_REG_EDX, (edx & ~0xffff) | (0x80d3 if h < 5 else 0x0002))
            setcf(0)
        elif ah == 0x19:
            mu.reg_write(UC_X86_REG_EAX, (eax & ~0xff) | 2)
        elif ah == 0x47:
            mu.mem_write(sel_base(reg(UC_X86_REG_DS)) + reg(UC_X86_REG_ESI), b'INSIDE\x00')
            setcf(0)
        elif ah == 0x2c:
            mu.reg_write(UC_X86_REG_ECX, ecx & ~0xffff)
            mu.reg_write(UC_X86_REG_EDX, edx & ~0xffff)
        elif ah == 0x2a:
            mu.reg_write(UC_X86_REG_ECX, (ecx & ~0xffff) | 1996)
            mu.reg_write(UC_X86_REG_EDX, (edx & ~0xffff) | 0x0406)
        else:
            print('int 21h ax=%04x unhandled at %x' % (ax, reg(UC_X86_REG_EIP)), flush=True)
            setcf(1)

    RMS = (('edi', 0, 4), ('esi', 4, 4), ('ebp', 8, 4), ('ebx', 0x10, 4), ('edx', 0x14, 4), ('ecx', 0x18, 4),
           ('eax', 0x1c, 4), ('flags', 0x20, 2), ('es', 0x22, 2), ('ds', 0x24, 2))

    def int31():
        eax = reg(UC_X86_REG_EAX)
        ax = eax & 0xffff
        ebx = reg(UC_X86_REG_EBX)
        count('int31/%04x' % ax)
        if ax == 0x0006:
            b = sel_base(ebx & 0xffff)
            mu.reg_write(UC_X86_REG_ECX, (reg(UC_X86_REG_ECX) & ~0xffff) | (b >> 16))
            mu.reg_write(UC_X86_REG_EDX, (reg(UC_X86_REG_EDX) & ~0xffff) | (b & 0xffff))
            setcf(0)
        elif ax == 0x0002:
            seg = ebx & 0xffff
            sel = new_sel(seg << 4, 0xffff)
            mu.reg_write(UC_X86_REG_EAX, (eax & ~0xffff) | sel)
            setcf(0)
        elif ax == 0x0000:
            sel = new_sel(0, 0)
            for _ in range(max(1, reg(UC_X86_REG_ECX) & 0xffff) - 1):
                new_sel(0, 0)
            mu.reg_write(UC_X86_REG_EAX, (eax & ~0xffff) | sel)
            setcf(0)
        elif ax == 0x0007:
            sel = ebx & 0xfff8
            b = (reg(UC_X86_REG_ECX) & 0xffff) << 16 | (reg(UC_X86_REG_EDX) & 0xffff)
            lim = st['sels'].get(sel, (0, 0))[1]
            st['sels'][sel] = (b, lim)
            mu.mem_write(GDT + sel, desc(b, min(lim, 0xfffff), 0x92, 0xc))
            setcf(0)
        elif ax == 0x0008:
            sel = ebx & 0xfff8
            lim = (reg(UC_X86_REG_ECX) & 0xffff) << 16 | (reg(UC_X86_REG_EDX) & 0xffff)
            b = st['sels'].get(sel, (0, 0))[0]
            st['sels'][sel] = (b, lim)
            mu.mem_write(GDT + sel, desc(b, lim >> 12 if lim > 0xfffff else lim, 0x92,
                                         0xc if lim > 0xfffff else 4))
            setcf(0)
        elif ax in (0x0009, 0x0001, 0x0600, 0x0601, 0x0101, 0x0702, 0x0703):
            setcf(0)
        elif ax == 0x0100:
            paras = ebx & 0xffff
            lin = st['dos']
            st['dos'] += (paras * 16 + 0xff) & ~0xff
            sel = new_sel(lin, paras * 16 - 1)
            mu.reg_write(UC_X86_REG_EAX, (eax & ~0xffff) | (lin >> 4))
            mu.reg_write(UC_X86_REG_EDX, (reg(UC_X86_REG_EDX) & ~0xffff) | sel)
            print('DPMI DOS memory %d paragraphs -> segment %#x selector %#x' % (paras, lin >> 4, sel), flush=True)
            setcf(0)
        elif ax == 0x0501:
            size = (ebx & 0xffff) << 16 | (reg(UC_X86_REG_ECX) & 0xffff)
            lin = st['heap']
            st['heap'] = (st['heap'] + size + 0xfff) & ~0xfff
            mu.reg_write(UC_X86_REG_EBX, (ebx & ~0xffff) | (lin >> 16))
            mu.reg_write(UC_X86_REG_ECX, (reg(UC_X86_REG_ECX) & ~0xffff) | (lin & 0xffff))
            mu.reg_write(UC_X86_REG_ESI, (reg(UC_X86_REG_ESI) & ~0xffff) | (lin >> 16))
            mu.reg_write(UC_X86_REG_EDI, (reg(UC_X86_REG_EDI) & ~0xffff) | (lin & 0xffff))
            print('DPMI 0501: %d bytes at %#x' % (size, lin), flush=True)
            setcf(0)
        elif ax == 0x0502:
            setcf(0)
        elif ax == 0x0500:
            buf = bytearray(48)
            free = RAM - st['heap']
            struct.pack_into('<III', buf, 0, free, free >> 12, free >> 12)
            for i in range(3, 12):
                struct.pack_into('<I', buf, i * 4, free >> 12 if i in (4, 5, 6, 8) else 0xffffffff)
            mu.mem_write(sel_base(reg(UC_X86_REG_ES)) + reg(UC_X86_REG_EDI), bytes(buf))
            setcf(0)
        elif ax == 0x0204:
            n = ebx & 0xff
            mu.reg_write(UC_X86_REG_ECX, (reg(UC_X86_REG_ECX) & ~0xffff) | SEL_CODE)
            mu.reg_write(UC_X86_REG_EDX, st['vec'].get(n, IRET))
            setcf(0)
        elif ax == 0x0205:
            n = ebx & 0xff
            st['vec'][n] = reg(UC_X86_REG_EDX)
            print('DPMI set PM vector %#x -> %x (frame %d)' % (n, reg(UC_X86_REG_EDX), st['frame']), flush=True)
            setcf(0)
        elif ax == 0x0200:
            mu.reg_write(UC_X86_REG_ECX, reg(UC_X86_REG_ECX) & ~0xffff)
            mu.reg_write(UC_X86_REG_EDX, reg(UC_X86_REG_EDX) & ~0xffff)
            setcf(0)
        elif ax == 0x0201:
            setcf(0)
        elif ax == 0x0300:
            edi = sel_base(reg(UC_X86_REG_ES)) + reg(UC_X86_REG_EDI)
            blk = bytes(mu.mem_read(edi, 0x32))
            r = {k: int.from_bytes(blk[o:o + n], 'little') for k, o, n in RMS}
            intno = ebx & 0xff
            print('real-mode int %x ax=%04x (simulated, ignored)' % (intno, r['eax'] & 0xffff), flush=True)
            setcf(0)
        else:
            print('int 31h ax=%04x unhandled at %x' % (ax, reg(UC_X86_REG_EIP)), flush=True)
            setcf(1)

    def int10():
        eax = reg(UC_X86_REG_EAX)
        ah = (eax >> 8) & 0xff
        count('int10/%02x' % ah)
        if ah == 0x00:
            m = eax & 0x7f
            st['gfx'] = (m == 0x13)
            print('int 10h set mode %#x (frame %d, time %.3f s)' % (m, st['frame'], st['now'] / PIT_HZ), flush=True)
            if m == 0x13:
                mu.mem_write(0xa0000, bytes(65536))
        elif ah == 0x0f:
            mu.reg_write(UC_X86_REG_EAX, (eax & ~0xffff) | (0x5000 | (0x13 if st['gfx'] else 3)))
        elif ah == 0x4f:
            mu.reg_write(UC_X86_REG_EAX, (eax & ~0xffff) | 0x0100)
        elif ah in (0x02, 0x03, 0x0e, 0x09, 0x01, 0x10, 0x11, 0x12, 0x1a):
            if ah == 0x0e:
                text(chr(eax & 0xff))
            if ah == 0x1a:
                mu.reg_write(UC_X86_REG_EAX, (eax & ~0xffff) | 0x1a)
                mu.reg_write(UC_X86_REG_EBX, (reg(UC_X86_REG_EBX) & ~0xffff) | 0x08)
        else:
            print('int 10h ax=%04x unhandled' % (eax & 0xffff), flush=True)

    def on_intr(mu, intno, _):
        if intno == 0x31:
            int31()
        elif intno == 0x21:
            int21()
        elif intno == 0x10:
            int10()
        elif intno == 0x16:
            ah = (reg(UC_X86_REG_EAX) >> 8) & 0xff
            count('int16/%02x' % ah)
            if ah in (0x01, 0x11):
                setflag(0x40, 1)
            else:
                mu.reg_write(UC_X86_REG_EAX, reg(UC_X86_REG_EAX) & ~0xffff)
        elif intno == 0x2f:
            count('int2f/%04x' % (reg(UC_X86_REG_EAX) & 0xffff))
        elif intno == 0x33:
            count('int33')
            mu.reg_write(UC_X86_REG_EAX, reg(UC_X86_REG_EAX) & ~0xffff)
        elif intno == 3:
            print('int 3 at %x' % reg(UC_X86_REG_EIP), flush=True)
        else:
            print('int %x at %x: stop' % (intno, reg(UC_X86_REG_EIP)), flush=True)
            print(' '.join('%s=%x' % (n, reg(r)) for n, r in (
                ('eax', UC_X86_REG_EAX), ('ebx', UC_X86_REG_EBX), ('ecx', UC_X86_REG_ECX), ('edx', UC_X86_REG_EDX),
                ('esi', UC_X86_REG_ESI), ('edi', UC_X86_REG_EDI), ('ebp', UC_X86_REG_EBP), ('esp', UC_X86_REG_ESP))),
                flush=True)
            st['stop'] = True
            mu.emu_stop()

    # ---- ports ----------------------------------------------------------------------------------------------
    DMA_PAGE = {0x87: 0, 0x83: 1, 0x81: 2, 0x82: 3, 0x8f: 4, 0x8b: 5, 0x89: 6, 0x8a: 7}
    dma = {'addr': [0] * 8, 'count': [0] * 8, 'page': [0] * 8, 'ff': [0, 0], 'mode': [0] * 8}

    def dma_out(port, val):
        if port < 0x10:
            c, ch0, sh = 0, 0, 0
            p = port
        else:
            c, ch0 = 1, 4
            p = (port - 0xc0) >> 1
        if p < 8:
            ch = ch0 + (p >> 1)
            key = 'addr' if p % 2 == 0 else 'count'
            if dma['ff'][c] == 0:
                dma[key][ch] = (dma[key][ch] & 0xff00) | val
            else:
                dma[key][ch] = (dma[key][ch] & 0x00ff) | (val << 8)
            dma['ff'][c] ^= 1
        elif p == 0xc:
            dma['ff'][c] = 0
        elif p == 0xb:
            dma['mode'][ch0 + (val & 3)] = val

    def dma_read(ch):
        n = dma['count'][ch] + 1
        if ch < 4:
            lin = dma['page'][ch] << 16 | dma['addr'][ch]
        else:
            lin = (dma['page'][ch] & 0xfe) << 16 | dma['addr'][ch] << 1
            n *= 2
        return bytes(mu.mem_read(lin, n))

    def on_in(mu, port, size, _):
        ins['%x' % port] = ins.get('%x' % port, 0) + 1
        if port == 0x3da:
            t = st['now']
            v = vga_status(t)
            eip = reg(UC_X86_REG_EIP)
            if st['spin'] == (eip, v):
                st['now'] = vga_next_change(t)
                v = vga_status(st['now'])
                if not st['in_irq']:
                    st['skip'] = True
                    mu.emu_stop()
            st['spin'] = (eip, v)
            return v
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
        if port == 0x21:
            return st['pic_mask']
        if port == 0xa1:
            return st['pic2_mask']
        if port == 0x60:
            return 0x01 | 0x80
        if port == 0x64:
            return 0x14
        if gus.base <= port < gus.base + 0x10 or gus.base + 0x100 <= port < gus.base + 0x108:
            return gus.inp(port, size)
        return 0xff if size == 1 else 0xffff

    def on_out(mu, port, size, val, _):
        outs[port] = outs.get(port, 0) + 1
        if port == 0x43:
            if val & 0xc0 == 0:
                if val & 0x30 == 0:
                    st['latch'] = pit_counter()
                    st['latch_n'] = 0
                else:
                    st['pit_w'] = 0
                    st['pit_mode'] = (val >> 1) & 7
                    if st['pit_mode'] > 5:
                        st['pit_mode'] -= 4
                    st['pit_armed'] = False
        elif port == 0x40:
            if st['pit_w'] == 0:
                st['pit_lo'] = val & 0xff
                st['pit_w'] = 1
            else:
                c = st['pit_lo'] | (val & 0xff) << 8
                st['pit_count'] = c or 0x10000
                st['pit_start'] = st['now']
                st['pit_armed'] = True
                st['next_irq'] = st['now'] + st['pit_count']
                st['pit_w'] = 0
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
        elif port == 0x21:
            st['pic_mask'] = val & 0xff
        elif port == 0xa1:
            st['pic2_mask'] = val & 0xff
        elif port < 0x10 or 0xc0 <= port < 0xe0:
            dma_out(port, val & 0xff)
        elif port in DMA_PAGE:
            dma['page'][DMA_PAGE[port]] = val & 0xff
        elif gus.base <= port < gus.base + 0x10 or gus.base + 0x100 <= port < gus.base + 0x108:
            gus.out(port, size, val)

    gus.dma = dma_read

    def irq_vector(irq):
        return irq + 8 if irq < 8 else irq + 0x68

    def run_handler(vec):
        """Run an interrupt handler to its iretd (retrace waits inside it let time pass)."""
        ctx = mu.context_save()
        esp = reg(UC_X86_REG_ESP) - 12
        mu.mem_write(esp, struct.pack('<III', SENTINEL, SEL_CODE, 0x202))
        mu.reg_write(UC_X86_REG_ESP, esp)
        st['in_irq'] = True
        pc = vec
        try:
            for _ in range(100):
                mu.emu_start(pc, SENTINEL, count=200_000_000)
                pc = reg(UC_X86_REG_EIP)
                if pc == SENTINEL or st['stop']:
                    break
        except UcError as e:
            print('error in irq handler at %x: %s' % (reg(UC_X86_REG_EIP), e), flush=True)
            st['stop'] = True
        st['in_irq'] = False
        mu.context_restore(ctx)

    def gus_irq():
        irq = 11
        v = st['vec'].get(irq_vector(irq))
        if v is not None and not st['in_irq']:
            run_handler(v)
        elif v is not None:
            st['gus_irq_pending'] = True
    gus.raise_irq = gus_irq

    def on_copy(mu, addr, size, _):
        st['copies'] += 1
        flog.write('%d %d %.5f %d %d %.1f %d %x\n' % (
            st['copies'], st['frame'], st['now'] / PIT_HZ, music()[0], music()[1], rdf(V_TICKS),
            rd32(V_PART_TICKS), rd32(V_FRAME_FN)))

    def music():
        if not mu.mem_read(MUS_FLAGS, 1)[0] & 1:
            return -1, -1
        return mu.mem_read(MUS_ORDER, 1)[0], struct.unpack('<H', mu.mem_read(MUS_ROW, 2))[0]

    traced = set()

    def on_trace(mu, addr, size, _):
        if addr not in traced:
            traced.add(addr)
            print('trace %x frame %d time %.3f' % (addr, st['frame'], st['now'] / PIT_HZ), flush=True)

    for x in filter(None, a.trace.split(',')):
        mu.hook_add(UC_HOOK_CODE, on_trace, None, int(x, 16), int(x, 16))
    mu.hook_add(UC_HOOK_CODE, on_copy, None, COPY, COPY)
    mu.hook_add(UC_HOOK_INSN, on_in, None, 1, 0, UC_X86_INS_IN)
    mu.hook_add(UC_HOOK_INSN, on_out, None, 1, 0, UC_X86_INS_OUT)
    mu.hook_add(UC_HOOK_INTR, on_intr)

    eip = ENTRY
    if a.load:
        snap = pickle.load(open(a.snap, 'rb'))
        mu.mem_write(0, snap['mem'])
        mu.context_restore(snap['ctx'])
        st.update(snap['st'])
        gus.__dict__.update(snap['gus'])
        gus.log = glog
        gus.dma = dma_read
        gus.raise_irq = gus_irq
        dma.update(snap['dma'])
        eip = snap['eip']
        print('resumed at frame %d' % st['frame'])

    frames = None if a.no_frames else open(a.out + '.frames', 'wb')
    if frames is not None:
        frames.write(b'INSDFRM1' + struct.pack('<IIII', W, H, 64768, 24))
    ticks = open(a.out + '.ticks', 'w')
    ticks.write('# frame time_s order row ticks part_ticks script frame_fn tick_fn shown irqs eip\n')
    snap_frames = {int(x) for x in a.snap_at.split(',') if x}

    def save_snap(name, eip):
        os.makedirs(a.snapdir, exist_ok=True)
        g = {k: v for k, v in gus.__dict__.items() if k not in ('log', 'dma', 'raise_irq')}
        path = os.path.join(a.snapdir, name)
        pickle.dump({'mem': bytes(mu.mem_read(0, RAM)), 'ctx': mu.context_save(), 'st': dict(st), 'gus': g,
                     'dma': dma, 'eip': eip}, open(path, 'wb'))
        print('snapshot %s at frame %d' % (path, st['frame']), flush=True)

    def record(eip):
        o, r = music()
        ticks.write('%d %.5f %d %d %.1f %d %d %x %x %.1f %d %x\n' % (
            st['frame'], st['now'] / PIT_HZ, o, r, rdf(V_TICKS), rd32(V_PART_TICKS), rd32(V_SCRIPT),
            rd32(V_FRAME_FN), rd32(V_TICK_FN), rdf(V_SHOWN), st['irqs'], eip))
        if frames is not None:
            frames.write(bytes(mu.mem_read(0xa0000, 64000)) if st['gfx'] else bytes(64000))
            frames.write(bytes(st['dac']))

    next_frame = FRAME                            # frames are recorded at the start of each refresh's line 0
    last_fn = None
    while not st['stop'] and st['frame'] < a.frames:
        # the next event: PIT interrupt or the start of a retrace
        irq_t = st['next_irq'] if (st['pit_armed'] and 8 in st['vec'] and not st['pic_mask'] & 1) else None
        if irq_t is not None and irq_t <= next_frame and st['now'] >= irq_t - 1e-6:
            if st['pit_mode'] == 0:
                st['pit_armed'] = False
                st['next_irq'] = None
            else:
                st['pit_start'] = irq_t
                st['next_irq'] = irq_t + st['pit_count']
            st['irqs'] += 1
            gus.tick = st['irqs']
            run_handler(st['vec'][8])
            continue
        if st['now'] >= next_frame - 1e-6:
            f = st['frame']
            fn = rd32(V_FRAME_FN)
            if f in snap_frames or (a.snap_parts and fn != st['snapped_fn'] and fn):
                st['snapped_fn'] = fn
                save_snap('f%d.snap' % f, eip)
            record(eip)
            if fn != last_fn:
                print('frame %d time %.3f: frame routine %x, tick routine %x, script %d, music %s' % (
                    f, st['now'] / PIT_HZ, fn, rd32(V_TICK_FN), rd32(V_SCRIPT), music()), flush=True)
                last_fn = fn
            st['frame'] += 1
            next_frame += FRAME
            if f % 70 == 0:
                print('frame %d time %.2f music %s ticks %.0f copies %d eip %x' % (
                    f, st['now'] / PIT_HZ, music(), rdf(V_TICKS), st['copies'], eip), flush=True)
            continue
        if st['pit_armed'] and st['now'] >= (st['next_irq'] or 0) and irq_t is None and st['pit_mode'] == 0:
            st['pit_armed'] = False                  # interrupt masked or no handler: the one shot is lost
        rate = a.ips
        q = a.quantum or max(500, int(a.ips // 20000))
        limit = next_frame if irq_t is None else min(irq_t, next_frame)
        n = int(min(q, max(1.0, (limit - st['now']) * rate / PIT_HZ + 1)))
        st['skip'] = False
        try:
            mu.emu_start(eip, 0xffffffff, count=n)
        except UcError as e:
            print('error in main at %x: %s' % (reg(UC_X86_REG_EIP), e), flush=True)
            break
        eip = reg(UC_X86_REG_EIP)
        if not st['skip']:
            st['now'] += n * PIT_HZ / rate
    open(a.out + '.dram', 'wb').write(bytes(gus.dram))
    ticks.close()
    if frames is not None:
        frames.close()
    glog.close()
    flog.close()
    print('ended at frame %d, time %.2f s, copies %d' % (st['frame'], st['now'] / PIT_HZ, st['copies']))
    print('out ports', {hex(k): v for k, v in sorted(outs.items())})
    print('in ports', dict(sorted(ins.items())))
    print('services', dict(sorted(services.items())))
    print('GUS register reads', {hex(k): v for k, v in sorted(gus.reads.items())})
    print('GUS DMA transfers %d, %d bytes' % (gus.dma_count, gus.dma_bytes))
    print('text output:', repr(st['text']))


if __name__ == '__main__':
    main()

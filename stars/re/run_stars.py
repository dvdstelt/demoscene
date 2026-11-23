"""Run STARS.EXE's 32-bit program headless in unicorn and record what it does.

No DOS and no PMODE kernel: the program image (unpacked/stars_img.bin from unpack.py) is loaded at linear 0 with
flat selectors 0x08 (code) and 0x10 (data), so the code32 base [0x17620] is 0 and a near pointer is a linear address
(VGA = 0xa0000). The kernel's hand-over values are preset: [0x17620] code32 base, [0x17624] / [0x17628] free low
memory size / start, ESP. PMODE's services are emulated where the program calls them: int 31h 0000/0001/0006/
0007/0008/0009 (descriptors), 0200/0201 (real-mode vectors), 0204/0205 (protected-mode vectors), 0300 (simulate a
real-mode interrupt: DOS file I/O on STARS.EXE, int 10h), 0303/0304 (real-mode callbacks, not used further),
0501/0502 (memory blocks); int 21h (2C time, 51 PSP, 42 seek, 4C exit, 02/09 text), int 10h (mode 13h, 1A00 = no
second display, text mode). Faked hardware: PIT channel 0, PIC masks, VGA DAC (write and read), the retrace bit,
keyboard (no key), and a Gravis UltraSound at 0x240 with its register file, 1 MB DRAM and timer 1 / timer 2
(IRQ 7, from ULTRASND=240,3,3,7,7), which is what clocks the music player.

Time: the PIT clock (1,193,182 Hz). Main-line code runs at --ips instructions per second in quanta (--quantum);
interrupt handlers run instantly. Events: PIT IRQ0 (the demo programs 0x4286 = 70.06 Hz), the GUS timer IRQ (timer 1:
(256 - reg 0x46) * 80 us, timer 2: (256 - reg 0x47) * 320 us, the player's tick), and the VGA frame (70.086 Hz, mode
13h: 449 lines; 0x3da bit 3 = vertical sync, lines 412-413; bit 0 = display disabled, lines 400-448). A loop that
polls 0x3da without the value changing ends the quantum and the clock jumps to the next change.

Outputs (prefix --out), one record per displayed frame (70.086 Hz), taken at the start of the vertical sync that
ends it: VGA memory then holds what the monitor showed in that frame's 400 lines (the program draws or copies its
next picture after waiting for the sync, before the next frame's line 0):
  <out>.frames  32-byte header (b'STARFRM1', <IIIIII: 320, 200, 8, record bytes 64768, header bytes 32, 0), then per
                frame 64000 bytes of VGA memory (mode 13h, 0xa0000) + 768 bytes of 6-bit DAC palette
  <out>.ticks   per frame: frame, time (s, program time at the frame's line 0), PIT ticks so far, GUS player ticks
                so far, music order [0x1c829], row [0x1c828], part (routine main is in, see PARTS), the PIT tick
                callback [0x4ab06] (each part installs its own), frames drawn (vertical syncs the program waited
                for) so far, main EIP
  <out>.gus     GUS register writes: <player tick> v<voice> r<reg> <value>  (and p<port> <value> for other ports)
  <out>.dram    final GUS DRAM (1 MB)
  <out>.log     (stdout) text output of the program, service calls, part changes

usage: python3 run_stars.py --out full [--ips 40000000] [--frames N] [--snap-at F,... --snap snaps/f{frame}.snap]
       python3 run_stars.py --load --snap snaps/f1234.snap --out x --frames 2000
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
while not os.path.exists(os.path.join(HERE, 'unpacked', 'stars_img.bin')) and os.path.dirname(HERE) != HERE:
    HERE = os.path.dirname(HERE)              # a copy in models/<scope>/ finds stars/re
IMG = open(os.path.join(HERE, 'unpacked', 'stars_img.bin'), 'rb').read()
EXE = os.path.join(HERE, '..', 'original', 'STARS.EXE')

PIT_HZ = 1193182
VGA_HZ = 25175000 / (800 * 449)          # mode 13h: 70.086 Hz
LINES = 449                              # mode 13h (400-line CRTC timing): 449 lines per frame, line 0 = first
VS0, VS1 = 412, 414                      # displayed line; vertical sync (0x3da bit 3) during lines 412-413,
DISP = 400                               # display disabled (bit 0) from line 400 to the end of the frame
RAM = 0x800000                           # 8 MB
ENTRY = 0x178c3
STACK = 0x840b0                          # SS:SP 7c0b:8000 of the original, relative to the load address
LOWFREE = 0x840b0                        # free low memory as the kernel reports it ([0x17628], [0x17624])
PSP = 0x90000                            # fake PSP, environment, GDT and the IRQ return sentinel
ENV = 0x90200
GDT = 0x91000
SENTINEL = 0x92000
HEAP = 0x100000                          # int 31h 0501 blocks
FILEBASE = 5                             # first DOS file handle

# main (0x4d) calls these one after the other: call site -> routine (see PARTS.md). Filled in from main's listing.
MAIN_CALLS = {}
# calls in main that are not parts: music position (polled), fades, picture display, delays, memory
QUIET = {0x29f80, 0x17e96, 0x17eba, 0x17e50, 0x4a843, 0x17bd7, 0x17cd2, 0x17c8e, 0x4a9f3, 0x17ad6}


def load_main_calls():
    """Every `call` in main (0x4d..0x472) with its target, so the harness can tell which routine main is in."""
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for ins in md.disasm(IMG[0x4d:0x473], 0x4d):
        if ins.mnemonic == 'call':
            try:
                MAIN_CALLS[ins.address] = int(ins.op_str, 16)
            except ValueError:
                pass


class Gus:
    """Gravis UltraSound: register file, 1 MB DRAM, timers. From jizz/re/run_jizz.py, plus the timer block."""
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
        self.t_ctrl = 0          # reg 0x45: bit 2 timer 1 IRQ enable, bit 3 timer 2
        self.t1 = 0              # reg 0x46
        self.t2 = 0              # reg 0x47
        self.t_start = 0         # AdLib timer control (base+9 after index 4): bit 0 start timer 1, bit 1 timer 2
        self.irq_status = 0
        self.dma_pending = False
        self.dma = None          # callback(channel) -> bytes of the programmed 8237 transfer (set by the harness)
        self.raise_irq = None    # callback() -> IRQ (set by the harness)
        self.dma_count = 0
        self.dma_bytes = 0

    def out(self, port, size, val):
        p = port - self.base
        if size == 2 and p == 0x102:        # voice select + register select
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
                # DMA to DRAM (bit 1 = 0: write), instantly; bit 2 = 16-bit DMA channel, bit 6 = 16-bit data,
                # bit 7 = invert the MSB (of every byte, or of every second byte for 16-bit data)
                r42 = self.glob.get(0x42, 0)
                ch = self.glob.get('ch', 1)
                if val & 4:
                    dst = ((r42 & 0xc000) << 4) | ((r42 & 0x1fff) << 5)
                else:
                    dst = r42 << 4
                data = bytearray(self.dma(ch))
                if val & 0x80:
                    for i in range(1 if val & 0x40 else 0, len(data), 2 if val & 0x40 else 1):
                        data[i] ^= 0x80
                for i, b in enumerate(data):
                    self.dram[(dst + i) & 0xfffff] = b
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
            if r >= 0x80 and r < 0x90:
                v = self.regs.get((self.voice, r - 0x80), 0)
                if r == 0x80: v |= 1        # voice stopped
                if r == 0x8d: v |= 1        # volume ramp stopped
                if p == 0x105 and size == 1 and r in (0x81, 0x82, 0x83, 0x84, 0x85, 0x89, 0x8a, 0x8b):
                    return (v >> 8) & 0xff
                return v & (0xff if size == 1 else 0xffff)
            if r == 0x8f:                   # IRQ source: none pending
                return 0xe0
            if r == 0x41:                   # DMA control; bit 6 = DMA IRQ pending, cleared by this read
                v = (self.glob.get(0x41, 0) & ~0x40) | (0x40 if self.dma_pending else 0)
                self.dma_pending = False
                self.irq_status &= ~0x80
                return v
            if r in (0x49, 0x45, 0x4c, 0x8e):
                return self.glob.get(r & 0x7f, 0) & 0xff
            return 0
        if p == 0x006:                      # IRQ status: timer bits
            return self.irq_status
        if p == 0x008:                      # AdLib status: bit 7 any, 6 timer 1, 5 timer 2
            s = 0
            if self.irq_status & 4: s |= 0xc0
            if self.irq_status & 8: s |= 0xa0
            return s
        if p == 0x103:
            return self.reg
        if p == 0x102:
            return self.voice
        return 0xff

    def period(self, which):
        """Timer period in PIT clocks (timer 1: 80 us steps, timer 2: 320 us), from the count register."""
        if which == 1:
            return (256 - self.t1) * 80e-6 * PIT_HZ
        return (256 - self.t2) * 320e-6 * PIT_HZ


def desc(base, limit, access, flags):
    """GDT descriptor; flags = high nibble of byte 6 (G, D/B, L, AVL)."""
    return struct.pack('<HHBBBB', limit & 0xffff, base & 0xffff, (base >> 16) & 0xff, access,
                       (flags & 0xf0) | ((limit >> 16) & 0xf), (base >> 24) & 0xff)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--frames', type=int, default=1 << 30, help='stop after this many retraces')
    ap.add_argument('--ips', type=float, default=40e6, help='main-line instructions per second')
    ap.add_argument('--quantum', type=int, default=0, help='instructions per emulation slice (default ips/70/32)')
    ap.add_argument('--minute', type=int, default=0, help='minute returned by int 21h AH=2C (the demo uses its '
                    'parity, [4] = minute & 1)')
    ap.add_argument('--start-frame', type=int, default=0, help='only record frames from this one on')
    ap.add_argument('--snap-at', default='', help='save snapshots at these frames (comma-separated); --snap may '
                    'contain {frame}; "parts" = at the first retrace in each main routine')
    ap.add_argument('--snap', help='snapshot file to write (with --snap-at) or to resume from (with --load)')
    ap.add_argument('--load', action='store_true', help='resume from --snap')
    ap.add_argument('--snap-out', help='with --load: where to write new snapshots (may contain {frame} / {part})')
    ap.add_argument('--freeze-pit', action='store_true', help='analysis: IRQ0 is not delivered, so the PIT tick '
                    'callbacks do not run; whatever still moves is stepped per frame (or by the music)')
    ap.add_argument('--part-ips', default='', help='per-part speed: ADDR=IPS,... (ADDR = routine called from main, '
                    'see PARTS.md), applies while main is in that routine')
    ap.add_argument('--trace', default='', help='comma-separated addresses: print the frame the first time each runs')
    ap.add_argument('--dump-files', default='', help='directory: write every file the program loads (0x4a9f3), as '
                    'the program has it after loading (decompressed)')
    a = ap.parse_args()
    load_main_calls()
    part_ips = {}
    for item in filter(None, a.part_ips.split(',')):
        k, v = item.split('=')
        part_ips[int(k, 16)] = float(v)

    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(0, RAM)
    mu.mem_write(0, IMG)
    # kernel hand-over: code32 base 0, free low memory [LOWFREE, PSP)
    mu.mem_write(0x17620, struct.pack('<III', 0, PSP - LOWFREE, LOWFREE))
    psp = bytearray(256)
    psp[0:2] = b'\xcd\x20'
    struct.pack_into('<H', psp, 2, 0xa000)
    struct.pack_into('<H', psp, 0x2c, ENV >> 4)
    psp[0x81] = 0x0d
    mu.mem_write(PSP, bytes(psp))
    mu.mem_write(ENV, b'PATH=C:\\\x00ULTRASND=240,3,3,7,7\x00\x00\x01\x00C:\\STARS\\STARS.EXE\x00')
    gdt = bytearray(8 * 64)
    gdt[8:16] = desc(0, 0xfffff, 0x9a, 0xc0)
    gdt[16:24] = desc(0, 0xfffff, 0x92, 0xc0)
    mu.mem_write(GDT, bytes(gdt))
    mu.mem_write(SENTINEL - 0x10, b'\xcf' * 0x10)        # default handlers for int 31h 0204: iretd
    mu.reg_write(UC_X86_REG_GDTR, (0, GDT, len(gdt) - 1, 0))
    mu.reg_write(UC_X86_REG_CS, 0x08)
    for r in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_FS, UC_X86_REG_GS):
        mu.reg_write(r, 0x10)
    mu.reg_write(UC_X86_REG_ESP, STACK)

    glog = open(a.out + '.gus', 'w')
    gus = Gus(glog)
    gus.glob['ch'] = 3                       # ULTRASND=240,3,3,7,7: playback DMA 3
    pal = bytearray(768)
    exe = open(EXE, 'rb').read()
    # st: everything that goes into a snapshot
    st = {'now': 0.0, 'pit_count': 0x10000, 'next_pit': float(0x10000), 'pit_w': 0, 'pit_lo': 0, 'pit_ticks': 0,
          'next_t1': None, 'next_t2': None, 'gus_ticks': 0, 'gus_irqs': 0, 'next_vs': PIT_HZ / VGA_HZ * VS0 / LINES, 'frame': 0, 'drawn': 0, 'poll_val': None,
          'poll_n': 0, 'idle': False, 'pic21': 0xb8, 'pica1': 0xff, 'vec': {}, 'rmvec': {}, 'sels': {},
          'nextsel': 0x18, 'heap': HEAP, 'blocks': {}, 'files': {}, 'nfile': FILEBASE, 'text': '', 'dac_w': 0,
          'dac_r': 0, 'mode': 3, 'part': 0, 'stop': False, 'callbacks': 0, 'eip': ENTRY, 'pending': [],
          'loads': 0}
    outs = {}
    ins = {}
    # 8237 DMA controllers: per channel address, count, page; flip-flops
    DMA_PAGE = {0x87: 0, 0x83: 1, 0x81: 2, 0x82: 3, 0x8f: 4, 0x8b: 5, 0x89: 6, 0x8a: 7}
    dma = {'addr': [0] * 8, 'count': [0] * 8, 'page': [0] * 8, 'ff': [0, 0], 'mode': [0] * 8}

    def dma_out(port, val):
        if port < 0x10:
            c, base = 0, 0
            p = port
        else:
            c, base = 1, 4
            p = (port - 0xc0) // 2
        if p < 8:
            ch = base + p // 2
            key = 'addr' if p % 2 == 0 else 'count'
            if dma['ff'][c] == 0:
                dma[key][ch] = (dma[key][ch] & 0xff00) | val
            else:
                dma[key][ch] = (dma[key][ch] & 0x00ff) | (val << 8)
            dma['ff'][c] ^= 1
        elif p == 0x0c or (c and port == 0xd8):
            dma['ff'][c] = 0
        elif p == 0x0b or (c and port == 0xd6):
            dma['mode'][base + (val & 3)] = val

    def dma_read(ch):
        n = dma['count'][ch] + 1
        if ch < 4:
            lin = dma['page'][ch] << 16 | dma['addr'][ch]
        else:
            lin = (dma['page'][ch] & 0xfe) << 16 | dma['addr'][ch] << 1
            n *= 2
        return bytes(mu.mem_read(lin, n))

    def reg(r): return mu.reg_read(r)
    def setcf(c):
        f = mu.reg_read(UC_X86_REG_EFLAGS)
        mu.reg_write(UC_X86_REG_EFLAGS, (f | 1) if c else (f & ~1))

    def text(s):
        st['text'] += s
        while '\n' in st['text']:
            line, st['text'] = st['text'].split('\n', 1)
            print('text: %s' % line.rstrip('\r')[-200:], flush=True)

    def cstr(addr, end=b'$', n=400):
        return bytes(mu.mem_read(addr, n)).split(end)[0].decode('latin1')

    def setdesc(sel):
        base, limit, acc, ext = st['sels'][sel]
        mu.mem_write(GDT + (sel & ~7), desc(base, limit, acc, ext))

    # ---- DOS (int 21h in protected mode, and through int 31h 0300) ---------------------------------------------
    def dos(r, rm):
        """r: dict of 32-bit registers (eax, ebx, ecx, edx, esi, edi, flags); rm: real-mode call (ds:dx addressing)."""
        ah = (r['eax'] >> 8) & 0xff
        al = r['eax'] & 0xff
        cf = 0
        if rm:
            dsdx = r['ds'] * 16 + (r['edx'] & 0xffff)
        else:
            dsdx = r['edx']
        if ah == 0x02:
            text(chr(r['edx'] & 0xff))
        elif ah == 0x09:
            text(cstr(dsdx))
        elif ah == 0x2c:
            r['ecx'] = (r['ecx'] & ~0xffff) | (12 << 8) | (a.minute & 0xff)
            r['edx'] = (r['edx'] & ~0xffff) | (0 << 8) | 0
        elif ah == 0x30:
            r['eax'] = (r['eax'] & ~0xffff) | 0x0005
        elif ah == 0x51 or ah == 0x62:
            r['ebx'] = (r['ebx'] & ~0xffff) | (PSP >> 4)
        elif ah == 0x3d:
            name = cstr(dsdx, b'\x00', 128)
            h = st['nfile']
            st['nfile'] += 1
            st['files'][h] = 0
            print('open %r mode %d -> handle %d (STARS.EXE)' % (name, al, h), flush=True)
            r['eax'] = (r['eax'] & ~0xffff) | h
        elif ah == 0x3e:
            st['files'].pop(r['ebx'] & 0xffff, None)
        elif ah == 0x3f:
            h = r['ebx'] & 0xffff
            n = r['ecx'] & 0xffff
            pos = st['files'][h]
            chunk = exe[pos:pos + n]
            mu.mem_write(dsdx, chunk)
            st['files'][h] = pos + len(chunk)
            r['eax'] = (r['eax'] & ~0xffff) | len(chunk)
        elif ah == 0x42:
            h = r['ebx'] & 0xffff
            off = ((r['ecx'] & 0xffff) << 16) | (r['edx'] & 0xffff)
            if off >= 1 << 31: off -= 1 << 32
            pos = {0: 0, 1: st['files'][h], 2: len(exe)}[al] + off
            st['files'][h] = pos
            r['eax'] = (r['eax'] & ~0xffff) | (pos & 0xffff)
            r['edx'] = (r['edx'] & ~0xffff) | ((pos >> 16) & 0xffff)
        elif ah == 0x4c:
            print('exit code %d at frame %d (time %.2f s)' % (al, st['frame'], st['now'] / PIT_HZ), flush=True)
            st['stop'] = True
            mu.emu_stop()
        else:
            print('int 21h ah=%02x unhandled (%s) at %x' % (ah, 'rm' if rm else 'pm', reg(UC_X86_REG_EIP)),
                  flush=True)
            cf = 1
        r['flags'] = (r['flags'] & ~1) | cf

    # ---- BIOS video ------------------------------------------------------------------------------------------
    def int10(r):
        ax = r['eax'] & 0xffff
        ah = ax >> 8
        if ah == 0x00:
            st['mode'] = ax & 0x7f
            print('int 10h set mode %#x at frame %d' % (ax & 0xff, st['frame']), flush=True)
            if st['mode'] == 0x13 and not ax & 0x80:
                mu.mem_write(0xa0000, bytes(0x10000))
        elif ax == 0x1a00:
            r['eax'] = (r['eax'] & ~0xffff) | 0x001a
            r['ebx'] = (r['ebx'] & ~0xffff) | 0x0008      # active: VGA colour, no inactive display (no MDA)
        elif ah == 0x0f:
            r['eax'] = (r['eax'] & ~0xffff) | (40 << 8) | st['mode']
        elif ah in (0x01, 0x02, 0x03, 0x06, 0x09, 0x0e, 0x10, 0x11, 0x12):
            if ah == 0x0e:
                text(chr(ax & 0xff))
        else:
            print('int 10h ax=%04x unhandled' % ax, flush=True)

    def regs_pm():
        return {k: reg(v) for k, v in (('eax', UC_X86_REG_EAX), ('ebx', UC_X86_REG_EBX), ('ecx', UC_X86_REG_ECX),
                                       ('edx', UC_X86_REG_EDX), ('esi', UC_X86_REG_ESI), ('edi', UC_X86_REG_EDI),
                                       ('flags', UC_X86_REG_EFLAGS))}

    def put_pm(r):
        for k, v in (('eax', UC_X86_REG_EAX), ('ebx', UC_X86_REG_EBX), ('ecx', UC_X86_REG_ECX),
                     ('edx', UC_X86_REG_EDX), ('esi', UC_X86_REG_ESI), ('edi', UC_X86_REG_EDI),
                     ('flags', UC_X86_REG_EFLAGS)):
            mu.reg_write(v, r[k] & 0xffffffff)

    RMS = (('edi', 0, 4), ('esi', 4, 4), ('ebp', 8, 4), ('ebx', 0x10, 4), ('edx', 0x14, 4), ('ecx', 0x18, 4),
           ('eax', 0x1c, 4), ('flags', 0x20, 2), ('es', 0x22, 2), ('ds', 0x24, 2))

    def int31():
        eax = reg(UC_X86_REG_EAX)
        ax = eax & 0xffff
        bx = reg(UC_X86_REG_EBX) & 0xffff
        cx = reg(UC_X86_REG_ECX) & 0xffff
        dx = reg(UC_X86_REG_EDX) & 0xffff
        cf = 0
        if ax == 0x0000:                    # allocate descriptors
            sel = st['nextsel']
            st['nextsel'] += 8 * max(1, cx)
            for i in range(max(1, cx)):
                st['sels'][sel + 8 * i] = [0, 0, 0x92, 0x40]
                setdesc(sel + 8 * i)
            mu.reg_write(UC_X86_REG_EAX, (eax & ~0xffff) | sel)
        elif ax == 0x0001:
            st['sels'].pop(bx, None)
        elif ax == 0x0006:                  # get segment base
            if bx in st['sels']:
                base = st['sels'][bx][0]
            elif bx in (0x08, 0x10):
                base = 0
            else:
                base = bx << 4              # e.g. the environment segment from the PSP
            mu.reg_write(UC_X86_REG_ECX, base >> 16)
            mu.reg_write(UC_X86_REG_EDX, base & 0xffff)
        elif ax in (0x0007, 0x0008, 0x0009):
            s = st['sels'].setdefault(bx, [0, 0, 0x92, 0x40])
            if ax == 7:
                s[0] = cx << 16 | dx
            elif ax == 8:
                s[1] = cx << 16 | dx
                if s[1] > 0xfffff:
                    s[1] >>= 12
                    s[3] |= 0x80
            else:
                s[2] = cx & 0xff
                s[3] = (s[3] & 0x80) | ((cx >> 8) & 0x70)
            setdesc(bx)
        elif ax == 0x0200:
            seg, off = st['rmvec'].get(bx & 0xff, (0xf000, bx & 0xff))
            mu.reg_write(UC_X86_REG_ECX, seg)
            mu.reg_write(UC_X86_REG_EDX, off)
        elif ax == 0x0201:
            st['rmvec'][bx & 0xff] = (cx, dx)
        elif ax == 0x0204:
            sel, off = st['vec'].get(bx & 0xff, (0x08, SENTINEL - 0x10 + (bx & 0xf)))
            mu.reg_write(UC_X86_REG_ECX, sel)
            mu.reg_write(UC_X86_REG_EDX, off)
        elif ax == 0x0205:
            st['vec'][bx & 0xff] = (cx, reg(UC_X86_REG_EDX))
            print('set PM vector %#x -> %x (frame %d)' % (bx & 0xff, reg(UC_X86_REG_EDX), st['frame']), flush=True)
        elif ax == 0x0300:
            edi = reg(UC_X86_REG_EDI)
            blk = bytes(mu.mem_read(edi, 0x32))
            r = {k: int.from_bytes(blk[o:o + n], 'little') for k, o, n in RMS}
            intno = bx & 0xff
            if intno == 0x21:
                dos(r, True)
            elif intno == 0x10:
                int10(r)
            else:
                print('real-mode int %x ax=%04x unhandled' % (intno, r['eax'] & 0xffff), flush=True)
            b = bytearray(blk)
            for k, o, n in RMS:
                b[o:o + n] = (r[k] & ((1 << (8 * n)) - 1)).to_bytes(n, 'little')
            mu.mem_write(edi, bytes(b))
        elif ax == 0x0303:                  # real-mode callback: a fake address, never called
            st['callbacks'] += 1
            mu.reg_write(UC_X86_REG_ECX, 0xf100)
            mu.reg_write(UC_X86_REG_EDX, 0x10 * st['callbacks'])
        elif ax == 0x0304:
            pass
        elif ax == 0x0501:
            size = bx << 16 | cx
            p = (st['heap'] + 0xfff) & ~0xfff
            if p + size > RAM:
                print('0501: out of memory for %#x' % size, flush=True)
                cf = 1
            else:
                st['heap'] = p + size
                st['blocks'][p] = size
                mu.reg_write(UC_X86_REG_EBX, p >> 16)
                mu.reg_write(UC_X86_REG_ECX, p & 0xffff)
                mu.reg_write(UC_X86_REG_ESI, p >> 16)
                mu.reg_write(UC_X86_REG_EDI, p & 0xffff)
                print('0501 alloc %#x -> %#x' % (size, p), flush=True)
        elif ax == 0x0502:
            pass
        else:
            print('int 31h ax=%04x unhandled at %x' % (ax, reg(UC_X86_REG_EIP)), flush=True)
            cf = 1
        setcf(cf)

    def on_intr(mu, intno, _):
        if intno == 0x31:
            int31()
        elif intno == 0x21:
            r = regs_pm()
            dos(r, False)
            put_pm(r)
        elif intno == 0x10:
            r = regs_pm()
            int10(r)
            put_pm(r)
        elif intno == 0x16:
            ah = (reg(UC_X86_REG_EAX) >> 8) & 0xff
            f = reg(UC_X86_REG_EFLAGS)
            if ah in (0x01, 0x11):
                mu.reg_write(UC_X86_REG_EFLAGS, f | 0x40)      # ZF: no key
            else:
                mu.reg_write(UC_X86_REG_EAX, 0)
        else:
            print('int %x at %x: stop' % (intno, reg(UC_X86_REG_EIP)), flush=True)
            st['stop'] = True
            mu.emu_stop()

    # ---- ports -----------------------------------------------------------------------------------------------
    def retrace(t):
        line = (t % (PIT_HZ / VGA_HZ)) * LINES / (PIT_HZ / VGA_HZ)
        return (0x08 if VS0 <= line < VS1 else 0) | (0x01 if line >= DISP else 0)

    def on_in(mu, port, size, _):
        ins[port] = ins.get(port, 0) + 1
        if port == 0x3da:
            # successive polls within one quantum move the clock a little (4 instructions each)
            t = st['now'] + st['poll_n'] * 4 * PIT_HZ / a.ips
            v = retrace(t)
            if v == st['poll_val']:
                st['poll_n'] += 1
                if st['poll_n'] >= 16:
                    st['idle'] = True
                    mu.emu_stop()
            else:
                if st['poll_val'] is not None and (v & 8) and not (st['poll_val'] & 8):
                    st['drawn'] += 1
                st['poll_val'] = v
                st['poll_n'] = 1
            return v
        if port == 0x3c9:
            v = pal[st['dac_r'] % 768]
            st['dac_r'] += 1
            return v
        if port == 0x21:
            return st['pic21']
        if port == 0xa1:
            return st['pica1']
        if port == 0x60:
            return 0
        if port == 0x64:
            return 0x14
        if 0x240 <= port < 0x350:
            return gus.inp(port, size)
        if port not in (0x61,) and not 0x200 <= port < 0x300:
            print('in %#x unhandled' % port, flush=True)
        return 0xff

    def on_out(mu, port, size, val, _):
        outs[port] = outs.get(port, 0) + 1
        if port == 0x43:
            if val & 0xc0 == 0:
                st['pit_w'] = 0
        elif port == 0x40:
            if st['pit_w'] == 0:
                st['pit_lo'] = val & 0xff
                st['pit_w'] = 1
            else:
                c = st['pit_lo'] | (val & 0xff) << 8
                st['pit_count'] = c or 0x10000
                st['next_pit'] = st['now'] + st['pit_count']
                st['pit_w'] = 0
                print('PIT count %#x (%.3f Hz) at frame %d' % (st['pit_count'], PIT_HZ / st['pit_count'],
                                                               st['frame']), flush=True)
        elif port == 0x3c8:
            st['dac_w'] = (val & 0xff) * 3
        elif port == 0x3c7:
            st['dac_r'] = (val & 0xff) * 3
        elif port == 0x3c9:
            pal[st['dac_w'] % 768] = val & 0x3f
            st['dac_w'] += 1
        elif port == 0x21:
            st['pic21'] = val & 0xff
        elif port == 0xa1:
            st['pica1'] = val & 0xff
        elif port in (0x20, 0xa0):
            pass
        elif port < 0x10 or 0xc0 <= port < 0xe0:
            dma_out(port, val & 0xff)
        elif port in DMA_PAGE:
            dma['page'][DMA_PAGE[port]] = val & 0xff
        elif 0x240 <= port < 0x350:
            old = gus.t_start
            gus.out(port, size, val)
            # the timers count while started (AdLib control bit 0 / 1); the count register is loaded at each
            # overflow; reg 0x45 only enables the IRQ
            for i in (1, 2):
                bit = 1 << (i - 1)
                if gus.t_start & bit and not old & bit:
                    st['next_t%d' % i] = st['now'] + gus.period(i)
                elif not gus.t_start & bit:
                    st['next_t%d' % i] = None
        else:
            print('out %#x %#x unhandled' % (port, val), flush=True)

    gus.dma = lambda ch: dma_read(ch)
    gus.raise_irq = lambda: st['pending'].append(0x0f)

    def on_part(mu, addr, size, _):
        p = MAIN_CALLS.get(addr)
        if p is not None and p not in QUIET:
            st['part'] = p
            print('main %x: call %x at frame %d (time %.2f s, music %02x:%02x, drawn %d)' % (
                addr, p, st['frame'], st['now'] / PIT_HZ, mu.mem_read(0x1c829, 1)[0], mu.mem_read(0x1c828, 1)[0],
                st['drawn']), flush=True)
            if a.snap_at == 'parts':
                st['snap_next'] = '%x' % p

    traced = set()
    def on_trace(mu, addr, size, _):
        if addr not in traced:
            traced.add(addr)
            print('trace %x frame %d time %.3f' % (addr, st['frame'], st['now'] / PIT_HZ), flush=True)

    def on_loaded(mu, addr, size, _):
        # 0x4a9f3 returns here (ret at 0x4aa17) with EAX = the loaded (decompressed) file
        n = st.get('loading')
        p = reg(UC_X86_REG_EAX)
        size_ = struct.unpack('<I', mu.mem_read(0x4a900, 4))[0]
        print('loaded file %s -> %#x' % (n, p), flush=True)
        if a.dump_files and n is not None:
            # the block size from the memory manager is not known here: dump the decompressed size from the header
            raw = exe[len(exe) + struct.unpack('<i', exe[len(exe) - 12 * n + 4:len(exe) - 12 * n + 8])[0]:]
            if raw[6:9] == b'dlz':
                osz = ((raw[14] >> 2) << 16) | struct.unpack_from('<H', raw, 15)[0]
            else:
                osz = size_
            os.makedirs(a.dump_files, exist_ok=True)
            open(os.path.join(a.dump_files, '%02d.bin' % n), 'wb').write(bytes(mu.mem_read(p, osz)))

    def on_load(mu, addr, size, _):
        st['loading'] = reg(UC_X86_REG_EAX) & 0xffff

    for x in filter(None, a.trace.split(',')):
        mu.hook_add(UC_HOOK_CODE, on_trace, None, int(x, 16), int(x, 16))
    mu.hook_add(UC_HOOK_CODE, on_part, None, min(MAIN_CALLS), max(MAIN_CALLS))
    mu.hook_add(UC_HOOK_CODE, on_load, None, 0x4a9f3, 0x4a9f3)
    mu.hook_add(UC_HOOK_CODE, on_loaded, None, 0x4aa17, 0x4aa17)
    mu.hook_add(UC_HOOK_INSN, on_in, None, 1, 0, UC_X86_INS_IN)
    mu.hook_add(UC_HOOK_INSN, on_out, None, 1, 0, UC_X86_INS_OUT)
    mu.hook_add(UC_HOOK_INTR, on_intr)

    eip = ENTRY
    if a.load:
        snap = pickle.load(open(a.snap, 'rb'))
        mu.mem_write(0, snap['mem'])
        mu.context_restore(snap['ctx'])
        pal[:] = snap['pal']
        st.update(snap['st'])
        gus.__dict__.update(snap['gus'])
        gus.log = glog
        gus.dma = lambda ch: dma_read(ch)
        gus.raise_irq = lambda: st['pending'].append(0x0f)
        dma.update(snap.get('dma', {}))
        eip = snap['eip']
        print('resumed at frame', st['frame'])

    frames = open(a.out + '.frames', 'wb')
    REC = 64000 + 768
    frames.write(b'STARFRM1' + struct.pack('<IIIIII', 320, 200, 8, REC, 32, 0))
    ticks = open(a.out + '.ticks', 'w')
    ticks.write('# frame time_s pit_ticks gus_ticks order row part tick_callback drawn eip\n')
    snap_frames = {int(x) for x in a.snap_at.split(',') if x and x != 'parts'}

    def deliver(intno):
        if intno not in st['vec']:
            return
        sel, off = st['vec'][intno]
        if off == 0x1abb0:                         # the player's timer handler: one music tick
            gus.tick = st['gus_ticks']
            st['gus_ticks'] += 1
        ctx = mu.context_save()
        esp = reg(UC_X86_REG_ESP) - 12
        mu.mem_write(esp, struct.pack('<III', SENTINEL, 0x08, reg(UC_X86_REG_EFLAGS) | 0x200))
        mu.reg_write(UC_X86_REG_ESP, esp)
        mu.reg_write(UC_X86_REG_EFLAGS, reg(UC_X86_REG_EFLAGS) & ~0x200)
        try:
            mu.emu_start(off, SENTINEL, count=20_000_000)
        except UcError as e:
            print('error in int %x handler at %x: %s' % (intno, reg(UC_X86_REG_EIP), e), flush=True)
            st['stop'] = True
        mu.context_restore(ctx)

    def snapshot(name):
        g = {k: v for k, v in gus.__dict__.items() if k not in ('log', 'dma', 'raise_irq')}
        pickle.dump({'mem': bytes(mu.mem_read(0, RAM)), 'ctx': mu.context_save(), 'pal': bytes(pal), 'st': dict(st),
                     'gus': g, 'dma': dma, 'eip': eip}, open(name, 'wb'))
        print('snapshot %s at frame %d' % (name, st['frame']), flush=True)

    t_host = htime.time()
    q0 = a.quantum or int(a.ips / 70 / 32)
    while not st['stop'] and st['frame'] < a.frames:
        # due events, in time order
        evs = [('vs', st['next_vs']), ('pit', st['next_pit'])]
        for i in (1, 2):
            if st['next_t%d' % i] is not None:
                evs.append(('t%d' % i, st['next_t%d' % i]))
        name, t_ev = min(evs, key=lambda e: e[1])
        if st['now'] >= t_ev - 1e-6:
            if name == 'vs':
                f = st['frame']
                sn = st.pop('snap_next', None)
                if f in snap_frames or sn:
                    snapshot((a.snap_out or a.snap).format(frame=f, part=sn or ''))
                order, row = mu.mem_read(0x1c829, 1)[0], mu.mem_read(0x1c828, 1)[0]
                cb = struct.unpack('<I', mu.mem_read(0x4ab06, 4))[0]
                tdisp = (st['now'] - PIT_HZ / VGA_HZ * VS0 / LINES) / PIT_HZ
                ticks.write('%d %.5f %d %d %d %d %x %x %d %x\n' % (f, tdisp, st['pit_ticks'], st['gus_ticks'], order,
                                                                   row, st['part'], cb, st['drawn'], eip))
                if f >= a.start_frame:
                    frames.write(bytes(mu.mem_read(0xa0000, 64000)) if st['mode'] == 0x13 else bytes(64000))
                    frames.write(bytes(pal))
                st['frame'] += 1
                st['next_vs'] += PIT_HZ / VGA_HZ
                if f % 700 == 0:
                    print('frame %d time %.2f music %02x:%02x part %x drawn %d eip %x (host %.0f s)' % (
                        f, st['now'] / PIT_HZ, order, row, st['part'], st['drawn'], eip, htime.time() - t_host),
                        flush=True)
            elif name == 'pit':
                st['next_pit'] += st['pit_count']
                st['pit_ticks'] += 1
                if not a.freeze_pit:
                    st['pending'].append(8)
            else:
                i = int(name[1])
                st['next_t%d' % i] += gus.period(i)
                if gus.t_ctrl & (2 << i):           # 0x45 bit 2 (timer 1) / bit 3 (timer 2): IRQ enabled
                    gus.irq_status |= 2 << i
                    st['gus_irqs'] += 1
                    st['pending'].append(0x0f)
            continue
        # deliver pending interrupts if the main line has interrupts enabled
        if st['pending'] and reg(UC_X86_REG_EFLAGS) & 0x200:
            for intno in st['pending']:
                irq = intno - 8
                if not (st['pic21'] >> irq) & 1:
                    deliver(intno)
            st['pending'] = []
            if st['stop']:
                break
        rate = part_ips.get(st['part'], a.ips)
        n = int(min(q0 * rate / a.ips, (t_ev - st['now']) * rate / PIT_HZ + 1))
        st['idle'] = False
        st['poll_n'] = 0
        t0 = st['now']
        try:
            mu.emu_start(eip, 0xffffffff, count=n)
        except UcError as e:
            print('error in main at %x: %s' % (reg(UC_X86_REG_EIP), e), flush=True)
            break
        eip = reg(UC_X86_REG_EIP)
        if st['idle']:
            # polling 0x3da: half the quantum was work, then jump to the next change of the retrace bit
            t = t0 + (n // 2) * PIT_HZ / rate
            T = PIT_HZ / VGA_HZ
            k = int(t / T)
            edges = [k * T + T * l / LINES for l in (DISP, VS0, VS1, LINES)]
            nxt = min([e for e in edges if e > t + 1e-6], default=(k + 1) * T)
            st['now'] = min(max(t, nxt), t_ev)
        else:
            st['now'] += n * PIT_HZ / rate
    open(a.out + '.dram', 'wb').write(bytes(gus.dram))
    glog.close()
    print('ended at frame %d, time %.2f s, PIT ticks %d, GUS ticks %d, drawn %d' % (
        st['frame'], st['now'] / PIT_HZ, st['pit_ticks'], st['gus_ticks'], st['drawn']))
    print('out ports', {hex(k): v for k, v in sorted(outs.items())})
    print('in ports', {hex(k): v for k, v in sorted(ins.items())})
    print('GUS register reads', {hex(k): v for k, v in sorted(gus.reads.items())})
    print('GUS DMA transfers %d, %d bytes' % (gus.dma_count, gus.dma_bytes))
    print('text output:', repr(st['text']))


if __name__ == '__main__':
    main()

"""Engine scope copy of run_clone.py: adds --dump-draws (state at the entry of the 3D engine's draw 0x4169f8 and
the target page after it) for engine.py. Everything else as in run_clone.py:

Run CLONE's 32-bit program headless in unicorn and record what it does.

No DOS, no PMODE/W and no veloader: the program image (unpacked/clone_img.bin from unpack.py, relocated for linear
base 0x400000) is loaded at 0x400000 with its BSS zeroed up to 0xf95937, and started the way the veloader starts
it: CS = flat code selector 0x08, DS = SS = FS = GS = flat data selector 0x10 (base 0, 4 GB), ES = PSP selector
0x18 (PMODE/W: the environment selector at PSP:2C, here 0x20), ESP = 0xf93940 with a return address that exits.
PMODE/W's services are emulated where the program calls them: int 21h 09 (print), 4C (exit), 3508 / 2508
(protected-mode vector of IRQ0), int 31h 0100 / 0101 (DOS memory with a selector), 0300 (simulate a real-mode
int 10h: VBE 4F00 / 4F01 / 4F02 / 4F05 / 4F07), 0800 (map the LFB), int 16h AH=01 (no key). Faked hardware: PIT
channel 0 (count, latch reads), PIC, a Gravis UltraSound at 0x240 (from ULTRASND=240,3,3,7,7 in the environment;
register file, 1 MB DRAM, word I/O; Gus class of jizz/re/run_jizz.py).

The VBE BIOS (2.0, 2 MB) offers 320x200 modes with a linear frame buffer at 0xe0000000: 0x10f with 32 bits per
pixel (BGRX: masks R 8@16, G 8@8, B 8@0, reserved 8@24; 1280 bytes per line) and 0x10d with 15 bits (5:5:5, 640
bytes per line). The intro asks for 320x200x32 first and takes 320x200x15 if that fails ("error initializing
32bit and 15bit videomode"); --bpp 15 offers only 0x10d. In the 32-bit mode it draws into four pages of 0x3e800
bytes and shows one with 4F07 BL=0 (set display start, no retrace wait); in the 15-bit mode it draws into a 32-bit
buffer at 0xdb1aec and converts it into the single 15-bit page (0x4188d4).

Time: the PIT clock (1,193,182 Hz). Main-line code runs at --ips instructions per second (default 150 M, about a
150 MHz Pentium at one instruction per clock) in quanta of --quantum instructions; a PIT latch read returns the
counter at the start of the current quantum. The timer interrupt comes when the programmed PIT period has elapsed
(MXMPlay programs its tick; before the music starts the BIOS rate 0x10000 is used and no handler runs). Interrupt
handlers run instantly. The intro never waits for the retrace, so it draws as fast as the CPU allows.

Outputs (prefix --out):
  <out>.frames  32-byte header (b'CLONFRM1', <IIIIII: width 320, height 200, bits 24, record bytes 192000, header
                bytes 32, video mode bits 32|15), then per timer tick the picture on screen at the end of the tick
                (record t = tick t), as 320x200 RGB (8 bits per channel; 15-bit pixels scaled by 255/31); black
                while the screen is in text mode. --flip-frames writes one record per shown frame instead (magic
                b'CLONFLP1', record i = flip i + 1; for short analysis runs: light parts show several hundred
                frames per second)
  <out>.flips   per shown frame (each call of the frame routine 0x400000: 4F07 page flip in the 32-bit mode,
                conversion to the LFB in the 15-bit mode): number, tick, program time (s) when it was shown, the timer
                value it was drawn with (first xmpGetTimer read 0x4022b3 after the previous frame; PIT clocks
                since the music start, signed; -2^40: none), the frame time t70 = timer * 70 / 1193182 (what main
                stores in [0xe9572e]), part, music order, row
  <out>.ticks   per PIT interrupt (player tick after xmpPlay): tick, PIT count, program time (s) at its end, music
                order, row, player timer (PIT clocks, after the handler), part [main ebp-0x14], sync index
                [0x420495], frames shown so far (the visible frame is record flips-1), main stage (the routine main
                is in; see STAGES), main EIP
  <out>.gus     GUS register writes:  tick v<voice> r<reg> value  /  tick p<port> value
  <out>.dram    final GUS DRAM (1 MB)
  stdout        text output of the program, service calls, part changes (redirect it to <out>.log)

usage: python3 run_clone.py --out full [--ips 150e6] [--ticks N] [--snap-parts --snapdir snaps]
       python3 run_clone.py --load --snap snaps/t1234.snap --out x --ticks 2000 [--part-ips 7=75e6]
The full run ends by itself when the program exits.
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
while not os.path.exists(os.path.join(HERE, 'unpacked', 'clone_img.bin')) and os.path.dirname(HERE) != HERE:
    HERE = os.path.dirname(HERE)              # a copy in models/<scope>/ finds clone/re
IMG = open(os.path.join(HERE, 'unpacked', 'clone_img.bin'), 'rb').read()
BASE = 0x400000
ENTRY = 0x4010de
ESP0 = 0xf93940
BSS_END = 0xf95937

PIT_HZ = 1193182
RAM = 0x2000000                 # 32 MB
IRET = 0x7f000                  # old IRQ0 handler: iretd
EXIT = 0x7f010                  # return address of the program's entry: int 21h 4C00
SENTINEL = 0x7fff0              # return address for IRQ delivery
GDT = 0x7e000                   # 64 descriptors
PSP = 0x80000                   # fake PSP, environment, DOS memory (below VGA)
ENV = 0x80100
DOSMEM = 0x81000
VBEBUF = 0x70000                # fake VBE mode list and OEM string
LFB = 0xe0000000
LFB_SIZE = 0x200000
SEL_CODE, SEL_DATA, SEL_PSP, SEL_ENV = 0x08, 0x10, 0x18, 0x20
W, H = 320, 200

TIMER_RET = 0x4022d1            # ret of xmpGetTimer (0x4022b3): EAX = PIT clocks since the music start
FRAME_END = (0x40004f, 0x400077)    # the two returns of the frame routine 0x400000 (32-bit / 15-bit path)
MAIN_FRAME = 0x4001b4           # first instruction after main's `enter`: EBP = main's frame
XMP_DATA = 0x4018d4             # pointer to MXMPlay's data block (read by 0x4018d8)

# main (0x4001ab): call site -> routine, to label the stages before the frame loop (see NOTES.md)
STAGES = {0x4001f0: 0x400ff4, 0x4001f5: 0x414aee, 0x40020e: 0x40196a, 0x400225: 0x416806, 0x40022a: 0x403f10,
          0x40022f: 0x4053f2, 0x400234: 0x40347a, 0x400239: 0x4033ee, 0x400247: 0x40100c, 0x4002a4: 0x4002a4,
          0x4008c2: 0x4033a7, 0x40095e: 0x403f53, 0x400a4f: 0x416cca, 0x400a54: 0x417563, 0x400a59: 0x418402,
          0x400a5e: 0x419373, 0x400a63: 0x419d5b, 0x400a68: 0x40181e, 0x400a7c: 0x401628, 0x400ae2: 0x401457,
          0x400b02: 0x400000, 0x400b09: 0x402129, 0x400b18: 0x400b18, 0x400fc4: 0x40219a}
PARTS = {1: 0x405527, 2: 0x4198bf, 3: 0x405c45, 4: 0x4037c0, 5: 0x403530, 6: 0x406677, 7: 0x416fce, 8: 0x417929,
         9: 0x4185e5, 10: 0x41958e, 11: 0x419dc7, 99: 0x400079}


class Gus:
    """Gravis UltraSound register file (jizz/re/run_jizz.py), with word I/O to 0x3x2 / 0x3x3 / 0x3x4."""
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


def desc(base, limit, access, flags=0xc):
    return struct.pack('<HHBBBB', limit & 0xffff, base & 0xffff, (base >> 16) & 0xff, access,
                       (flags << 4) | ((limit >> 16) & 0xf), (base >> 24) & 0xff)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--ticks', type=int, default=1 << 30, help='stop after this many ticks')
    ap.add_argument('--ips', type=float, default=150e6, help='main-line instructions per second')
    ap.add_argument('--part-ips', default='', help='per-part override: PART=IPS,... (PART = number as in PARTS.md, '
                    'the value of main\'s part variable); applies while that part is active')
    ap.add_argument('--quantum', type=int, default=0, help='instructions per emulation slice (default ips/4000)')
    ap.add_argument('--bpp', type=int, default=32, choices=(15, 32), help='offer 320x200x32 (and 15) or only 15')
    ap.add_argument('--snap-at', default='', help='save snapshots at these ticks (comma-separated)')
    ap.add_argument('--snap-parts', action='store_true', help='save a snapshot at the first tick after each part '
                    'change (and when the precalc starts the frame loop)')
    ap.add_argument('--snapdir', default=os.path.join(HERE, 'snaps'))
    ap.add_argument('--snap', help='snapshot file to resume from (with --load)')
    ap.add_argument('--load', action='store_true', help='resume from --snap')
    ap.add_argument('--freeze-timer', action='store_true', help='analysis: xmpGetTimer returns the value of its first '
                    'call in this run every time, so whatever still moves is stepped per frame')
    ap.add_argument('--trace', default='', help='comma-separated addresses: print the tick the first time each runs')
    ap.add_argument('--no-frames', action='store_true', help='do not write .frames (faster analysis runs)')
    ap.add_argument('--dump-draws', default='', help='engine: dump the state at these calls of 0x4169f8 (call '
                    'numbers counted from the start of the run, comma-separated) to --dumpdir')
    ap.add_argument('--dumpdir', default=os.path.join(HERE, 'dump', 'engine'))
    ap.add_argument('--flip-frames', action='store_true', help='one .frames record per shown frame, not per tick')
    a = ap.parse_args()

    part_ips = {}
    for item in filter(None, a.part_ips.split(',')):
        k, v = item.split('=')
        part_ips[int(k, 0)] = float(v)

    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(0, RAM)
    mu.mem_map(LFB, LFB_SIZE)
    mu.mem_write(BASE, IMG)
    psp = bytearray(256)
    struct.pack_into('<H', psp, 0x2c, SEL_ENV)
    psp[0x80] = 0
    psp[0x81] = 0x0d
    mu.mem_write(PSP, bytes(psp))
    mu.mem_write(ENV, b'PATH=C:\\\x00ULTRASND=240,3,3,7,7\x00\x00\x01\x00C:\\CLONE.EXE\x00')
    mu.mem_write(IRET, b'\xcf')
    mu.mem_write(EXIT, b'\x66\xb8\x00\x4c\xcd\x21')          # mov ax, 4c00h; int 21h
    gdt = bytearray(64 * 8)
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
    flog.write('# flip tick time_s timer t70 part order row  (timer = first xmpGetTimer value read while drawing this '
               'frame, t70 = timer*70/1193182)\n')
    gus = Gus(glog)
    # st: everything that goes into a snapshot
    st = {'tick': 0, 'now': 0.0, 'next_irq': float(0x10000), 'pit_count': 0x10000, 'pit_start': 0.0,
          'pit_w': 0, 'pit_lo': 0, 'latch': None, 'latch_n': 0, 'vec8': None, 'dos': DOSMEM, 'sels': {},
          'gfx': False, 'mode': 3, 'bits': 32, 'pitch': 1280, 'disp': 0, 'flips': 0, 'stage': 0, 'main_ebp': None,
          'part': 0, 'ftimer': None, 'stop': False, 'text': '', 'snapped_part': None, 'frozen': None}
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

    def pit_counter():
        c = st['pit_count']
        el = int(st['now'] - st['pit_start']) % c
        return (c - el) & 0xffff

    def cstr(addr, end=b'$', n=400):
        return bytes(mu.mem_read(addr, n)).split(end)[0].decode('latin1')

    def text(s):
        st['text'] += s
        while '\n' in st['text'] or '\r' in st['text']:
            i = min(x for x in (st['text'].find('\n'), st['text'].find('\r')) if x >= 0)
            line, st['text'] = st['text'][:i], st['text'][i + 1:]
            if line.strip():
                print('text: %s' % line.rstrip()[-200:], flush=True)

    def sel_base(sel):
        if sel in (SEL_CODE, SEL_DATA):
            return 0
        if sel == SEL_PSP:
            return PSP
        if sel == SEL_ENV:
            return ENV
        return st['sels'].get(sel, (0, 0))[0]

    # ---- VESA (real-mode int 10h AX=4Fxx through int 31h 0300) --------------------------------------------------
    modes = [0x10f, 0x10d] if a.bpp == 32 else [0x10d]

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
            mu.mem_write(VBEBUF, ml + bytes(0x100 - len(ml)))
            mu.mem_write(VBEBUF + 0x100, b'run_clone VBE\x00')
            r['eax'] = 0x4f
        elif ax == 0x4f01:
            m = r['ecx'] & 0x1ff
            if m not in modes:
                r['eax'] = 0x014f
                return
            b = bytearray(256)
            bits = 32 if m == 0x10f else 15
            pitch = 1280 if bits == 32 else 640
            struct.pack_into('<HBBHHHHIH', b, 0, 0x9b, 7, 0, 64, 64, 0xa000, 0, 0, pitch)
            struct.pack_into('<HHBBBBBBBBB', b, 0x12, W, H, 8, 8, 1, bits, 1, 6, 0, LFB_SIZE // (pitch * H) - 1, 1)
            if bits == 32:
                b[0x1f:0x27] = bytes((8, 16, 8, 8, 8, 0, 8, 24))
            else:
                b[0x1f:0x27] = bytes((5, 10, 5, 5, 5, 0, 1, 15))
            struct.pack_into('<I', b, 0x28, LFB)
            mu.mem_write(lin, bytes(b))
            r['eax'] = 0x4f
        elif ax == 0x4f02:
            st['mode'] = r['ebx'] & 0xffff
            m = st['mode'] & 0x1ff
            st['gfx'] = m in modes
            if st['gfx']:
                st['bits'] = 32 if m == 0x10f else 15
                st['pitch'] = 1280 if st['bits'] == 32 else 640
            st['disp'] = 0
            print('VBE set mode %#x (tick %d)' % (st['mode'], st['tick']), flush=True)
            r['eax'] = 0x4f
        elif ax == 0x4f07:
            bl = r['ebx'] & 0xff
            if bl in (0, 0x80):
                st['disp'] = (r['edx'] & 0xffff) * st['pitch'] + (r['ecx'] & 0xffff) * (st['bits'] + 1) // 8
            else:
                r['ecx'] = (st['disp'] % st['pitch']) * 8 // (st['bits'] + 1)
                r['edx'] = st['disp'] // st['pitch']
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
        count('int10/%02x' % ah)
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

    RMS = (('edi', 0, 4), ('esi', 4, 4), ('ebp', 8, 4), ('ebx', 0x10, 4), ('edx', 0x14, 4), ('ecx', 0x18, 4),
           ('eax', 0x1c, 4), ('flags', 0x20, 2), ('es', 0x22, 2), ('ds', 0x24, 2))

    def int31():
        eax = reg(UC_X86_REG_EAX)
        ax = eax & 0xffff
        count('int31/%04x' % ax)
        if ax == 0x0100:
            paras = reg(UC_X86_REG_EBX) & 0xffff
            lin = st['dos']
            st['dos'] += (paras * 16 + 0xff) & ~0xff
            sel = 0x28 + 8 * len(st['sels'])
            st['sels'][sel] = (lin, paras * 16)
            mu.mem_write(GDT + sel, desc(lin, paras * 16 - 1, 0x92, 4))
            mu.reg_write(UC_X86_REG_EAX, (eax & ~0xffff) | (lin >> 4))
            mu.reg_write(UC_X86_REG_EDX, (reg(UC_X86_REG_EDX) & ~0xffff) | sel)
            print('DPMI DOS memory %d paragraphs -> segment %#x selector %#x' % (paras, lin >> 4, sel), flush=True)
            setcf(0)
        elif ax == 0x0101:
            setcf(0)
        elif ax == 0x0300:
            edi = sel_base(reg(UC_X86_REG_ES)) + reg(UC_X86_REG_EDI)
            blk = bytes(mu.mem_read(edi, 0x32))
            r = {k: int.from_bytes(blk[o:o + n], 'little') for k, o, n in RMS}
            intno = reg(UC_X86_REG_EBX) & 0xff
            if intno == 0x10:
                int10(r)
            else:
                print('real-mode int %x ax=%04x unhandled' % (intno, r['eax'] & 0xffff), flush=True)
            b = bytearray(blk)
            for k, o, n in RMS:
                b[o:o + n] = (r[k] & ((1 << (8 * n)) - 1)).to_bytes(n, 'little')
            mu.mem_write(edi, bytes(b))
            setcf(0)
        elif ax == 0x0800:
            phys = (reg(UC_X86_REG_EBX) & 0xffff) << 16 | reg(UC_X86_REG_ECX) & 0xffff
            size = (reg(UC_X86_REG_ESI) & 0xffff) << 16 | reg(UC_X86_REG_EDI) & 0xffff
            print('DPMI map physical %#x size %#x' % (phys, size), flush=True)
            setcf(0)                            # identity: BX:CX unchanged
        else:
            print('int 31h ax=%04x unhandled at %x' % (ax, reg(UC_X86_REG_EIP)), flush=True)
            setcf(1)

    def int21():
        eax = reg(UC_X86_REG_EAX)
        ah = (eax >> 8) & 0xff
        ax = eax & 0xffff
        edx = reg(UC_X86_REG_EDX)
        count('int21/%02x' % ah)
        if ah == 0x09:
            text(cstr(sel_base(reg(UC_X86_REG_DS)) + edx))
        elif ah == 0x4c:
            print('exit code %d at tick %d (time %.2f s)' % (eax & 0xff, st['tick'], st['now'] / PIT_HZ), flush=True)
            st['stop'] = True
            mu.emu_stop()
        elif ax == 0x3508:
            mu.reg_write(UC_X86_REG_ES, SEL_CODE)
            mu.reg_write(UC_X86_REG_EBX, IRET if st['vec8'] is None else st['vec8'])
        elif ax == 0x2508:
            st['vec8'] = None if edx == IRET else edx
            print('set PM vector 8 -> %x (tick %d, time %.2f s)' % (edx, st['tick'], st['now'] / PIT_HZ),
                  flush=True)
        else:
            print('int 21h ax=%04x unhandled at %x' % (ax, reg(UC_X86_REG_EIP)), flush=True)
            setcf(1)

    def on_intr(mu, intno, _):
        if intno == 0x31:
            int31()
        elif intno == 0x21:
            int21()
        elif intno == 0x16:
            ah = (reg(UC_X86_REG_EAX) >> 8) & 0xff
            count('int16/%02x' % ah)
            if ah in (0x01, 0x11):
                setflag(0x40, 1)                # ZF = 1: no key
            else:
                mu.reg_write(UC_X86_REG_EAX, reg(UC_X86_REG_EAX) & ~0xffff)
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
        elif 0x240 <= port < 0x350:
            gus.out(port, size, val)

    def on_stage(mu, addr, size, _):
        if addr == MAIN_FRAME and st['main_ebp'] is None:
            st['main_ebp'] = reg(UC_X86_REG_EBP)
        p = STAGES.get(addr)
        if p is not None and p != st['stage']:
            st['stage'] = p
            print('main stage %x at tick %d (time %.2f s)' % (p, st['tick'], st['now'] / PIT_HZ), flush=True)

    def on_timer(mu, addr, size, _):
        if a.freeze_timer:
            if st['frozen'] is None:
                st['frozen'] = reg(UC_X86_REG_EAX)
            mu.reg_write(UC_X86_REG_EAX, st['frozen'])
        if st['ftimer'] is None:
            v = reg(UC_X86_REG_EAX)
            st['ftimer'] = v - (1 << 32) if v >= 1 << 31 else v

    frames = None if a.no_frames else open(a.out + '.frames', 'wb')

    def visible():
        """The picture on screen as 320x200 RGB bytes."""
        if not st['gfx']:
            return bytes(W * H * 3)
        if st['bits'] == 32:
            px = np.frombuffer(bytes(mu.mem_read(LFB + st['disp'], W * H * 4)), np.uint8).reshape(H, W, 4)
            return px[:, :, 2::-1].tobytes()
        px = np.frombuffer(bytes(mu.mem_read(LFB + st['disp'], W * H * 2)), '<u2').reshape(H, W).astype(np.uint32)
        rgb = np.stack([(px >> 10) & 31, (px >> 5) & 31, px & 31], axis=-1) * 255 // 31
        return rgb.astype(np.uint8).tobytes()

    def music_pos():
        xmp = struct.unpack('<I', mu.mem_read(XMP_DATA, 4))[0]
        if st['vec8'] is None and not st['flips']:
            return -1, -1, 0
        row = mu.mem_read(xmp + 6, 1)[0]
        order = mu.mem_read(xmp + 0x12, 1)[0]
        t = struct.unpack('<ii', mu.mem_read(xmp + 0x35, 8))
        return order, row, (t[0] + t[1]) & 0xffffffff

    def cur_part():
        if st['main_ebp'] is None:
            return 0
        return struct.unpack('<i', mu.mem_read(st['main_ebp'] - 0x14, 4))[0]

    def on_frame(mu, addr, size, _):
        st['flips'] += 1
        order, row, _ = music_pos()
        ft = st['ftimer']
        flog.write('%d %d %.5f %d %.4f %d %d %d\n' % (
            st['flips'], st['tick'], st['now'] / PIT_HZ, -(1 << 40) if ft is None else ft,
            0.0 if ft is None else ft * 70.0 / PIT_HZ, cur_part(), order, row))
        st['ftimer'] = None
        if frames is not None and a.flip_frames:
            frames.write(visible())

    traced = set()
    def on_trace(mu, addr, size, _):
        if addr not in traced:
            traced.add(addr)
            print('trace %x tick %d time %.3f' % (addr, st['tick'], st['now'] / PIT_HZ), flush=True)

    dump_calls = {int(x) for x in a.dump_draws.split(',') if x}
    dstate = {'n': 0, 'cur': None}

    def on_draw(mu, addr, size, _):
        if addr == 0x4169f8:
            n = dstate['n']
            dstate['n'] += 1
            if n in dump_calls:
                tgt = reg(UC_X86_REG_EAX)
                order, row, timer = music_pos()
                dstate['cur'] = dict(call=n, tick=st['tick'], flips=st['flips'], target=tgt, part=cur_part(),
                                     timer=timer, ftimer=st['ftimer'] if st['ftimer'] is not None else 0,
                                     mem=np.frombuffer(bytes(mu.mem_read(0x400000, 0xf96000 - 0x400000)), np.uint8),
                                     before=np.frombuffer(bytes(mu.mem_read(tgt, 0x3e800)), np.uint8))
        elif dstate['cur'] is not None:          # 0x416b5c: leave of 0x4169f8
            d = dstate['cur']
            d['after'] = np.frombuffer(bytes(mu.mem_read(d['target'], 0x3e800)), np.uint8)
            os.makedirs(a.dumpdir, exist_ok=True)
            path = os.path.join(a.dumpdir, 'draw_t%d_c%d.npz' % (d['tick'], d['call']))
            np.savez_compressed(path, **d)
            print('dumped %s (part %d, flips %d)' % (path, d['part'], d['flips']), flush=True)
            dstate['cur'] = None

    if dump_calls:
        mu.hook_add(UC_HOOK_CODE, on_draw, None, 0x4169f8, 0x4169f8)
        mu.hook_add(UC_HOOK_CODE, on_draw, None, 0x416b5c, 0x416b5c)

    for x in filter(None, a.trace.split(',')):
        mu.hook_add(UC_HOOK_CODE, on_trace, None, int(x, 16), int(x, 16))
    # Few code hooks: with emu_start(count=...) unicorn checks every code hook's range on every instruction.
    mu.hook_add(UC_HOOK_CODE, on_stage, None, MAIN_FRAME, max(STAGES))
    mu.hook_add(UC_HOOK_CODE, on_timer, None, TIMER_RET, TIMER_RET)
    for x in FRAME_END:
        mu.hook_add(UC_HOOK_CODE, on_frame, None, x, x)
    mu.hook_add(UC_HOOK_INSN, on_in, None, 1, 0, UC_X86_INS_IN)
    mu.hook_add(UC_HOOK_INSN, on_out, None, 1, 0, UC_X86_INS_OUT)
    mu.hook_add(UC_HOOK_INTR, on_intr)

    eip = ENTRY
    if a.load:
        snap = pickle.load(open(a.snap, 'rb'))
        mu.mem_write(0, snap['mem'])
        mu.mem_write(LFB, snap['lfb'])
        mu.context_restore(snap['ctx'])
        st.update(snap['st'])
        gus.__dict__.update(snap['gus'])
        gus.log = glog
        eip = snap['eip']
        print('resumed at tick %d (flips %d)' % (st['tick'], st['flips']))
        st['flip0'] = st['flips']

    if frames is not None:
        frames.write((b'CLONFLP1' if a.flip_frames else b'CLONFRM1') + struct.pack('<IIIIII', W, H, 24, W * H * 3, 32,
                                                                                   a.bpp))
    ticks = open(a.out + '.ticks', 'w')
    ticks.write('# tick pit_count time_s order row timer part sync_index flips stage eip\n')
    snap_ticks = {int(x) for x in a.snap_at.split(',') if x}
    if a.load:
        flog.write('# resumed from %s: first record = flip %d\n' % (a.snap, st['flips'] + 1))

    def ips():
        return part_ips.get(cur_part(), a.ips)

    def deliver_irq():
        ctx = mu.context_save()
        esp = reg(UC_X86_REG_ESP) - 12
        mu.mem_write(esp, struct.pack('<III', SENTINEL, SEL_CODE, 0x202))
        mu.reg_write(UC_X86_REG_ESP, esp)
        try:
            mu.emu_start(st['vec8'], SENTINEL, count=50_000_000)
        except UcError as e:
            print('error in irq at %x: %s' % (reg(UC_X86_REG_EIP), e), flush=True)
            st['stop'] = True
        mu.context_restore(ctx)

    def save_snap(name, eip):
        os.makedirs(a.snapdir, exist_ok=True)
        g = {k: v for k, v in gus.__dict__.items() if k != 'log'}
        path = os.path.join(a.snapdir, name)
        pickle.dump({'mem': bytes(mu.mem_read(0, RAM)), 'lfb': bytes(mu.mem_read(LFB, LFB_SIZE)),
                     'ctx': mu.context_save(), 'st': dict(st), 'gus': g, 'eip': eip}, open(path, 'wb'))
        print('snapshot %s at tick %d (part %d, flips %d)' % (path, st['tick'], cur_part(), st['flips']), flush=True)

    def record(eip):
        t = st['tick']
        order, row, timer = music_pos()
        si = struct.unpack('<I', mu.mem_read(0x420495, 4))[0]
        ticks.write('%d %d %.5f %d %d %d %d %d %d %x %x\n' % (
            t, st['pit_count'], st['now'] / PIT_HZ, order, row, timer - (1 << 32) if timer >= 1 << 31 else timer,
            cur_part(), si, st['flips'], st['stage'], eip))
        if frames is not None and not a.flip_frames:
            frames.write(visible())

    while not st['stop'] and st['tick'] < a.ticks:
        if st['now'] >= st['next_irq'] - 1e-6:
            # timer tick: deliver IRQ0 (if the intro's handler is installed) and record
            t = st['tick']
            gus.tick = t
            p = cur_part()
            if t in snap_ticks or (a.snap_parts and st['stage'] == 0x400b18 and p != st['snapped_part']):
                st['snapped_part'] = p
                save_snap('t%d.snap' % t, eip)
            st['now'] = st['next_irq']
            st['pit_start'] = st['now']
            st['next_irq'] = st['now'] + st['pit_count']     # mode 2 reload; the handler may reprogram
            if st['vec8'] is not None:
                deliver_irq()
            record(eip)
            st['tick'] += 1
            if t % 1000 == 0:
                order, row, timer = music_pos()
                print('tick %d time %.2f part %d pos %02x:%02x flips %d eip %x' % (
                    t, st['now'] / PIT_HZ, p, order & 0xff, row & 0xff, st['flips'], eip), flush=True)
            continue
        rate = ips()
        q = a.quantum or max(1000, int(a.ips // 4000))
        n = int(min(q, (st['next_irq'] - st['now']) * rate / PIT_HZ + 1))
        try:
            mu.emu_start(eip, 0xffffffff, count=n)
        except UcError as e:
            print('error in main at %x: %s' % (reg(UC_X86_REG_EIP), e), flush=True)
            break
        eip = reg(UC_X86_REG_EIP)
        st['now'] += n * PIT_HZ / rate
    open(a.out + '.dram', 'wb').write(bytes(gus.dram))
    glog.close()
    flog.close()
    print('ended at tick %d, time %.2f s, flips %d' % (st['tick'], st['now'] / PIT_HZ, st['flips']))
    print('out ports', {hex(k): v for k, v in sorted(outs.items())})
    print('in ports', dict(sorted(ins.items())))
    print('services', dict(sorted(services.items())))
    print('GUS register reads', {hex(k): v for k, v in sorted(gus.reads.items())})
    print('text output:', repr(st['text']))


if __name__ == '__main__':
    main()

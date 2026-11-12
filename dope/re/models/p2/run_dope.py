"""Run DOPE headless in unicorn and record what it does.

DOPE.EXE = Tran's PMODE 3.x + a 32-bit program (the "loader": GUS ProTracker player + main) + CDPI v0.2, a 16-bit
chain loader that loads the seven appended real-mode parts from DOPE.EXE one after the other and runs them in real
mode while the music keeps playing from the GUS timer interrupt in protected mode (see NOTES.md).

No PMODE kernel is run. Two unicorn CPUs share one 16 MB memory:
  rm  real mode (UC_MODE_16): CDPI and the parts, with a fake BIOS/DOS. Every interrupt goes through the real-mode
      IVT; vectors that point into the fake BIOS (F000:N*16) are serviced in Python.
  pm  32-bit protected mode: PMODE's code32 segment of the loader (base code32a, flat 4 GB selectors 08h/10h, 18h =
      base 0, like PMODE's selcode/seldata/selzero). It starts at PMODE's 32-bit entry (code32 0x20d). PMODE's
      services are emulated: int 33h (real-mode interrupt with the register image at code32 0x84), int 32h (real-mode
      far call to CX:DX: runs on rm until it returns), int 31h 090x (interrupt flag), setirqvect / getirqvect
      (code32 0x9db / 0x9b5) are intercepted, the exit (0x355) ends the run.
The main line runs on one CPU at a time: pm until the loader calls CDPI with int 32h, then rm (all parts), then pm.
IRQ 7 (the GUS, ULTRASND=240,3,3,7,7) runs the player's handler on pm, on its own stack, whenever the main line has
interrupts enabled; it takes no emulated time. IRQ 0 (PIT) goes through the real-mode IVT on rm (the parts hook it).

Time: emulated seconds. The main line runs --mips million instructions per second (default 100) in quanta.
Polling the VGA status port without progress fast-forwards to the next change of the retrace bits; I/O reads see
the time at the start of the quantum plus what was fast-forwarded (error < one quantum, 0.2 ms by default).

Faked hardware:
  VGA      registers (misc, sequencer, CRTC, graphics, attribute, DAC), 256 KB planar memory. Chain-4 modes use plain
           RAM at A0000; unchained modes (mode X) use an MMIO region with write modes 0-3 and latches. Refresh timing
           from the CRTC/misc registers (mode 13h: 70.09 Hz, 480-line modes 59.94 Hz). Status bit 3 = vertical
           retrace, bit 0 = vertical blank (no horizontal timing).
  PIT      channel 0, modes 0/2/3 (mode 3 counts down by 2), latches.
  PIC      masks only (no in-service tracking).
  GUS      at 0x240, IRQ 7: register file, 1 MB DRAM, timers 1/2 (AdLib-style control at 2x8/2x9), voice position and
           volume ramp simulation (the player switches samples in the ramp-end IRQ), IRQ status 2x6, IRQ source 0x8F.
  keyboard none pressed; mouse driver none.

Outputs (prefix --out), one record per displayed video frame (the natural clock: the parts are synced to the
vertical retrace; the music runs from the GUS timer, see the .ticks music columns):
  <out>.frames  header 32 bytes: b'DOPEFRM1', <IIIIII: max width W, max height H, record bytes, header bytes, 0, 0>;
                then per frame: <HHHH width, height, flags (1 = graphics), 0>, 768 bytes palette (6-bit DAC values),
                W*H bytes of pixel indices (the visible width x height in the top left, rest 0)
  <out>.ticks   per frame: frame, time (s) at the frame start, music order, row, speed counter, music tick, part,
                start address used, page flips (CRTC start writes) so far, key checks so far (int 16h AH=1: the
                parts p2-p7 make one per drawn frame), mode (WxH, c4 = chain-4 / x = unchained), main CPU:IP
  <out>.gus     GUS register / port writes: `<music tick> v<voice> r<reg> <value>` / `<music tick> p<port> <value>`,
                plus `<music tick> irq <status> <time>` before each handler run (t = timer, r = ramp, w = wave);
                music tick = GUS timer-2 interrupts served by the player so far (the handler serves a pending
                ramp IRQ first; the timer then comes in the next run)
  <out>.dram    final GUS DRAM (1 MB)
  <out>.log     (stdout) events: parts, modes, DOS calls

usage: python3 run_dope.py --out full --snap-parts [--mips 100] [--frames N] [--snap-at F1,F2]
       python3 run_dope.py --load snaps/part_p3.snap --out x [--stop-part p4]   (one part; capture.py runs all of
                                                                             them in parallel)
"""
import argparse
import ctypes
import os
import pickle
import struct
import sys

import numpy as np
from unicorn import (Uc, UcError, UC_ARCH_X86, UC_MODE_16, UC_MODE_32, UC_PROT_ALL, UC_HOOK_INTR, UC_HOOK_CODE,
                     UC_HOOK_INSN)
from unicorn.x86_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
# dope/re (the folder with unpacked/), also when this script is a copy in dope/re/models/<scope>/
RE = next((d for d in (HERE, os.path.dirname(HERE), os.path.dirname(os.path.dirname(HERE)))
           if os.path.exists(os.path.join(d, 'unpacked', 'loader.bin'))), HERE)
ORIG = os.path.join(RE, '..', 'original')
UNP = os.path.join(RE, 'unpacked')

PIT_HZ = 1193182.0
ENVSEG = 0x07f0               # environment block
PSP = 0x0800                  # the loader's PSP; DOS memory arena from here to MEMTOP
LOAD = PSP + 0x10             # load segment of the loader
MEMTOP = 0xA000
RAM = 0x1000000               # 16 MB shared memory
XMS = 0x110000                # PMODE's extended memory ("himem") from here
C16 = LOAD * 16               # code16a
C32 = (LOAD + 0x17e) * 16     # code32a (PMODE's 32-bit segment base)
BIOS = 0xF0000                # fake BIOS: F000:N*16 = service stub of vector N ('nop; iret', int 8: 'nop; int 1Ch; iret')
RMRET = (0xF000, 0x0FF0)      # return address of int 32h calls (linear 0xF0FF0)
GDT = 0xF8000
PM_STACK = 0xFC000            # pm main stack top (linear)
PM_ISTACK = 0xFE000           # pm IRQ stack top (linear)
PM_SENT = 0xFF000             # pm IRQ return sentinel (linear)
RM_STACK = (LOAD + 0x7f9f, 0x0100)   # real-mode stack for the int 32h call (the loader's own 16-bit stack)
V86R = 0x84                   # PMODE's real-mode register image (DPMI layout) in code32
PM_ENTRY = 0x20d
PM_SETIRQ, PM_GETIRQ, PM_EXIT = 0x9db, 0x9b5, 0x355
CDPI_SEG = LOAD + 0xeb
CDPI_LAUNCH = CDPI_SEG * 16 + 0x3bc     # CDPI's `retf` into a part
PLAYER = {'order': 0x1aa0, 'row': 0x1a9c, 'count': 0x1a9b, 'speed': 0xc76}   # player variables (code32 bytes)
# CDPI's [6] (file offset of the next executable) at the moment it starts a part -> the part
EXE_PARTS = {0xf64c: 'p1', 0x1dbfa: 'p2', 0x2ad50: 'p3', 0x56f22: 'p4', 0x68d44: 'p5', 0x7bbe2: 'p6',
             0x8e990: 'p7', 0x8e9c5: 'term'}

FW, FH = 320, 240             # frame record size (largest mode used)
REC = 8 + 768 + FW * FH


# ---- VGA -----------------------------------------------------------------------------------------------------------

MODE13 = dict(misc=0x63, seq=[0x03, 0x01, 0x0f, 0x00, 0x0e],
              crtc=[0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0xbf, 0x1f, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x9c, 0x8e, 0x8f, 0x28, 0x40, 0x96, 0xb9, 0xa3, 0xff],
              gc=[0, 0, 0, 0, 0, 0x40, 0x05, 0x0f, 0xff],
              ac=list(range(16)) + [0x41, 0x00, 0x0f, 0x00, 0x00])
MODE3 = dict(misc=0x67, seq=[0x03, 0x00, 0x03, 0x00, 0x02],
             crtc=[0x5f, 0x4f, 0x50, 0x82, 0x55, 0x81, 0xbf, 0x1f, 0x00, 0x4f, 0x0d, 0x0e, 0x00, 0x00, 0x00, 0x00,
                   0x9c, 0x8e, 0x8f, 0x28, 0x1f, 0x96, 0xb9, 0xa3, 0xff],
             gc=[0, 0, 0, 0, 0, 0x10, 0x0e, 0x00, 0xff],
             ac=[0, 1, 2, 3, 4, 5, 0x14, 7, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x0c, 0, 0x0f, 8, 0])


def default_palette():
    """VGA BIOS default DAC (EGA colours, grey ramp, the rest approximated) - only shown if a part never sets one"""
    p = bytearray(768)
    ega = [(0, 0, 0), (0, 0, 42), (0, 42, 0), (0, 42, 42), (42, 0, 0), (42, 0, 42), (42, 21, 0), (42, 42, 42),
           (21, 21, 21), (21, 21, 63), (21, 63, 21), (21, 63, 63), (63, 21, 21), (63, 21, 63), (63, 63, 21),
           (63, 63, 63)]
    for i, c in enumerate(ega):
        p[i * 3:i * 3 + 3] = bytes(c)
    for i in range(16):
        g = [0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63][i]
        p[(16 + i) * 3:(16 + i) * 3 + 3] = bytes((g, g, g))
    return p


class Vga:
    def __init__(self):
        self.planes = np.zeros((4, 0x10000), np.uint8)
        self.latch = [0, 0, 0, 0]
        self.misc = 0x67
        self.seq = list(MODE3['seq']) + [0] * 3
        self.crtc = list(MODE3['crtc']) + [0] * 7
        self.gc = list(MODE3['gc'])
        self.ac = list(MODE3['ac'])
        self.seq_i = self.crtc_i = self.gc_i = self.ac_i = 0
        self.ac_ff = 0
        self.dac = default_palette()
        self.dac_w = self.dac_r = 0
        self.graphics = False
        self.chain4_ram = True        # A0000-AFFFF is plain RAM (chain-4) or MMIO (planar)
        self.remap = False            # a chain-4 change waits for the end of the quantum
        self.start_writes = 0
        self.latched_start = 0
        self.mode_name = 'text'

    # registers
    def start_addr(self):
        return self.crtc[0x0c] << 8 | self.crtc[0x0d]

    def chain4(self):
        return bool(self.seq[4] & 8)

    def timing(self):
        clock = 28.322e6 if (self.misc >> 2) & 3 == 1 else 25.175e6
        charw = 8 if self.seq[1] & 1 else 9
        htotal = self.crtc[0] + 5
        c7 = self.crtc[7]
        vtotal = (self.crtc[6] | (c7 & 1) << 8 | (c7 & 0x20) << 4) + 2
        vrs = self.crtc[0x10] | (c7 & 4) << 6 | (c7 & 0x80) << 2
        vrlen = ((self.crtc[0x11] & 0xf) - vrs) & 0xf or 16
        vde = (self.crtc[0x12] | (c7 & 2) << 7 | (c7 & 0x40) << 3) + 1
        lp = htotal * charw / clock
        return lp, vtotal, vrs, vrlen, vde

    def geometry(self):
        """visible width, height (in pixels of the 256-colour mode) and the line repeat"""
        width = (self.crtc[1] + 1) * 4
        lp, vtotal, vrs, vrlen, vde = self.timing()
        rep = (self.crtc[9] & 0x1f) + 1
        if self.crtc[9] & 0x80:
            rep *= 2
        return width, vde // rep, rep

    def render(self, ram):
        """the visible frame as (h, w) uint8 indices; ram = A0000-AFFFF bytes when chain-4 RAM is mapped"""
        width, height, rep = self.geometry()
        width, height = min(width, FW), min(height, FH)
        shift = 2 if self.crtc[0x14] & 0x40 else (0 if self.crtc[0x17] & 0x40 else 1)
        offset = self.crtc[0x13] * 2
        start = self.latched_start
        c7, c9 = self.crtc[7], self.crtc[9]
        lc = self.crtc[0x18] | (c7 & 0x10) << 4 | (c9 & 0x40) << 3          # line compare (scan lines)
        pan = (self.ac[0x13] & 7) >> 1
        ys = np.arange(height)
        split = ys * rep > lc
        rowstart = np.where(split, (ys * rep - (lc + 1)) // rep * offset, start + ys * offset)
        # pel panning (in pixels); AC 10h bit 5: no panning below the split
        xs = np.arange(width)[None, :] + np.where(split & bool(self.ac[0x10] & 0x20), 0, pan)[:, None]
        cnt = (rowstart[:, None] + (xs >> 2)) & 0xffff
        addr = (cnt << shift) & 0xffff
        if self.chain4_ram:                         # plane p, offset o = linear (o & 0xfffc) | p
            return np.frombuffer(ram, np.uint8)[(addr & 0xfffc) | (xs & 3)]
        return self.planes[xs & 3, addr]

    # memory (planar)
    def read(self, off, size):
        v = 0
        for i in range(size):
            o = (off + i) & 0xffff
            pl = self.planes
            self.latch = [int(pl[0, o]), int(pl[1, o]), int(pl[2, o]), int(pl[3, o])]
            if self.gc[5] & 8:                                  # read mode 1: colour compare
                cc, dc = self.gc[2], self.gc[7]
                b = 0xff
                for p in range(4):
                    if dc >> p & 1:
                        b &= ~(self.latch[p] ^ (0xff if cc >> p & 1 else 0))
                b &= 0xff
            else:
                b = self.latch[self.gc[4] & 3]
            v |= b << (8 * i)
        return v

    def write(self, off, size, val):
        mask = self.seq[2] & 0xf
        gc = self.gc
        wm = gc[5] & 3
        fast = wm == 0 and gc[1] == 0 and gc[3] == 0 and gc[8] == 0xff
        pl = self.planes
        for i in range(size):
            o = (off + i) & 0xffff
            b = (val >> (8 * i)) & 0xff
            if fast:
                for p in range(4):
                    if mask >> p & 1:
                        pl[p, o] = b
                continue
            if wm == 1:
                for p in range(4):
                    if mask >> p & 1:
                        pl[p, o] = self.latch[p]
                continue
            rot = gc[3] & 7
            b = ((b >> rot) | (b << (8 - rot))) & 0xff
            bm = gc[8]
            if wm == 2:
                data = [0xff if b >> p & 1 else 0 for p in range(4)]
            elif wm == 3:
                bm &= b
                data = [0xff if gc[0] >> p & 1 else 0 for p in range(4)]
            else:
                data = [(0xff if gc[0] >> p & 1 else 0) if gc[1] >> p & 1 else b for p in range(4)]
            op = (gc[3] >> 3) & 3
            for p in range(4):
                if not mask >> p & 1:
                    continue
                d, l = data[p], self.latch[p]
                if op == 1:
                    d &= l
                elif op == 2:
                    d |= l
                elif op == 3:
                    d ^= l
                pl[p, o] = (d & bm) | (l & ~bm & 0xff)

    def set_mode(self, m):
        self.misc = m['misc']
        self.seq[:5] = m['seq']
        self.crtc[:25] = m['crtc']
        self.gc[:9] = m['gc']
        self.ac[:21] = m['ac']


# ---- GUS ------------------------------------------------------------------------------------------------------------

V16 = {1, 2, 3, 4, 5, 9, 0x0a, 0x0b}


class Voice:
    def __init__(self):
        self.r = {0: 3, 0x0d: 3, 1: 0, 2: 0, 3: 0, 4: 0, 5: 0, 6: 0, 7: 0, 8: 0, 9: 0, 0x0a: 0, 0x0b: 0, 0x0c: 7}
        self.t = 0.0                  # simulation time of pos / vol
        self.pos = 0.0                # current position, 1/512 samples
        self.vol = 0.0                # 12-bit volume

    def addr(self, hi, lo):
        return float((self.r[hi] & 0x1fff) << 16 | self.r[lo])


class Gus:
    def __init__(self, log, base=0x240):
        self.dram = bytearray(1 << 20)
        self.base = base
        self.voice = 0
        self.reg = 0
        self.lo = 0
        self.addr = 0
        self.v = [Voice() for _ in range(32)]
        self.g = {0x0e: 13, 0x45: 0, 0x46: 0, 0x47: 0, 0x4c: 0}
        self.status = 0               # 2x6 bits: 4 timer 1, 8 timer 2, 0x20 wave, 0x40 ramp
        self.queue = []               # voices with a pending wave / ramp IRQ
        self.adlib_i = 0
        self.t1_run = self.t2_run = False
        self.t1_next = self.t2_next = None
        self.log = log
        self.tick = 0
        self.now = 0.0
        self.reads = {}

    def __getstate__(self):
        d = dict(self.__dict__)
        d.pop('log', None)
        d.pop('v', None)
        return d

    # voice simulation
    def rate(self):
        return 1e6 / (1.619695497 * max(14, (self.g[0x0e] & 0x1f) + 1))

    def sim(self, v, t):
        """advance voice v to time t: position and volume ramp (stops, loops, IRQ flags)"""
        dt = t - v.t
        v.t = t
        if dt <= 0:
            return
        frames = dt * self.rate()
        vn = self.v.index(v)
        c = v.r[0]
        if not c & 3:
            inc = (v.r[1] >> 1) * frames
            st, en = v.addr(2, 3), v.addr(4, 5)
            if c & 0x40:
                v.pos -= inc
                if v.pos <= st:
                    self._vend(v, vn, st, en, True)
            else:
                v.pos += inc
                if v.pos >= en:
                    self._vend(v, vn, st, en, False)
        rc = v.r[0x0d]
        if not rc & 3:
            inc = (v.r[6] & 0x3f) * frames / (1, 8, 64, 512)[v.r[6] >> 6]
            lo, hi = v.r[7] << 4, v.r[8] << 4
            if rc & 0x40:
                v.vol -= inc
                hit = v.vol <= lo
            else:
                v.vol += inc
                hit = v.vol >= hi
            if hit:
                if rc & 8:
                    if rc & 0x10:
                        v.r[0x0d] ^= 0x40
                        v.vol = lo if rc & 0x40 else hi
                    else:
                        v.vol = hi if rc & 0x40 else lo
                else:
                    v.vol = lo if rc & 0x40 else hi
                    v.r[0x0d] |= 1
                if rc & 0x20:
                    v.r[0x0d] |= 0x80
                    if vn not in self.queue:
                        self.queue.append(vn)
            v.vol = min(max(v.vol, 0.0), 4095.0)

    def _vend(self, v, vn, st, en, back):
        c = v.r[0]
        if c & 8:
            span = max(en - st, 1.0)
            if c & 0x10:
                v.r[0] ^= 0x40
                v.pos = st + ((st - v.pos) % span) if back else en - ((v.pos - en) % span)
            else:
                v.pos = en - ((st - v.pos) % span) if back else st + ((v.pos - en) % span)
        else:
            v.pos = st if back else en
            v.r[0] |= 1
        if c & 0x20:
            v.r[0] |= 0x80
            if vn not in self.queue:
                self.queue.append(vn)

    def next_event(self, t):
        """earliest time > t at which a ramp / wave IRQ or a timer fires"""
        best = None
        for c in (self.t1_next if self.t1_run else None, self.t2_next if self.t2_run else None):
            if c is not None and (best is None or c < best):
                best = c
        rate = self.rate()
        for vn, v in enumerate(self.v):
            if v.r[0x0d] & 0x20 and not v.r[0x0d] & 3:
                inc = (v.r[6] & 0x3f) / (1, 8, 64, 512)[v.r[6] >> 6]
                if inc > 0:
                    self.sim(v, t)
                    d = (v.vol - (v.r[7] << 4)) if v.r[0x0d] & 0x40 else ((v.r[8] << 4) - v.vol)
                    c = t + max(d, 0.0) / inc / rate + 1e-7
                    if best is None or c < best:
                        best = c
            if v.r[0] & 0x20 and not v.r[0] & 3 and v.r[1] >> 1:
                self.sim(v, t)
                inc = (v.r[1] >> 1)
                d = (v.pos - v.addr(2, 3)) if v.r[0] & 0x40 else (v.addr(4, 5) - v.pos)
                c = t + max(d, 0.0) / inc / rate + 1e-7
                if best is None or c < best:
                    best = c
        return best

    def advance(self, t):
        """run timers and voices up to time t; True if the IRQ line is (newly) up"""
        self.now = t
        if self.t1_run and self.t1_next is not None:
            while self.t1_next <= t:
                if self.g[0x45] & 4:
                    self.status |= 4
                self.t1_next += (256 - self.g[0x46]) * 80e-6
        if self.t2_run and self.t2_next is not None:
            while self.t2_next <= t:
                if self.g[0x45] & 8:
                    self.status |= 8
                self.t2_next += (256 - self.g[0x47]) * 320e-6
        for v in self.v:
            if (v.r[0x0d] & 0x20 and not v.r[0x0d] & 3) or (v.r[0] & 0x20 and not v.r[0] & 3):
                self.sim(v, t)
        self._irqbits()
        return self.irq()

    def _irqbits(self):
        self.status &= ~0x60
        for vn in self.queue:
            if self.v[vn].r[0] & 0x80:
                self.status |= 0x20
            if self.v[vn].r[0x0d] & 0x80:
                self.status |= 0x40

    def irq(self):
        return bool(self.status & 0x6c)

    # ports
    def out(self, port, size, val):
        p = port - self.base
        if p == 0x102:
            self.voice = val & 31
            if size == 2:
                self.reg = (val >> 8) & 0xff
        elif p == 0x103:
            self.reg = val & 0xff
            if size == 2:
                self.lo = (val >> 8) & 0xff
        elif p == 0x104:
            if size == 2:
                self._write(self.reg, val & 0xffff)
            else:
                self.lo = val & 0xff
        elif p == 0x105:
            r = self.reg
            self._write(r, (self.lo | (val & 0xff) << 8) if (r in V16 or r == 0x43) else val & 0xff)
        elif p == 0x107:
            self.dram[self.addr & 0xfffff] = val & 0xff
        elif p == 0x008:
            self.adlib_i = val & 0xff
        elif p == 0x009:
            if self.adlib_i == 4:
                if val & 0x80:
                    self.status &= ~0x0c
                else:
                    r1, r2 = bool(val & 1), bool(val & 2)
                    if r1 and not self.t1_run:
                        self.t1_next = self.now + (256 - self.g[0x46]) * 80e-6
                    if r2 and not self.t2_run:
                        self.t2_next = self.now + (256 - self.g[0x47]) * 320e-6
                    self.t1_run, self.t2_run = r1, r2
            self.log.write('%d p%03x %x\n' % (self.tick, port, val))
        else:
            self.log.write('%d p%03x %x\n' % (self.tick, port, val))

    def _write(self, reg, val):
        if reg == 0x43:
            self.addr = (self.addr & 0xff0000) | (val & 0xffff)
            return
        if reg == 0x44:
            self.addr = (self.addr & 0xffff) | ((val & 0xff) << 16)
            return
        self.log.write('%d v%d r%02x %x\n' % (self.tick, self.voice, reg, val))
        if reg < 0x20:
            v = self.v[self.voice]
            self.sim(v, self.now)
            if reg == 0:
                if not val & 0x80:
                    v.r[0] &= ~0x80
                v.r[0] = (val & 0x7f) | (v.r[0] & 0x80 if val & 0x80 else 0)
                if not val & 0x20:
                    v.r[0] &= ~0x80
            elif reg == 0x0d:
                v.r[0x0d] = (val & 0x7f) | (v.r[0x0d] & 0x80 if val & 0x80 else 0)
                if not val & 0x20:
                    v.r[0x0d] &= ~0x80
            elif reg == 0x0e:
                self.g[0x0e] = val
            else:
                v.r[reg] = val
                if reg == 9:
                    v.vol = float(val >> 4)
                elif reg in (0x0a, 0x0b):
                    v.pos = v.addr(0x0a, 0x0b)
            if not (v.r[0] & 0x80 or v.r[0x0d] & 0x80) and self.voice in self.queue:
                self.queue.remove(self.voice)
            self._irqbits()
        else:
            self.g[reg] = val
            if reg == 0x45:
                if not val & 4:
                    self.status &= ~4
                if not val & 8:
                    self.status &= ~8
            elif reg == 0x4c and not val & 1:
                pass

    def _read(self, reg):
        if reg == 0x8f:
            if self.queue:
                vn = self.queue.pop(0)
                v = self.v[vn]
                r = vn | 0x20 | (0 if v.r[0] & 0x80 else 0x80) | (0 if v.r[0x0d] & 0x80 else 0x40)
                v.r[0] &= ~0x80
                v.r[0x0d] &= ~0x80
                self._irqbits()
                return r
            return 0xe0
        if 0x80 <= reg < 0x90:
            v = self.v[self.voice]
            self.sim(v, self.now)
            r = reg - 0x80
            if r == 9:
                return int(v.vol) << 4
            if r in (0x0a, 0x0b):
                p = int(v.pos) & 0x1fffffff
                return (p >> 16) & 0x1fff if r == 0x0a else p & 0xffff
            return v.r.get(r, 0)
        if reg in self.g:
            return self.g[reg]
        return 0

    def inp(self, port, size):
        p = port - self.base
        if p == 0x107:
            return self.dram[self.addr & 0xfffff]
        if p in (0x104, 0x105):
            r = self.reg
            self.reads[r] = self.reads.get(r, 0) + 1
            v = self._read(r)
            if size == 2:
                return v & 0xffff
            if p == 0x105:
                rr = r - 0x80
                return (v >> 8) & 0xff if (rr in V16) else v & 0xff
            return v & 0xff
        if p == 0x006:
            return self.status & 0xec
        if p == 0x008:
            s = 0
            if self.status & 4:
                s |= 0xc0
            if self.status & 8:
                s |= 0xa0
            return s
        if p == 0x103:
            return self.reg
        if p == 0x102:
            return self.voice
        return 0xff


# ---- PIT ------------------------------------------------------------------------------------------------------------

class Pit:
    """PIT channel 0. next_t = time of the next IRQ (rising OUT edge): every `count` clocks in modes 2 and 3, once
    in mode 0. The counter read back counts down by 1 (modes 0, 2) or by 2 twice per period (mode 3)."""
    def __init__(self):
        self.mode = 3
        self.count = 0x10000
        self.t0 = 0.0                 # time the count was loaded
        self.run = True
        self.access = 3
        self.wstate = 0
        self.lo = 0
        self.latch = None
        self.rstate = 0
        self.next_t = self.count / PIT_HZ

    def counter(self, t):
        if not self.run:
            return self.count & 0xffff
        el = int((t - self.t0) * PIT_HZ)
        c = self.count
        if self.mode == 0:
            return (c - el) & 0xffff
        if self.mode == 3:
            half = max(c // 2, 1)
            return (c - 2 * (el % half)) & 0xffff
        return (c - el % c) & 0xffff

    def fire(self):
        """the IRQ at next_t happened: schedule the next one"""
        if self.mode == 0:
            self.next_t = None
        else:
            self.next_t += self.count / PIT_HZ

    def write43(self, val, t):
        ch = val >> 6
        if ch != 0:
            return
        acc = (val >> 4) & 3
        if acc == 0:
            self.latch = self.counter(t)
            self.rstate = 0
            return
        self.access = acc
        self.mode = (val >> 1) & 7
        if self.mode > 5:
            self.mode -= 4
        self.wstate = 0
        if self.mode == 0:
            self.run = False
            self.next_t = None

    def write40(self, val, t):
        if self.access == 1:
            self._load(val, t)
        elif self.access == 2:
            self._load(val << 8, t)
        elif self.wstate == 0:
            self.lo = val
            self.wstate = 1
        else:
            self.wstate = 0
            self._load(self.lo | val << 8, t)

    def _load(self, c, t):
        self.count = c or 0x10000
        self.t0 = t
        self.run = True
        self.next_t = t + self.count / PIT_HZ

    def read40(self, t):
        v = self.latch if self.latch is not None else self.counter(t)
        if self.access == 3:
            b = (v >> (8 * self.rstate)) & 0xff
            self.rstate ^= 1
            if self.rstate == 0:
                self.latch = None
        else:
            b = (v >> (8 if self.access == 2 else 0)) & 0xff
            self.latch = None
        return b


# ---- DOS memory -----------------------------------------------------------------------------------------------------

class Arena:
    """DOS memory blocks: [mcb segment, size in paragraphs, owner (0 = free)]; block segment = mcb + 1"""
    def __init__(self):
        self.blocks = [[PSP - 1, MEMTOP - PSP, PSP]]

    def alloc(self, n, owner):
        best = 0
        for i, b in enumerate(self.blocks):
            if b[2] == 0:
                best = max(best, b[1])
                if b[1] >= n:
                    if b[1] > n:
                        self.blocks.insert(i + 1, [b[0] + 1 + n, b[1] - n - 1, 0])
                    b[1], b[2] = n, owner
                    return b[0] + 1, 0
        return None, best

    def free(self, seg):
        for i, b in enumerate(self.blocks):
            if b[0] + 1 == seg:
                b[2] = 0
                self._merge()
                return True
        return False

    def resize(self, seg, n):
        for i, b in enumerate(self.blocks):
            if b[0] + 1 == seg:
                if n <= b[1]:
                    if n < b[1]:
                        self.blocks.insert(i + 1, [b[0] + 1 + n, b[1] - n - 1, 0])
                        b[1] = n
                        self._merge()
                    return True, n
                nxt = self.blocks[i + 1] if i + 1 < len(self.blocks) else None
                if nxt and nxt[2] == 0 and b[1] + 1 + nxt[1] >= n:
                    total = b[1] + 1 + nxt[1]
                    del self.blocks[i + 1]
                    b[1] = n
                    if total > n:
                        self.blocks.insert(i + 1, [b[0] + 1 + n, total - n - 1, 0])
                    return True, n
                return False, b[1] + (1 + nxt[1] if nxt and nxt[2] == 0 else 0)
        return False, 0

    def _merge(self):
        i = 0
        while i + 1 < len(self.blocks):
            a, b = self.blocks[i], self.blocks[i + 1]
            if a[2] == 0 and b[2] == 0:
                a[1] += 1 + b[1]
                del self.blocks[i + 1]
            else:
                i += 1


# ---- the machine ----------------------------------------------------------------------------------------------------

def desc(base, access, flags=0xcf):
    return struct.pack('<HHBBBB', 0xffff, base & 0xffff, (base >> 16) & 0xff, access, flags, (base >> 24) & 0xff)


RREGS16 = (('ax', UC_X86_REG_EAX), ('bx', UC_X86_REG_EBX), ('cx', UC_X86_REG_ECX), ('dx', UC_X86_REG_EDX),
           ('si', UC_X86_REG_ESI), ('di', UC_X86_REG_EDI), ('bp', UC_X86_REG_EBP), ('ds', UC_X86_REG_DS),
           ('es', UC_X86_REG_ES))
V86 = (('edi', 0, 4), ('esi', 4, 4), ('ebp', 8, 4), ('ebx', 0x10, 4), ('edx', 0x14, 4), ('ecx', 0x18, 4),
       ('eax', 0x1c, 4), ('flags', 0x20, 2), ('es', 0x22, 2), ('ds', 0x24, 2), ('fs', 0x26, 2), ('gs', 0x28, 2))


class Machine:
    def __init__(self, a):
        self.a = a
        self.buf = ctypes.create_string_buffer(RAM)
        ptr = ctypes.addressof(self.buf)
        self.rm = Uc(UC_ARCH_X86, UC_MODE_16)
        self.rm.mem_map_ptr(0, 0xa0000, UC_PROT_ALL, ptr)
        self.rm.mem_map(0xa0000, 0x10000)                        # VGA window (RAM in chain-4 modes)
        self.rm.mem_map(0xb0000, 0x10000)                        # text memory
        self.rm.mem_map_ptr(0xc0000, 0x110000 - 0xc0000, UC_PROT_ALL, ptr + 0xc0000)
        self.pm = Uc(UC_ARCH_X86, UC_MODE_32)
        self.pm.mem_map_ptr(0, RAM, UC_PROT_ALL, ptr)
        self.vga = Vga()
        self.pit = Pit()
        self.arena = Arena()
        self.glog = open(a.out + '.gus', 'w')
        self.glog.write('# <music tick> v<voice> r<reg> <value> | p<port> <value> | irq <t/r/w> <time s>\n')
        self.gus = Gus(self.glog)
        self.st = {'now': 0.0, 'skew': 0.0, 'cpu': 'pm', 'stop': False, 'pic': [0xb8, 0xff], 'irq0': False,
                   'irqv': {}, 'fs': 0.0, 'vr_t': None, 'frame': 0, 'part': 'loader', 'mtick': 0,
                   'files': {}, 'nfile': 5, 'text': '', 'rmcall': None, 'stop_t': None,
                   'poll': None, 'cur_psp': PSP, 'dta': (PSP, 0x80), 'exitcode': None, 'hlt': False}
        self.frames = None
        self.ticks = None
        self.init_memory()
        self.hooks()

    # memory and CPU set-up
    def wr(self, lin, data):
        self.pm.mem_write(lin, bytes(data))

    def rd(self, lin, n):
        return bytes(self.pm.mem_read(lin, n))

    def init_memory(self):
        # the loader image at LOAD, relocated like DOS does
        img = bytearray(open(os.path.join(UNP, 'loader.bin'), 'rb').read())
        for line in open(os.path.join(UNP, 'loader.txt')):
            f = line.split()
            if len(f) >= 2 and ':' in f[0] and f[1].startswith('0x'):
                seg, off = (int(x, 16) for x in f[0].split(':'))
                x = seg * 16 + off
                struct.pack_into('<H', img, x, (struct.unpack_from('<H', img, x)[0] + LOAD) & 0xffff)
        self.wr(LOAD * 16, img)
        # IVT -> fake BIOS stubs; BIOS data area
        ivt = b''.join(struct.pack('<HH', n * 16, 0xF000) for n in range(256))
        self.wr(0, ivt)
        stubs = bytearray(0x1000)
        for n in range(256):
            stubs[n * 16:n * 16 + 2] = b'\x90\xcf'
        stubs[8 * 16:8 * 16 + 4] = b'\x90\xcd\x1c\xcf'
        stubs[0xff0:0xff2] = b'\x90\xcb'                        # RMRET: nop; retf
        self.wr(BIOS, stubs)
        bda = bytearray(256)
        struct.pack_into('<H', bda, 0x10, 0x0026)               # equipment
        struct.pack_into('<H', bda, 0x13, 640)
        struct.pack_into('<HH', bda, 0x1a, 0x1e, 0x1e)          # keyboard buffer empty
        bda[0x49] = 3
        struct.pack_into('<H', bda, 0x4a, 80)
        struct.pack_into('<H', bda, 0x63, 0x3d4)
        bda[0x84] = 24
        self.wr(0x400, bda)
        # environment and PSP
        env = b'COMSPEC=C:\\COMMAND.COM\x00ULTRASND=240,3,3,7,7\x00PATH=C:\\\x00\x00\x01\x00C:\\DOPE\\DOPE.EXE\x00'
        self.wr(ENVSEG * 16, env)
        psp = bytearray(256)
        psp[0:2] = b'\xcd\x20'
        struct.pack_into('<H', psp, 2, MEMTOP)
        struct.pack_into('<H', psp, 0x16, PSP)
        struct.pack_into('<H', psp, 0x2c, ENVSEG)
        psp[0x80] = 0
        psp[0x81] = 0x0d
        self.wr(PSP * 16, psp)
        # PMODE: GDT and code32 variables
        self.wr(GDT, b'\0' * 8 + desc(C32, 0x9a) + desc(C32, 0x92) + desc(0, 0x92))
        self.pm.reg_write(UC_X86_REG_GDTR, (0, GDT, 4 * 8 - 1, 0))
        himem_top = RAM
        self.wr(C32, struct.pack('<IIIIIII', (LOAD + 0x7e3f) * 16 + 0x1600 - C32, MEMTOP * 16 - C32, XMS - C32,
                                 himem_top - C32, PSP * 16, C16, C32))
        self.pm.reg_write(UC_X86_REG_CS, 8)
        for r in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_FS, UC_X86_REG_SS):
            self.pm.reg_write(r, 0x10)
        self.pm.reg_write(UC_X86_REG_GS, 0x18)
        self.pm.reg_write(UC_X86_REG_ESP, PM_STACK - C32)
        self.pm.reg_write(UC_X86_REG_EFLAGS, 0x2)
        self.pm.reg_write(UC_X86_REG_EIP, PM_ENTRY)

    # ---- hooks ----------------------------------------------------------------------------------------------------
    def hooks(self):
        rm, pm = self.rm, self.pm
        rm.hook_add(UC_HOOK_INTR, self.rm_intr)
        rm.hook_add(UC_HOOK_CODE, self.rm_bios, None, BIOS, BIOS + 0xfff)
        rm.hook_add(UC_HOOK_CODE, self.rm_launch, None, CDPI_LAUNCH, CDPI_LAUNCH)
        rm.hook_add(UC_HOOK_INSN, self.on_in, None, 1, 0, UC_X86_INS_IN)
        rm.hook_add(UC_HOOK_INSN, self.on_out, None, 1, 0, UC_X86_INS_OUT)
        pm.hook_add(UC_HOOK_INTR, self.pm_intr)
        for a in (PM_SETIRQ, PM_GETIRQ, PM_EXIT):
            pm.hook_add(UC_HOOK_CODE, self.pm_service, None, C32 + a, C32 + a)
        pm.hook_add(UC_HOOK_INSN, self.on_in, None, 1, 0, UC_X86_INS_IN)
        pm.hook_add(UC_HOOK_INSN, self.on_out, None, 1, 0, UC_X86_INS_OUT)
        for x in filter(None, self.a.trace.split(',')):
            seg, off = x.split(':') if ':' in x else ('pm', x)
            lin = C32 + int(off, 16) if seg == 'pm' else int(seg, 16) * 16 + int(off, 16)
            (pm if seg == 'pm' else rm).hook_add(UC_HOOK_CODE, self.on_trace, x, lin, lin)

    def t_io(self):
        return self.st['now'] + self.st['skew']

    def cpu(self):
        return self.rm if self.st['cpu'] == 'rm' else self.pm

    def on_trace(self, uc, addr, size, name):
        print('trace %s frame %d t %.4f' % (name, self.st['frame'], self.t_io()), flush=True)

    def stop_at(self, t):
        """end the quantum at time t (an event is due)"""
        self.st['stop_t'] = t
        self.cpu().emu_stop()

    # real-mode interrupts: dispatch through the IVT
    def rm_intr(self, uc, intno, _):
        if intno in (0x0d, 0x06, 0x00) and self.st.get('fault_dump') is None:
            pass
        self.dispatch(intno)

    def dispatch(self, n, hw=False):
        rm = self.rm
        ip, cs = rm.reg_read(UC_X86_REG_IP), rm.reg_read(UC_X86_REG_CS)
        sp, ss = rm.reg_read(UC_X86_REG_SP), rm.reg_read(UC_X86_REG_SS)
        fl = rm.reg_read(UC_X86_REG_EFLAGS)
        sp = (sp - 6) & 0xffff
        rm.mem_write(ss * 16 + sp, struct.pack('<HHH', ip, cs, fl & 0xffff))
        rm.reg_write(UC_X86_REG_SP, sp)
        off, seg = struct.unpack('<HH', rm.mem_read(n * 4, 4))
        rm.reg_write(UC_X86_REG_EFLAGS, fl & ~0x300)
        rm.reg_write(UC_X86_REG_CS, seg)
        rm.reg_write(UC_X86_REG_IP, off)

    def rm_regs(self):
        rm = self.rm
        return {k: rm.reg_read(r) for k, r in RREGS16}

    def rm_bios(self, uc, addr, size, _):
        off = addr - BIOS
        if off == 0xff0:                                         # return from an int 32h call
            self.rmcall_return()
            return
        if off & 15:
            return
        n = off >> 4
        # interrupt frame on the stack: ip, cs, flags
        rm = self.rm
        ss, sp = rm.reg_read(UC_X86_REG_SS), rm.reg_read(UC_X86_REG_SP)
        r = {k: rm.reg_read(reg) for k, reg in (('eax', UC_X86_REG_EAX), ('ebx', UC_X86_REG_EBX),
                                                   ('ecx', UC_X86_REG_ECX), ('edx', UC_X86_REG_EDX),
                                                   ('esi', UC_X86_REG_ESI), ('edi', UC_X86_REG_EDI),
                                                   ('ebp', UC_X86_REG_EBP), ('ds', UC_X86_REG_DS),
                                                   ('es', UC_X86_REG_ES))}
        fl = struct.unpack('<H', rm.mem_read(ss * 16 + sp + 4, 2))[0]
        r['flags'] = fl
        self.service(n, r, 'rm')
        for k, reg in (('eax', UC_X86_REG_EAX), ('ebx', UC_X86_REG_EBX), ('ecx', UC_X86_REG_ECX),
                       ('edx', UC_X86_REG_EDX), ('esi', UC_X86_REG_ESI), ('edi', UC_X86_REG_EDI),
                       ('es', UC_X86_REG_ES)):
            rm.reg_write(reg, r[k] & (0xffff if k == 'es' else 0xffffffff))
        rm.mem_write(ss * 16 + sp + 4, struct.pack('<H', r['flags'] & 0xffff))

    def rm_launch(self, uc, addr, size, _):
        off = struct.unpack('<I', self.rd(CDPI_SEG * 16 + 6, 4))[0]
        name = EXE_PARTS.get(off, 'exe@%x' % off)
        self.st['part'] = name
        self.st['part_start'] = self.st['frame']
        print('part %s starts: frame %d, time %.3f, music %s' % (name, self.st['frame'], self.t_io(), self.music()),
              flush=True)
        if name == self.a.stop_part:
            self.st['stop_req'] = True
        elif self.a.snap_parts:
            self.st['snap_req'] = 'part_%s' % name
        self.rm.ctl_remove_cache(0, 0xa0000)

    # services (int N) for both CPUs; r: dict of 32-bit registers + 'flags', 'ds', 'es'
    def service(self, n, r, who):
        ah, al = (r['eax'] >> 8) & 0xff, r['eax'] & 0xff
        ax = r['eax'] & 0xffff

        def setcf(c):
            r['flags'] = (r['flags'] | 1) if c else (r['flags'] & ~1)

        def setzf(z):
            r['flags'] = (r['flags'] | 0x40) if z else (r['flags'] & ~0x40)

        def setax(v):
            r['eax'] = (r['eax'] & ~0xffff) | (v & 0xffff)

        def lin(seg, off):
            return (seg << 4) + (off & 0xffff)

        if n == 8:                                               # BIOS timer tick
            c = struct.unpack('<I', self.rd(0x46c, 4))[0] + 1
            self.wr(0x46c, struct.pack('<I', c))
            return
        if n in (0x1c, 0x1b, 0x23, 0x24, 0x28):
            return
        if n == 0x10:
            self.int10(r, ah, al, ax, setax)
            return
        if n == 0x16:
            if ah in (1, 0x11):
                setzf(True)
                self.st['keychecks'] = self.st.get('keychecks', 0) + 1
            elif ah in (0, 0x10):
                setax(0x011b)
                print('int 16h AH=%02x: wait for a key (returned ESC)' % ah, flush=True)
            elif ah in (2, 0x12):
                setax(0)
            return
        if n == 0x33:                                            # mouse: not installed
            if ax == 0:
                setax(0)
            return
        if n == 0x2f:
            if ax == 0x1680:
                return
            print('int 2Fh AX=%04x' % ax, flush=True)
            return
        if n == 0x21:
            self.int21(r, ah, al, setcf, setax, lin)
            return
        if n == 0x1a:
            if ah == 0:
                c = struct.unpack('<I', self.rd(0x46c, 4))[0]
                r['ecx'] = (r['ecx'] & ~0xffff) | (c >> 16)
                r['edx'] = (r['edx'] & ~0xffff) | (c & 0xffff)
                r['eax'] &= ~0xff
            return
        if n == 0x15:
            setcf(True)
            return
        print('%s int %02xh AX=%04x unhandled' % (who, n, ax), flush=True)

    def int10(self, r, ah, al, ax, setax):
        vga = self.vga
        if ah == 0:
            m = al & 0x7f
            if m == 0x13:
                vga.set_mode(MODE13)
                vga.graphics = True
                vga.dac[:] = default_palette()
                vga.mode_name = '13h'
            else:
                vga.set_mode(MODE3)
                vga.graphics = False
                vga.mode_name = 'text'
            if not al & 0x80:
                self.clear_vram()
            vga.remap = True
            self.wr(0x449, bytes([m]))
            print('int 10h set mode %#x (frame %d, %s, from %s)' % (al, self.st['frame'], self.st['part'],
                                                                   self.caller()), flush=True)
        elif ah == 0x0f:
            setax(0x2800 | self.rd(0x449, 1)[0] if vga.graphics else 0x5003)
            r['ebx'] &= ~0xff00
        elif ah == 0x10:
            if al == 0x10:
                i = r['ebx'] & 0xff
                vga.dac[i * 3:i * 3 + 3] = bytes(((r['edx'] >> 8) & 0x3f, (r['ecx'] >> 8) & 0x3f, r['ecx'] & 0x3f))
            elif al == 0x12:
                first, cnt = r['ebx'] & 0xffff, r['ecx'] & 0xffff
                data = self.rd((r['es'] << 4) + (r['edx'] & 0xffff), cnt * 3)
                for k in range(cnt * 3):
                    vga.dac[(first * 3 + k) % 768] = data[k] & 0x3f
            elif al == 0x17:
                first, cnt = r['ebx'] & 0xffff, r['ecx'] & 0xffff
                self.wr((r['es'] << 4) + (r['edx'] & 0xffff), bytes(vga.dac[first * 3:(first + cnt) * 3]))
            elif al in (0, 1, 2, 3):
                pass
            else:
                print('int 10h AX=%04x' % ax, flush=True)
        elif ah == 0x1a:
            setax(0x001a)
            r['ebx'] = (r['ebx'] & ~0xffff) | 0x0008
        elif ah == 0x12:
            if (r['ebx'] & 0xff) == 0x10:
                r['ebx'] = (r['ebx'] & ~0xffff) | 0x0003
        elif ah in (1, 2, 5, 6, 7, 9, 0x0a):
            pass
        elif ah == 3:
            r['ecx'] = (r['ecx'] & ~0xffff) | 0x0607
            r['edx'] &= ~0xffff
        elif ah == 0x0e:
            self.text(chr(al))
        elif ah == 0x11:
            if al == 0x30:
                r['ecx'] = (r['ecx'] & ~0xffff) | 16
                r['edx'] = (r['edx'] & ~0xffff) | 24
                r['es'] = 0xc000
                r['ebp'] = (r['ebp'] & ~0xffff)
        else:
            print('int 10h AX=%04x unhandled' % ax, flush=True)

    def int21(self, r, ah, al, setcf, setax, lin):
        dx = r['edx'] & 0xffff
        bx = r['ebx'] & 0xffff
        cx = r['ecx'] & 0xffff
        st = self.st
        if ah == 0x09:
            s = self.rd(lin(r['ds'], dx), 2000).split(b'$')[0]
            self.text(s.decode('latin1'))
        elif ah == 0x02:
            self.text(chr(dx & 0xff))
        elif ah == 0x30:
            setax(0x1606)
            r['ebx'] &= ~0xffff
        elif ah == 0x25:
            self.wr(al * 4, struct.pack('<HH', dx, r['ds']))
        elif ah == 0x35:
            off, seg = struct.unpack('<HH', self.rd(al * 4, 4))
            r['ebx'] = (r['ebx'] & ~0xffff) | off
            r['es'] = seg
        elif ah == 0x3d:
            name = self.rd(lin(r['ds'], dx), 128).split(b'\0')[0].decode('latin1')
            base = name.replace('\\', '/').split('/')[-1].upper()
            path = None
            for f in os.listdir(ORIG):
                if f.upper() == base:
                    path = os.path.join(ORIG, f)
            if path is None:
                print('open %r: not found' % name, flush=True)
                setax(2)
                setcf(True)
                return
            h = st['nfile']
            st['nfile'] += 1
            st['files'][h] = [path, 0]
            setax(h)
            setcf(False)
            print('open %r -> %d (frame %d)' % (name, h, st['frame']), flush=True)
        elif ah == 0x3e:
            st['files'].pop(bx, None)
            setcf(False)
        elif ah == 0x3f:
            f = st['files'].get(bx)
            if f is None:
                setax(6)
                setcf(True)
                return
            with open(f[0], 'rb') as fh:
                fh.seek(f[1])
                data = fh.read(cx)
            f[1] += len(data)
            dst = lin(r['ds'], dx)
            self.wr(dst, data)
            self.rm.ctl_remove_cache(dst, dst + max(len(data), 1))
            setax(len(data))
            setcf(False)
        elif ah == 0x40:
            data = self.rd(lin(r['ds'], dx), cx)
            if bx in (1, 2):
                self.text(data.decode('latin1'))
            setax(cx)
            setcf(False)
        elif ah == 0x42:
            f = st['files'].get(bx)
            if f is None:
                setax(6)
                setcf(True)
                return
            off = (cx << 16) | dx
            if off >= 0x80000000:
                off -= 1 << 32
            size = os.path.getsize(f[0])
            f[1] = [off, f[1] + off, size + off][al]
            r['edx'] = (r['edx'] & ~0xffff) | (f[1] >> 16)
            setax(f[1] & 0xffff)
            setcf(False)
        elif ah == 0x48:
            seg, best = self.arena.alloc(bx, st['cur_psp'])
            if seg is None:
                setax(8)
                r['ebx'] = (r['ebx'] & ~0xffff) | best
                setcf(True)
            else:
                setax(seg)
                setcf(False)
            print('DOS alloc %#x paragraphs -> %s (frame %d)' % (bx, '%04x' % seg if seg else 'fail, largest %#x'
                                                                % best, st['frame']), flush=True)
        elif ah == 0x49:
            ok = self.arena.free(r['es'])
            setcf(not ok)
            print('DOS free %04x (frame %d)' % (r['es'], st['frame']), flush=True)
        elif ah == 0x4a:
            ok, n = self.arena.resize(r['es'], bx)
            if not ok:
                setax(8)
                r['ebx'] = (r['ebx'] & ~0xffff) | n
            setcf(not ok)
            print('DOS resize %04x to %#x: %s' % (r['es'], bx, ok), flush=True)
        elif ah == 0x26:
            src = self.rd(st['cur_psp'] * 16, 256)
            p = bytearray(src)
            struct.pack_into('<H', p, 2, MEMTOP)
            self.wr(dx * 16, p)
        elif ah == 0x4c:
            print('DOS exit %d (frame %d, %s)' % (al, st['frame'], st['part']), flush=True)
            st['exitcode'] = al
            st['stop'] = True
            self.cpu().emu_stop()
        elif ah == 0x62 or ah == 0x51:
            r['ebx'] = (r['ebx'] & ~0xffff) | st['cur_psp']
        elif ah == 0x1a:
            st['dta'] = (r['ds'], dx)
        elif ah == 0x2c:
            r['ecx'] &= ~0xffff
            r['edx'] &= ~0xffff
        elif ah == 0x2a:
            r['ecx'] = (r['ecx'] & ~0xffff) | 1995
            r['edx'] = (r['edx'] & ~0xffff) | 0x040f
        elif ah == 0x33:
            r['edx'] &= ~0xff
        elif ah == 0x44:
            setcf(True)
            setax(1)
        elif ah == 0x0b:
            r['eax'] &= ~0xff
        else:
            print('int 21h AH=%02x unhandled (%s)' % (ah, st['part']), flush=True)
            setcf(True)

    def caller(self):
        if self.st['cpu'] != 'rm':
            return 'pm'
        rm = self.rm
        ss, sp = rm.reg_read(UC_X86_REG_SS), rm.reg_read(UC_X86_REG_SP)
        ip, cs = struct.unpack('<HH', rm.mem_read(ss * 16 + sp, 4))
        return '%04x:%04x' % (cs, ip)

    def text(self, s):
        self.st['text'] += s
        while '\n' in self.st['text']:
            line, self.st['text'] = self.st['text'].split('\n', 1)
            print('text: %s' % line.rstrip('\r'), flush=True)

    def clear_vram(self):
        self.vga.planes[:] = 0
        if self.vga.chain4_ram:
            self.rm.mem_write(0xa0000, bytes(0x10000))

    # ---- protected mode ---------------------------------------------------------------------------------------------
    def pm_intr(self, uc, intno, _):
        pm = self.pm
        if intno == 0x33:                                        # real-mode interrupt, register image at V86R
            img = self.rd(C32 + V86R, 0x2a)
            r = {k: int.from_bytes(img[o:o + s], 'little') for k, o, s in V86}
            n = pm.reg_read(UC_X86_REG_EAX) & 0xff
            off, seg = struct.unpack('<HH', self.rd(n * 4, 4))
            if seg != 0xF000:
                print('pm int 33h -> int %02x hooked at %04x:%04x (not emulated)' % (n, seg, off), flush=True)
            self.service(n, r, 'pm')
            b = bytearray(img)
            for k, o, s in V86:
                b[o:o + s] = (r[k] & ((1 << (8 * s)) - 1)).to_bytes(s, 'little')
            self.wr(C32 + V86R, b)
            return
        if intno == 0x32:                                        # real-mode far call CX:DX
            cx, dx = pm.reg_read(UC_X86_REG_ECX) & 0xffff, pm.reg_read(UC_X86_REG_EDX) & 0xffff
            self.st['rmcall'] = (cx, dx)
            pm.emu_stop()
            return
        if intno == 0x31:
            ax = pm.reg_read(UC_X86_REG_EAX) & 0xffff
            fl = pm.reg_read(UC_X86_REG_EFLAGS)
            if ax in (0x900, 0x901, 0x902):
                old = 1 if fl & 0x200 else 0
                if ax == 0x900:
                    fl &= ~0x200
                elif ax == 0x901:
                    fl |= 0x200
                pm.reg_write(UC_X86_REG_EFLAGS, fl & ~1)
                pm.reg_write(UC_X86_REG_EAX, (pm.reg_read(UC_X86_REG_EAX) & ~0xff) | old)
                return
            print('pm int 31h AX=%04x unhandled' % ax, flush=True)
            pm.reg_write(UC_X86_REG_EFLAGS, fl | 1)
            return
        eip = pm.reg_read(UC_X86_REG_EIP)
        print('pm int %02xh at %05x: stop' % (intno, eip), flush=True)
        self.st['stop'] = True
        pm.emu_stop()

    def pm_service(self, uc, addr, size, _):
        pm = self.pm
        a = addr - C32
        if a == PM_EXIT:
            print('loader exits (frame %d, time %.3f)' % (self.st['frame'], self.t_io()), flush=True)
            self.st['stop'] = True
            pm.emu_stop()
            return
        bl = pm.reg_read(UC_X86_REG_EBX) & 0xff
        if a == PM_SETIRQ:
            edx = pm.reg_read(UC_X86_REG_EDX)
            self.st['irqv'][bl] = edx
            print('setirqvect IRQ %d -> %05x (frame %d)' % (bl, edx, self.st['frame']), flush=True)
        else:
            pm.reg_write(UC_X86_REG_EDX, self.st['irqv'].get(bl, 0xdead0000 + bl))
        esp = pm.reg_read(UC_X86_REG_ESP)
        ret = struct.unpack('<I', self.rd(C32 + esp, 4))[0]
        pm.reg_write(UC_X86_REG_ESP, esp + 4)
        pm.reg_write(UC_X86_REG_EIP, ret)

    def start_rmcall(self):
        cx, dx = self.st['rmcall']
        img = self.rd(C32 + V86R, 0x2a)
        r = {k: int.from_bytes(img[o:o + s], 'little') for k, o, s in V86}
        rm = self.rm
        for k, reg in (('eax', UC_X86_REG_EAX), ('ebx', UC_X86_REG_EBX), ('ecx', UC_X86_REG_ECX),
                       ('edx', UC_X86_REG_EDX), ('esi', UC_X86_REG_ESI), ('edi', UC_X86_REG_EDI),
                       ('ebp', UC_X86_REG_EBP)):
            rm.reg_write(reg, r[k])
        for k, reg in (('ds', UC_X86_REG_DS), ('es', UC_X86_REG_ES), ('fs', UC_X86_REG_FS), ('gs', UC_X86_REG_GS)):
            rm.reg_write(reg, r[k])
        ss, sp = RM_STACK
        sp -= 4
        self.wr(ss * 16 + sp, struct.pack('<HH', RMRET[1], RMRET[0]))
        rm.reg_write(UC_X86_REG_SS, ss)
        rm.reg_write(UC_X86_REG_SP, sp)
        rm.reg_write(UC_X86_REG_EFLAGS, 0x202)
        rm.reg_write(UC_X86_REG_CS, cx)
        rm.reg_write(UC_X86_REG_IP, dx)
        self.st['cpu'] = 'rm'
        self.st['rmcall'] = None
        print('int 32h: real-mode call %04x:%04x (frame %d, time %.3f)' % (cx, dx, self.st['frame'], self.t_io()),
              flush=True)

    def rmcall_return(self):
        rm = self.rm
        img = bytearray(self.rd(C32 + V86R, 0x2a))
        vals = {'eax': UC_X86_REG_EAX, 'ebx': UC_X86_REG_EBX, 'ecx': UC_X86_REG_ECX, 'edx': UC_X86_REG_EDX,
                'esi': UC_X86_REG_ESI, 'edi': UC_X86_REG_EDI, 'ebp': UC_X86_REG_EBP, 'ds': UC_X86_REG_DS,
                'es': UC_X86_REG_ES, 'fs': UC_X86_REG_FS, 'gs': UC_X86_REG_GS, 'flags': UC_X86_REG_EFLAGS}
        for k, o, s in V86:
            img[o:o + s] = (rm.reg_read(vals[k]) & ((1 << (8 * s)) - 1)).to_bytes(s, 'little')
        self.wr(C32 + V86R, img)
        self.st['cpu'] = 'pm'
        print('int 32h call returned (frame %d, time %.3f)' % (self.st['frame'], self.t_io()), flush=True)
        rm.emu_stop()

    def run_pm_irq(self, irq):
        """run the player's protected-mode handler for IRQ irq to completion on its own stack"""
        pm = self.pm
        handler = self.st['irqv'][irq]
        ctx = pm.context_save()
        esp = PM_ISTACK - C32 - 12
        self.wr(C32 + esp, struct.pack('<III', PM_SENT - C32, 8, 0x2))
        pm.reg_write(UC_X86_REG_ESP, esp)
        pm.reg_write(UC_X86_REG_SS, 0x10)
        pm.reg_write(UC_X86_REG_EFLAGS, 0x2)
        try:
            pm.emu_start(handler, PM_SENT, count=20_000_000)
        except UcError as e:
            print('error in IRQ handler at %x: %s' % (pm.reg_read(UC_X86_REG_EIP), e), flush=True)
            self.st['stop'] = True
        if pm.reg_read(UC_X86_REG_EIP) != PM_SENT - C32:
            print('IRQ handler did not return (eip %x)' % pm.reg_read(UC_X86_REG_EIP), flush=True)
            self.st['stop'] = True
        pm.context_restore(ctx)

    # ---- ports ------------------------------------------------------------------------------------------------------
    def on_in(self, uc, port, size, _):
        t = self.t_io()
        vga = self.vga
        if 0x240 <= port <= 0x24f or 0x340 <= port <= 0x347:
            self.gus.now = t
            return self.gus.inp(port, size)
        if port == 0x3da:
            vga.ac_ff = 0
            v = self.retrace_status(t)
            c = self.cpu()
            ip = c.reg_read(UC_X86_REG_EIP) + (c.reg_read(UC_X86_REG_CS) << 16 if self.st['cpu'] == 'rm' else 0)
            last = self.st['poll']
            if last == (ip, v, t):
                # polled again without progress: jump to the next change of the status bits
                tn = self.retrace_next(t)
                te = self.next_event_time()
                if te is not None and te < tn:
                    self.st['skew'] += te - t
                    self.stop_at(te)
                else:
                    self.st['skew'] += tn - t
                self.st['poll'] = None
            else:
                self.st['poll'] = (ip, v, t)
            return v
        if port == 0x40:
            return self.pit.read40(t)
        if port == 0x3c5:
            return vga.seq[vga.seq_i] if vga.seq_i < len(vga.seq) else 0
        if port == 0x3d5:
            return vga.crtc[vga.crtc_i] if vga.crtc_i < len(vga.crtc) else 0
        if port == 0x3cf:
            return vga.gc[vga.gc_i] if vga.gc_i < len(vga.gc) else 0
        if port == 0x3c1:
            return vga.ac[vga.ac_i & 0x1f] if (vga.ac_i & 0x1f) < len(vga.ac) else 0
        if port == 0x3cc:
            return vga.misc
        if port == 0x3c4:
            return vga.seq_i
        if port == 0x3d4:
            return vga.crtc_i
        if port == 0x3ce:
            return vga.gc_i
        if port == 0x3c9:
            v = vga.dac[vga.dac_r % 768]
            vga.dac_r += 1
            return v
        if port == 0x3c8:
            return vga.dac_w // 3
        if port == 0x3c7:
            return 0
        if port == 0x3c6:
            return 0xff
        if port == 0x21:
            return self.st['pic'][0]
        if port == 0xa1:
            return self.st['pic'][1]
        if port == 0x60:
            return 0
        if port == 0x64:
            return 0x1c
        if port == 0x61:
            return 0x20 if int(t * 66288) & 1 else 0
        if port in (0x20, 0xa0):
            return 0
        if port == 0x300 or port == 0x80:
            return 0xff
        cnt = self.st.setdefault('unk_in', {})
        cnt[port] = cnt.get(port, 0) + 1
        return 0xff

    def on_out(self, uc, port, size, val, _):
        t = self.t_io()
        vga = self.vga
        if 0x240 <= port <= 0x24f or 0x340 <= port <= 0x347:
            self.gus.now = t
            self.gus.out(port, size, val)
            return
        if size == 2 and port in (0x3c4, 0x3d4, 0x3ce):
            self.on_out(uc, port, 1, val & 0xff, None)
            self.on_out(uc, port + 1, 1, (val >> 8) & 0xff, None)
            return
        if size == 2 and port == 0x3c0:
            self.on_out(uc, port, 1, val & 0xff, None)
            self.on_out(uc, port, 1, (val >> 8) & 0xff, None)
            return
        val &= 0xff
        if port == 0x3c8:
            vga.dac_w = val * 3
        elif port == 0x3c9:
            vga.dac[vga.dac_w % 768] = val & 0x3f
            vga.dac_w += 1
        elif port == 0x3c7:
            vga.dac_r = val * 3
        elif port == 0x3c4:
            vga.seq_i = val & 7
        elif port == 0x3c5:
            if vga.seq_i < len(vga.seq):
                old = vga.seq[vga.seq_i]
                vga.seq[vga.seq_i] = val
                if vga.seq_i == 4 and (old ^ val) & 8:
                    vga.remap = True
                    self.cpu().emu_stop()
        elif port == 0x3d4:
            vga.crtc_i = val & 0x3f
        elif port == 0x3d5:
            i = vga.crtc_i
            if i < len(vga.crtc):
                if i < 8 and vga.crtc[0x11] & 0x80 and i != 7:
                    return
                if i == 7 and vga.crtc[0x11] & 0x80:
                    vga.crtc[7] = (vga.crtc[7] & ~0x10) | (val & 0x10)
                    return
                vga.crtc[i] = val
                if i in (0x0c, 0x0d):
                    vga.start_writes += 1
        elif port == 0x3ce:
            vga.gc_i = val & 0xf
        elif port == 0x3cf:
            if vga.gc_i < len(vga.gc):
                vga.gc[vga.gc_i] = val
        elif port == 0x3c0:
            if vga.ac_ff == 0:
                vga.ac_i = val
            elif (vga.ac_i & 0x1f) < len(vga.ac):
                vga.ac[vga.ac_i & 0x1f] = val
            vga.ac_ff ^= 1
        elif port == 0x3c2:
            vga.misc = val
        elif port == 0x43:
            self.pit.write43(val, t)
        elif port == 0x40:
            self.pit.write40(val, t)
        elif port == 0x21:
            self.st['pic'][0] = val
        elif port == 0xa1:
            self.st['pic'][1] = val
        elif port in (0x20, 0xa0, 0x80, 0x61, 0x42, 0x3c6, 0x3d9):
            pass
        else:
            cnt = self.st.setdefault('unk_out', {})
            cnt[port] = cnt.get(port, 0) + 1

    # ---- VGA timing and memory mapping -------------------------------------------------------------------------------
    def retrace_status(self, t):
        lp, vtotal, vrs, vrlen, vde = self.vga.timing()
        line = ((t - self.st['fs']) / lp + 1e-6) % vtotal       # (bias: an event time computed as fs + k*lp is in line k)
        v = 0
        if vrs <= line < vrs + vrlen:
            v |= 8
        if line >= vde:
            v |= 1
        return v

    def retrace_next(self, t):
        """the time the status bits change next after t"""
        lp, vtotal, vrs, vrlen, vde = self.vga.timing()
        el = (t - self.st['fs']) / lp + 1e-6
        line = el % vtotal
        base = el - line
        for edge in (vde, vrs, vrs + vrlen, vtotal):
            if edge > line:
                return self.st['fs'] + (base + edge) * lp
        return self.st['fs'] + (base + vtotal) * lp

    def apply_remap(self):
        vga = self.vga
        vga.remap = False
        want_ram = vga.chain4() or not vga.graphics
        if want_ram == vga.chain4_ram:
            return
        rm = self.rm
        if want_ram:
            lin = np.empty(0x10000, np.uint8)
            a = np.arange(0x10000)
            lin[:] = vga.planes[a & 3, a & 0xfffc]
            rm.mem_unmap(0xa0000, 0x10000)
            rm.mem_map(0xa0000, 0x10000)
            rm.mem_write(0xa0000, lin.tobytes())
        else:
            lin = np.frombuffer(bytes(rm.mem_read(0xa0000, 0x10000)), np.uint8)
            a = np.arange(0x10000)
            vga.planes[a & 3, a & 0xfffc] = lin
            rm.mem_unmap(0xa0000, 0x10000)
            rm.mmio_map(0xa0000, 0x10000, self.mmio_read, None, self.mmio_write, None)
        vga.chain4_ram = want_ram
        print('VGA %s (frame %d, %s)' % ('chain-4' if want_ram else 'unchained (planar)', self.st['frame'],
                                        self.st['part']), flush=True)

    def mmio_read(self, uc, off, size, _):
        return self.vga.read(off, size)

    def mmio_write(self, uc, off, size, val, _):
        self.vga.write(off, size, val)

    # ---- scheduling -----------------------------------------------------------------------------------------------
    def frame_times(self):
        lp, vtotal, vrs, vrlen, vde = self.vga.timing()
        fs = self.st['fs']
        return fs + vrs * lp, fs + vtotal * lp

    def next_event_time(self):
        t = self.st['now']
        vr, fe = self.frame_times()
        cands = [fe]
        if self.st['vr_t'] != vr and vr > t - 1e-9:
            cands.append(vr)
        if self.pit.next_t is not None:
            cands.append(self.pit.next_t)
        g = self.gus.next_event(t)
        if g is not None:
            cands.append(g)
        return min(cands)

    def music(self):
        b = self.rd(C32 + PLAYER['order'], 1)[0], self.rd(C32 + PLAYER['row'], 1)[0]
        return '%02x:%02x' % b

    def process_events(self):
        st = self.st
        t = st['now']
        vr, fe = self.frame_times()
        if st['vr_t'] != vr and vr <= t + 1e-9:
            st['vr_t'] = vr
            self.vga.latched_start = self.vga.start_addr()
        if fe <= t + 1e-9:
            self.record()
            st['fs'] = fe
            st['frame'] += 1
        while self.pit.next_t is not None and self.pit.next_t <= t + 1e-9:
            st['irq0'] = True
            self.pit.fire()
        self.gus.advance(t)

    def record(self):
        st = self.st
        vga = self.vga
        hdr = bytearray(8)
        img = np.zeros((FH, FW), np.uint8)
        if vga.graphics:
            ram = bytes(self.rm.mem_read(0xa0000, 0x10000)) if vga.chain4_ram else None
            fr = vga.render(ram)
            img[:fr.shape[0], :fr.shape[1]] = fr
            w, h = fr.shape[1], fr.shape[0]
        else:
            w = h = 0
        struct.pack_into('<HHHH', hdr, 0, w, h, 1 if vga.graphics else 0, 0)
        if st['frame'] >= self.a.start_frame:
            self.frames.write(bytes(hdr) + bytes(vga.dac) + img.tobytes())
        order, row, cnt = (self.rd(C32 + PLAYER[k], 1)[0] for k in ('order', 'row', 'count'))
        c = self.cpu()
        if st['cpu'] == 'rm':
            where = 'rm %04x:%04x' % (c.reg_read(UC_X86_REG_CS), c.reg_read(UC_X86_REG_IP))
        else:
            where = 'pm %05x' % c.reg_read(UC_X86_REG_EIP)
        mode = '%dx%d%s' % (w, h, 'c4' if vga.chain4() else 'x') if vga.graphics else 'text'
        self.ticks.write('%d %.5f %d %d %d %d %s %04x %d %d %s %s\n' % (
            st['frame'], st['fs'], order, row, cnt, st['mtick'], st['part'], vga.latched_start, vga.start_writes,
            st.get('keychecks', 0), mode, where))
        if st['frame'] % 700 == 0:
            print('frame %d time %.2f music %s part %s %s flips %d' % (st['frame'], st['fs'], self.music(), st['part'],
                                                                      where, vga.start_writes), flush=True)
        if st['frame'] in self.snap_frames:
            st['snap_req'] = 'f%d' % st['frame']

    def irq_allowed(self, irq):
        st = self.st
        mask = st['pic'][0]
        if irq < 8 and mask >> irq & 1:
            return False
        fl = self.cpu().reg_read(UC_X86_REG_EFLAGS)
        return bool(fl & 0x200)

    def deliver(self):
        """deliver pending interrupts; returns True if one is still blocked"""
        st = self.st
        blocked = False
        if st['irq0']:
            if self.irq_allowed(0):
                st['irq0'] = False
                if st['cpu'] == 'rm':
                    off, seg = struct.unpack('<HH', self.rd(8 * 4, 4))
                    self.dispatch(8, hw=True)
                else:
                    self.service(8, {'eax': 0, 'flags': 0}, 'pm')
            else:
                blocked = True
        g = self.gus
        g.advance(st['now'])
        n = 0
        while g.irq() and 7 in st['irqv'] and n < 40:
            if not self.irq_allowed(7):
                blocked = True
                break
            kind = ('t' if g.status & 0xc else '') + ('r' if g.status & 0x40 else '') + ('w' if g.status & 0x20 else '')
            if g.status & 8 and not g.status & 0x40:            # the handler takes the timer path (0x4040)
                st['mtick'] += 1
                g.tick = st['mtick']
            self.glog.write('%d irq %s %.6f\n' % (g.tick, kind, st['now']))
            self.run_pm_irq(7)
            g.advance(st['now'])
            n += 1
        return blocked

    def run(self):
        st = self.st
        a = self.a
        ips = a.mips * 1e6
        quantum = a.quantum
        max_frame = a.frames
        while not st['stop'] and st['frame'] < max_frame:
            if st.pop('stop_req', False):
                print('stopped where part %s starts (frame %d)' % (a.stop_part, st['frame']), flush=True)
                break
            if st.get('snap_req'):
                self.save_snapshot(st.pop('snap_req'))
            if self.vga.remap:
                self.apply_remap()
            if st['rmcall']:
                self.start_rmcall()
            te = self.next_event_time()
            if st['now'] >= te - 1e-9:
                self.process_events()
                continue
            blocked = self.deliver()
            if st['stop']:
                break
            n = int(min(quantum, (te - st['now']) * ips + 1))
            if blocked:
                n = min(n, 300)
            st['skew'] = 0.0
            st['stop_t'] = None
            cpu = self.cpu()
            if a.debug:
                print('dbg now %.7f te %.7f n %d cpu %s irq0 %s gus %x blocked %s' % (
                    st['now'], te, n, st['cpu'], st['irq0'], self.gus.status, blocked))
                a.debug -= 1
            try:
                if st['cpu'] == 'rm':
                    pc = cpu.reg_read(UC_X86_REG_CS) * 16 + cpu.reg_read(UC_X86_REG_IP)
                    cpu.emu_start(pc, 0x200000, count=n)
                else:
                    cpu.emu_start(cpu.reg_read(UC_X86_REG_EIP), 0xffffffff, count=n)
            except UcError as e:
                c = self.cpu()
                if st['cpu'] == 'rm':
                    where = '%04x:%04x' % (c.reg_read(UC_X86_REG_CS), c.reg_read(UC_X86_REG_IP))
                else:
                    where = '%05x' % c.reg_read(UC_X86_REG_EIP)
                print('error on %s at %s: %s (frame %d, %s)' % (st['cpu'], where, e, st['frame'], st['part']),
                      flush=True)
                break
            if st['stop_t'] is not None:
                st['now'] = max(st['now'], st['stop_t'])
            else:
                st['now'] += n / ips + st['skew']
            # hlt: wait for the next event
            if st['cpu'] == 'rm':
                pc = self.rm.reg_read(UC_X86_REG_CS) * 16 + self.rm.reg_read(UC_X86_REG_IP)
                if self.rm.mem_read(pc - 1, 1) == b'\xf4' and st.get('hlt_pc') == pc:
                    st['now'] = max(st['now'], self.next_event_time())
                st['hlt_pc'] = pc if self.rm.mem_read(pc - 1, 1) == b'\xf4' else None
        print('ended at frame %d, time %.2f s, part %s, music %s' % (st['frame'], st['now'], st['part'],
                                                                    self.music()), flush=True)

    # ---- snapshots ------------------------------------------------------------------------------------------------
    def save_snapshot(self, tag):
        path = self.a.snap.format(frame=self.st['frame'], tag=tag)
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        vram = bytes(self.rm.mem_read(0xa0000, 0x10000)) if self.vga.chain4_ram else None
        d = {'mem': self.buf.raw, 'vram': vram, 'text': bytes(self.rm.mem_read(0xb0000, 0x10000)),
             'rm': self.rm.context_save(), 'pm': self.pm.context_save(), 'st': dict(self.st),
             'vga': dict(self.vga.__dict__), 'pit': dict(self.pit.__dict__), 'arena': dict(self.arena.__dict__),
             'gus': self.gus.__getstate__(), 'voices': [dict(v.__dict__) for v in self.gus.v]}
        with open(path, 'wb') as f:
            pickle.dump(d, f, protocol=4)
        print('snapshot %s at frame %d' % (path, self.st['frame']), flush=True)

    def load_snapshot(self, path):
        d = pickle.load(open(path, 'rb'))
        ctypes.memmove(self.buf, d['mem'], RAM)
        self.rm.ctl_remove_cache(0, 0x110000)
        self.rm.context_restore(d['rm'])
        self.pm.context_restore(d['pm'])
        self.st.update(d['st'])
        self.vga.__dict__.update(d['vga'])
        self.pit.__dict__.update(d['pit'])
        self.arena.__dict__.update(d['arena'])
        self.gus.__dict__.update(d['gus'])
        self.gus.v = [Voice() for _ in range(32)]
        for v, vd in zip(self.gus.v, d['voices']):
            v.__dict__.update(vd)
        self.rm.mem_write(0xb0000, d['text'])
        if not self.vga.chain4_ram:
            self.rm.mem_unmap(0xa0000, 0x10000)
            self.rm.mmio_map(0xa0000, 0x10000, self.mmio_read, None, self.mmio_write, None)
        else:
            self.rm.mem_write(0xa0000, d['vram'])
        print('resumed at frame %d (%s, music %s)' % (self.st['frame'], self.st['part'], self.music()), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--mips', type=float, default=100.0, help='main-line speed, million instructions per second')
    ap.add_argument('--quantum', type=int, default=20000, help='instructions per emulation slice')
    ap.add_argument('--frames', type=int, default=1 << 30, help='stop after this many video frames')
    ap.add_argument('--start-frame', type=int, default=0, help='only write frames from this one on')
    ap.add_argument('--snap-at', default='', help='snapshots at these frames (comma-separated)')
    ap.add_argument('--snap-parts', action='store_true', help='snapshot when each part starts')
    ap.add_argument('--snap', default=os.path.join(RE, 'snaps', '{tag}.snap'),
                    help='where snapshots are saved ({frame}, {tag} = part_<p> or f<frame>)')
    ap.add_argument('--load', metavar='SNAP', help='resume from this snapshot')
    ap.add_argument('--stop-part', default='', help='stop where this part starts (exactly where --snap-parts saves its '
                    'snapshot), e.g. p3: a run from snaps/part_p2.snap with --stop-part p3 is the p2 segment')
    ap.add_argument('--debug', type=int, default=0, help='print the first N scheduler steps')
    ap.add_argument('--trace', default='', help='addresses to report the first time they run: pm offsets (hex) '
                    'or SEG:OFF linear real-mode addresses')
    a = ap.parse_args()
    m = Machine(a)
    m.snap_frames = {int(x) for x in a.snap_at.split(',') if x}
    if a.load:
        m.load_snapshot(a.load)
    m.frames = open(a.out + '.frames', 'wb')
    m.frames.write(b'DOPEFRM1' + struct.pack('<IIIIII', FW, FH, REC, 32, 0, 0))
    m.ticks = open(a.out + '.ticks', 'w', buffering=1)
    m.ticks.write('# frame time_s order row speedcount mtick part start flips keychecks mode cpu:ip\n')
    try:
        m.run()
    finally:
        open(a.out + '.dram', 'wb').write(bytes(m.gus.dram))
        m.glog.close()
        m.frames.close()
        m.ticks.close()
        print('unknown ports in', m.st.get('unk_in'), 'out', m.st.get('unk_out'))
        print('GUS register reads', {hex(k): v for k, v in sorted(m.gus.reads.items())})


if __name__ == '__main__':
    main()

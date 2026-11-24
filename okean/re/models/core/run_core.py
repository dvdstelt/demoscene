"""Core scope's copy of run_okean.py: the same run, plus an event log (--events FILE) for the clock analysis.

Events (one line each: kind, frame, line within the refresh (0..624, retrace at 502..503), time, part, the part's
callback count so far, order [0x12b75], row word [0x12b73], player row [0x12b72], tick countdown [0x12b71], music
ticks served, extra):
  cb       entry of the part's retrace callback (the count includes this call)
  irq0     entry of the part's IRQ 0 handler (extra: retrace bit at entry, the count word before the handler adds)
  top      main loop top (the draw of a new frame starts)
  copy     the copy of the draw buffer to video memory is done (the instruction after rep movsd)
  dac      out 3C8h (a palette write starts; extra: the start index)
  tick     entry of the player tick 0x1343c
  psel     the end part's page select (0x4d3c8, after its noise fill)
  print    entry of the text printer 0x4cacc (extra: page pointer [0x4c8e4]) and 'printx' at its two returns
  install / measure / restore / load: the timer copy's routines and the measure's count load (extra: the count)
  rec      a record of the capture is written (end of the refresh)

Original docstring:
Run The Sea Robot of Love (KORSO2.EXE) headless in unicorn and record what it does.

KORSO2.EXE = Tran's PMODE 3.x + one 32-bit program (main, a ProTracker player with a GUS driver, the parts), all
in PMODE's code32 segment (see NOTES.md). No PMODE kernel and no DOS are run: one unicorn CPU in 32-bit protected
mode runs code32 (base code32a, flat 4 GB selectors 08h/10h, 18h = base 0, like PMODE's selcode/seldata/selzero)
from PMODE's 32-bit entry (code32 0x259). PMODE's services are emulated: int 33h (real-mode interrupt, register
image at code32 0xc4: only int 10h mode sets), int 31h 090x (interrupt flag), getirqvect / setirqvect (code32
0xa6b / 0xa91) are intercepted, and the exit (0x3f3) ends the run. PMODE's other routines (getmem, the IRQ mask
0x2fe, the real-mode IRQ pass-up 0x346) run as they are.

Interrupts: IRQ 0 (the timer; every part sets its own handler) and IRQ 7 (the GUS, ULTRASND=240,3,3,7,7) are
taken like the CPU does (EFLAGS, CS, EIP pushed on the current stack, IF cleared) and their handlers run as part of
the main line, at the same speed. The PIC is modelled with request and in-service bits: requests on rising edges
(the PIT's OUT in mode 0, the GUS IRQ line), priorities, non-specific and specific EOI, the mask. Nesting matters:
the player's timer handler sends its EOI, enables interrupts and then waits for a volume-ramp interrupt.

Time: emulated seconds. The main line runs --mips million instructions per second (default 100) in quanta.
Polling the VGA status port without progress fast-forwards to the next change of the retrace bits; I/O reads see
the time at the start of the quantum plus what was fast-forwarded (error < one quantum, 0.2 ms by default).

Faked hardware (from dope/re/run_dope.py):
  VGA      registers (misc, sequencer, CRTC, graphics, attribute, DAC), 256 KB planar memory. Chain-4 modes use plain
           RAM at A0000; unchained modes use an MMIO region with write modes 0-3 and latches. Refresh timing from
           the CRTC/misc registers: mode 13h 70.09 Hz; the intro's own "50 Hz" mode (main 0x100f0: 808 dots x 625
           lines at 25.175 MHz) 49.85 Hz. Status bit 3 = vertical retrace, bit 0 = vertical blank.
  PIT      channel 0, modes 0/2/3 (mode 3 counts down by 2), latches.
  PIC      masks only (no in-service tracking).
  GUS      at 0x240, IRQ 7: register file, 1 MB DRAM, timers 1/2 (AdLib-style control at 2x8/2x9), voice position and
           volume ramp simulation, IRQ status 2x6, IRQ source 0x8F.
  keyboard none pressed (port 60h reads 0).

Outputs (prefix --out), one record per displayed video frame:
  <out>.frames  header 32 bytes: b'OKEANFR1', <IIIIII: max width W, max height H, record bytes, header bytes, 0, 0>;
                then per frame: <HHHH width, height, flags (1 = graphics), 0>, 768 bytes palette (6-bit DAC values),
                W*H bytes of pixel indices (the visible width x height in the top left, rest 0)
  <out>.ticks   per frame: frame, time (s) at the frame start, music order, row, speed counter, music tick, part,
                start address used, CRTC start writes so far, timer IRQs so far, mode, main EIP
  <out>.gus     GUS register / port writes: `<music tick> v<voice> r<reg> <value>` / `<music tick> p<port> <value>`,
                plus `<music tick> irq <status> <time>` before each handler run (t = timer, r = ramp, w = wave);
                music tick = GUS timer-2 interrupts served so far
  <out>.dram    final GUS DRAM (1 MB)
  <out>.log     (stdout) events: parts, modes, IRQ vectors

usage: python3 run_okean.py --out full --snap-parts [--mips 100] [--frames N] [--snap-at F1,F2] [--args -M]
       python3 run_okean.py --load snaps/part_2c26f.snap --out x [--frames N]
"""
import argparse
import ctypes
import os
import pickle
import struct
import sys

import numpy as np
from unicorn import (Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_PROT_ALL, UC_HOOK_INTR, UC_HOOK_CODE, UC_HOOK_INSN)
from unicorn.x86_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
# okean/re (the folder with unpacked/), also when this script is a copy in okean/re/models/<scope>/
RE = next((d for d in (HERE, os.path.dirname(HERE), os.path.dirname(os.path.dirname(HERE)))
           if os.path.exists(os.path.join(d, 'unpacked', 'korso2.bin'))), HERE)
UNP = os.path.join(RE, 'unpacked')

PIT_HZ = 1193182.0
ENVSEG = 0x07f0               # environment block
PSP = 0x0800                  # PSP; the program is loaded right after it
LOAD = PSP + 0x10             # load segment
MEMTOP = 0xA000
RAM = 0x1000000               # 16 MB
XMS = 0x110000                # PMODE's extended memory ("himem") from here
C16 = LOAD * 16               # code16a
C32 = (LOAD + 0xec) * 16      # code32a (PMODE's 32-bit segment base)
LOWHEAP = (LOAD + 0x5454) * 16 + 0x1c00 + 0x1600   # after the image, PMODE's stack (5454:1c00) and its tables
GDT = 0xF8000
PM_STACK = (LOAD + 0x5454) * 16 + 0x1c00          # the program's stack top (linear)
PM_ISTACK = 0xFE000           # IRQ stack top (linear)
PM_SENT = 0xFF000             # IRQ return sentinel (linear)
V86R = 0xc4                   # PMODE's real-mode register image (DPMI layout) in code32
PM_ENTRY = 0x259
PM_GETIRQ, PM_SETIRQ, PM_EXIT = 0xa6b, 0xa91, 0x3f3
MAIN = 0x100c4
# main's part calls (code32 0x101c1..0x1022f): the routine main is in names the part
PARTS = [0x38469, 0x2c26f, 0x2e779, 0x3554b, 0x367e5, 0x38520, 0x3c8da, 0x5334f, 0x4d370]
PLAYER = {'order': 0x12b75, 'row': 0x12b72, 'count': 0x12b71}   # player variables (code32 bytes)

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


# ---- the machine ----------------------------------------------------------------------------------------------------

def desc(base, access, flags=0xcf):
    return struct.pack('<HHBBBB', 0xffff, base & 0xffff, (base >> 16) & 0xff, access, flags, (base >> 24) & 0xff)


V86 = (('edi', 0, 4), ('esi', 4, 4), ('ebp', 8, 4), ('ebx', 0x10, 4), ('edx', 0x14, 4), ('ecx', 0x18, 4),
       ('eax', 0x1c, 4), ('flags', 0x20, 2), ('es', 0x22, 2), ('ds', 0x24, 2), ('fs', 0x26, 2), ('gs', 0x28, 2))


class Machine:
    def __init__(self, a):
        self.a = a
        self.buf = ctypes.create_string_buffer(RAM)
        ptr = ctypes.addressof(self.buf)
        self.pm = Uc(UC_ARCH_X86, UC_MODE_32)
        self.pm.mem_map_ptr(0, 0xa0000, UC_PROT_ALL, ptr)
        self.pm.mem_map(0xa0000, 0x10000)                        # VGA window (RAM in chain-4 modes)
        self.pm.mem_map_ptr(0xb0000, RAM - 0xb0000, UC_PROT_ALL, ptr + 0xb0000)
        self.vga = Vga()
        self.pit = Pit()
        self.glog = open(a.out + '.gus', 'w')
        self.glog.write('# <music tick> v<voice> r<reg> <value> | p<port> <value> | irq <t/r/w> <time s>\n')
        self.gus = Gus(self.glog)
        self.st = {'now': 0.0, 'skew': 0.0, 'stop': False, 'pic': [0xb8, 0xff], 'irq0': False, 'irqv': {},
                   'fs': 0.0, 'vr_t': None, 'frame': 0, 'part': 'init', 'mtick': 0, 'stop_t': None, 'poll': None,
                   'irr': 0, 'isr': 0, 'gus_line': False, 'timer_irqs': 0, 'hlt_pc': None}
        self.frames = None
        self.ticks = None
        self.snap_frames = set()
        self.init_memory()
        self.hooks()

    def wr(self, lin, data):
        self.pm.mem_write(lin, bytes(data))

    def rd(self, lin, n):
        return bytes(self.pm.mem_read(lin, n))

    def init_memory(self):
        img = bytearray(open(os.path.join(UNP, 'korso2.bin'), 'rb').read())
        for line in open(os.path.join(UNP, 'korso2.txt')):
            f = line.split()
            if len(f) >= 2 and ':' in f[0] and f[1].startswith('0x'):
                seg, off = (int(x, 16) for x in f[0].split(':'))
                x = seg * 16 + off
                struct.pack_into('<H', img, x, (struct.unpack_from('<H', img, x)[0] + LOAD) & 0xffff)
        self.wr(LOAD * 16, img)
        bda = bytearray(256)
        struct.pack_into('<H', bda, 0x10, 0x0026)
        struct.pack_into('<H', bda, 0x13, 640)
        bda[0x49] = 3
        struct.pack_into('<H', bda, 0x4a, 80)
        struct.pack_into('<H', bda, 0x63, 0x3d4)
        bda[0x84] = 24
        self.wr(0x400, bda)
        env = b'COMSPEC=C:\\COMMAND.COM\x00ULTRASND=240,3,3,7,7\x00PATH=C:\\\x00\x00\x01\x00C:\\OKEAN\\KORSO2.EXE\x00'
        self.wr(ENVSEG * 16, env)
        psp = bytearray(256)
        psp[0:2] = b'\xcd\x20'
        struct.pack_into('<H', psp, 2, MEMTOP)
        struct.pack_into('<H', psp, 0x16, PSP)
        struct.pack_into('<H', psp, 0x2c, ENVSEG)
        tail = (' ' + self.a.args if self.a.args else '').encode()
        psp[0x80] = len(tail)
        psp[0x81:0x81 + len(tail)] = tail
        psp[0x81 + len(tail)] = 0x0d
        self.wr(PSP * 16, psp)
        self.wr(GDT, b'\0' * 8 + desc(C32, 0x9a) + desc(C32, 0x92) + desc(0, 0x92))
        self.pm.reg_write(UC_X86_REG_GDTR, (0, GDT, 4 * 8 - 1, 0))
        self.wr(C32, struct.pack('<IIIIIII', LOWHEAP - C32, MEMTOP * 16 - C32, XMS - C32, RAM - C32, PSP * 16, C16,
                                 C32))
        self.pm.reg_write(UC_X86_REG_CS, 8)
        for r in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_FS, UC_X86_REG_SS):
            self.pm.reg_write(r, 0x10)
        self.pm.reg_write(UC_X86_REG_GS, 0x18)
        self.pm.reg_write(UC_X86_REG_ESP, PM_STACK - C32)
        self.pm.reg_write(UC_X86_REG_EFLAGS, 0x2)
        self.pm.reg_write(UC_X86_REG_EIP, PM_ENTRY)

    # ---- hooks ----------------------------------------------------------------------------------------------------
    def hooks(self):
        pm = self.pm
        pm.hook_add(UC_HOOK_INTR, self.pm_intr)
        for a in (PM_SETIRQ, PM_GETIRQ, PM_EXIT):
            pm.hook_add(UC_HOOK_CODE, self.pm_service, None, C32 + a, C32 + a)
        for p in PARTS:
            pm.hook_add(UC_HOOK_CODE, self.on_part, p, C32 + p, C32 + p)
        pm.hook_add(UC_HOOK_INSN, self.on_in, None, 1, 0, UC_X86_INS_IN)
        pm.hook_add(UC_HOOK_INSN, self.on_out, None, 1, 0, UC_X86_INS_OUT)
        for x in filter(None, self.a.trace.split(',')):
            lin = C32 + int(x, 16)
            pm.hook_add(UC_HOOK_CODE, self.on_trace, x, lin, lin)


    # ---- core scope: event log ---------------------------------------------------------------------------------
    TIMERS = {  # timer copy base (callback pointer) -> part
        0x14be0: 0x2c26f, 0x2c570: 0x2e779, 0x2ede0: 0x3554b, 0x35900: 0x367e5, 0x36cf0: 0x38520,
        0x38be0: 0x3c8da, 0x4d710: 0x5334f, 0x3cd70: 0x4d370}
    LOOPS = {  # part -> (main loop top, instruction after the copy's rep movsd)
        0x2c26f: (0x2c314, 0x2c458), 0x2e779: (0x2e9ca, 0x2eb5e), 0x3554b: (0x35590, 0x35790),
        0x367e5: (0x36943, 0x36ad1), 0x38520: (0x3872e, 0x38962), 0x3c8da: (0x3c94b, 0x3cab0),
        0x5334f: (0x53391, 0x5353d), 0x4d370: (0x4d389, 0x4d597)}

    def ev_hooks(self):
        pm = self.pm
        self.evf = open(self.a.events, 'w', buffering=1 << 16)
        self.evf.write('# kind frame line time part cbcount order row12b73 row12b72 countdown mtick extra\n')
        self.cbn = {}
        def add(a, kind, arg=None):
            pm.hook_add(UC_HOOK_CODE, self.on_ev, (kind, arg), C32 + a, C32 + a)
        for base, part in self.TIMERS.items():
            d = base - 0x14be0
            cb = struct.unpack('<I', self.rd(C32 + base, 4))[0]
            add(cb, 'cb', part)
            add(0x14cb8 + d, 'irq0', d)
            add(0x14bec + d, 'install', part)
            add(0x14c67 + d, 'measure', part)
            add(0x14c40 + d, 'restore', part)
            add(0x14cac + d, 'load', d)
        for part, (top, cp) in self.LOOPS.items():
            add(top, 'top', part)
            add(cp, 'copy', part)
        add(0x1343c, 'tick')
        add(0x4cacc, 'print')
        add(0x4cbda, 'printx', 'advance')
        add(0x4cbe2, 'printx', 'same')
        add(0x4cbfa, 'printx', 'neworder')
        add(0x4d3c8, 'psel')          # the end part reads the order for its page select here

    def ev(self, kind, extra=''):
        st = self.st
        t = self.t_io()
        lp = self.vga.timing()[0]
        line = (t - st['fs']) / lp
        r = self.rd(C32 + 0x12b71, 5)
        self.evf.write('%s %d %.2f %.6f %s %d %d %d %d %d %d %s\n' % (
            kind, st['frame'], line, t, st['part'], self.cbn.get(st['part'], 0), r[4], r[2] | r[3] << 8, r[1],
            r[0], st['mtick'], extra))

    def on_ev(self, uc, addr, size, arg):
        kind, a = arg
        extra = ''
        if kind == 'cb':
            k = '%05x' % a
            self.cbn[k] = self.cbn.get(k, 0) + 1
        elif kind == 'irq0':
            cnt = struct.unpack('<H', self.rd(C32 + 0x14d0d + a, 2))[0]
            extra = 'retrace=%d count=%d' % (self.retrace_status(self.t_io()) >> 3 & 1, cnt)
        elif kind == 'load':
            extra = 'count=%d' % (uc.reg_read(UC_X86_REG_EBX) & 0xffff)
        elif kind == 'print':
            extra = '%05x' % struct.unpack('<I', self.rd(C32 + 0x4c8e4, 4))[0]
        elif kind == 'printx':
            extra = '%s %05x' % (a, struct.unpack('<I', self.rd(C32 + 0x4c8e4, 4))[0])
        self.ev(kind, extra)

    def t_io(self):
        return self.st['now'] + self.st['skew']

    def on_trace(self, uc, addr, size, name):
        print('trace %s frame %d t %.4f' % (name, self.st['frame'], self.t_io()), flush=True)

    def on_part(self, uc, addr, size, p):
        st = self.st
        if '%05x' % p in st.setdefault('started', []):         # resumed at the hook after the snapshot stop
            return
        st['started'].append('%05x' % p)
        st['part'] = '%05x' % p
        print('part %05x starts: frame %d, time %.4f, music %s' % (p, st['frame'], self.t_io(), self.music()),
              flush=True)
        if self.a.snap_parts:
            st['snap_req'] = 'part_%05x' % p
            uc.emu_stop()

    def stop_at(self, t):
        self.st['stop_t'] = t
        self.pm.emu_stop()

    # ---- PMODE services -----------------------------------------------------------------------------------------
    def pm_intr(self, uc, intno, _):
        pm = self.pm
        if intno == 0x33:                                        # real-mode interrupt, register image at V86R
            img = self.rd(C32 + V86R, 0x2a)
            r = {k: int.from_bytes(img[o:o + s], 'little') for k, o, s in V86}
            n = pm.reg_read(UC_X86_REG_EAX) & 0xff
            if n == 0x10:
                self.int10(r)
            else:
                print('int 33h -> int %02x AX=%04x (ignored), frame %d' % (n, r['eax'] & 0xffff, self.st['frame']),
                      flush=True)
            b = bytearray(img)
            for k, o, s in V86:
                b[o:o + s] = (r[k] & ((1 << (8 * s)) - 1)).to_bytes(s, 'little')
            self.wr(C32 + V86R, b)
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
            print('int 31h AX=%04x unhandled' % ax, flush=True)
            pm.reg_write(UC_X86_REG_EFLAGS, fl | 1)
            return
        eip = pm.reg_read(UC_X86_REG_EIP)
        print('int %02xh at %05x: stop (frame %d)' % (intno, eip, self.st['frame']), flush=True)
        self.st['stop'] = True
        pm.emu_stop()

    def int10(self, r):
        ax = r['eax'] & 0xffff
        ah, al = ax >> 8, ax & 0xff
        if ah == 0:
            m = al & 0x7f
            if m == 0x13:
                self.vga.set_mode(MODE13)
                self.vga.graphics = True
            else:
                self.vga.set_mode(MODE3)
                self.vga.graphics = False
            if not al & 0x80:
                self.wr(0xa0000, bytes(0x10000))
                self.vga.planes[:] = 0
            if self.vga.chain4_ram != (self.vga.chain4() or not self.vga.graphics):
                self.vga.remap = True
                self.pm.emu_stop()
            print('video mode %02x (frame %d, time %.4f)' % (al, self.st['frame'], self.t_io()), flush=True)
        else:
            print('int 10h AX=%04x ignored' % ax, flush=True)

    def pm_service(self, uc, addr, size, _):
        pm = self.pm
        a = addr - C32
        if a == PM_EXIT:
            print('exit (frame %d, time %.3f)' % (self.st['frame'], self.t_io()), flush=True)
            self.st['stop'] = True
            pm.emu_stop()
            return
        bl = pm.reg_read(UC_X86_REG_EBX) & 0xff
        if a == PM_SETIRQ:
            edx = pm.reg_read(UC_X86_REG_EDX)
            self.st['irqv'][bl] = edx
            print('setirqvect IRQ %d -> %05x (frame %d, part %s)' % (bl, edx, self.st['frame'], self.st['part']),
                  flush=True)
        else:
            pm.reg_write(UC_X86_REG_EDX, self.st['irqv'].get(bl, 0xdead0000 + bl))
        esp = pm.reg_read(UC_X86_REG_ESP)
        ret = struct.unpack('<I', self.rd(C32 + esp, 4))[0]
        pm.reg_write(UC_X86_REG_ESP, esp + 4)
        pm.reg_write(UC_X86_REG_EIP, ret)

    def raise_irq(self, irq):
        """take interrupt irq like the CPU does: push EFLAGS, CS, EIP on the current stack (PMODE runs everything at
        ring 0, so there is no stack switch), clear IF and TF, continue at the handler"""
        pm = self.pm
        st = self.st
        handler = st['irqv'][irq]
        esp = (pm.reg_read(UC_X86_REG_ESP) - 12) & 0xffffffff
        fl = pm.reg_read(UC_X86_REG_EFLAGS)
        self.wr(C32 + esp, struct.pack('<III', pm.reg_read(UC_X86_REG_EIP), 8, fl))
        pm.reg_write(UC_X86_REG_ESP, esp)
        pm.reg_write(UC_X86_REG_EFLAGS, fl & ~0x300)
        pm.reg_write(UC_X86_REG_EIP, handler)
        st['isr'] |= 1 << irq

    def gus_line(self):
        """the GUS IRQ line; a rising edge sets IRQ 7's request bit (the PIC is edge-triggered)"""
        up = self.gus.irq()
        if up and not self.st['gus_line']:
            self.st['irr'] |= 0x80
        self.st['gus_line'] = up

    # ---- ports ------------------------------------------------------------------------------------------------------
    def on_in(self, uc, port, size, _):
        t = self.t_io()
        vga = self.vga
        if 0x240 <= port <= 0x24f or 0x340 <= port <= 0x347:
            self.gus.now = t
            v = self.gus.inp(port, size)
            self.gus_line()
            return v
        if port == 0x3da:
            vga.ac_ff = 0
            v = self.retrace_status(t)
            ip = uc.reg_read(UC_X86_REG_EIP)
            last = self.st['poll']
            if last == (ip, v, t):
                # polled again without progress: jump to the next change of the status bits
                tn = self.retrace_next(t)
                te = self.next_event_time()
                if te < tn:
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
            self.gus_line()
            return
        if size == 2 and port in (0x3c4, 0x3d4, 0x3ce):
            self.on_out(uc, port, 1, val & 0xff, None)
            self.on_out(uc, port + 1, 1, (val >> 8) & 0xff, None)
            return
        if size == 2 and port == 0x3c0:
            self.on_out(uc, port, 1, val & 0xff, None)
            self.on_out(uc, port, 1, (val >> 8) & 0xff, None)
            return
        if size == 2 and port in (0x3c8, 0x3c9):
            self.on_out(uc, port, 1, val & 0xff, None)
            self.on_out(uc, port + 1, 1, (val >> 8) & 0xff, None)
            return
        val &= 0xff
        if port == 0x3c8:
            if getattr(self, 'evf', None):
                self.ev('dac', str(val))
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
                    self.pm.emu_stop()
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
            print('misc output %02x (frame %d): refresh %.4f Hz' % (val, self.st['frame'], self.refresh_hz()),
                  flush=True)
        elif port == 0x43:
            self.pit.write43(val, t)
        elif port == 0x40:
            self.pit.write40(val, t)
        elif port == 0x21:
            self.st['pic'][0] = val
        elif port == 0xa1:
            self.st['pic'][1] = val
        elif port == 0x20:
            if val == 0x20:                                     # non-specific EOI: the highest-priority ISR bit
                isr = self.st['isr']
                if isr:
                    self.st['isr'] = isr & (isr - 1)
            elif val & 0xe0 == 0x60:                            # specific EOI
                self.st['isr'] &= ~(1 << (val & 7))
        elif port in (0xa0, 0x80, 0x61, 0x42, 0x3c6, 0x3d9):
            pass
        else:
            cnt = self.st.setdefault('unk_out', {})
            cnt[port] = cnt.get(port, 0) + 1

    # ---- VGA timing and memory mapping -------------------------------------------------------------------------------
    def refresh_hz(self):
        lp, vtotal, vrs, vrlen, vde = self.vga.timing()
        return 1.0 / (lp * vtotal)

    def retrace_status(self, t):
        lp, vtotal, vrs, vrlen, vde = self.vga.timing()
        line = ((t - self.st['fs']) / lp + 1e-6) % vtotal
        v = 0
        if vrs <= line < vrs + vrlen:
            v |= 8
        if line >= vde:
            v |= 1
        return v

    def retrace_next(self, t):
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
        pm = self.pm
        a = np.arange(0x10000)
        if want_ram:
            lin = np.empty(0x10000, np.uint8)
            lin[:] = vga.planes[a & 3, a & 0xfffc]
            pm.mem_unmap(0xa0000, 0x10000)
            pm.mem_map(0xa0000, 0x10000)
            pm.mem_write(0xa0000, lin.tobytes())
        else:
            lin = np.frombuffer(bytes(pm.mem_read(0xa0000, 0x10000)), np.uint8)
            vga.planes[a & 3, a & 0xfffc] = lin
            pm.mem_unmap(0xa0000, 0x10000)
            pm.mmio_map(0xa0000, 0x10000, self.mmio_read, None, self.mmio_write, None)
        vga.chain4_ram = want_ram
        print('VGA %s (frame %d, part %s)' % ('chain-4' if want_ram else 'unchained (planar)', self.st['frame'],
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
        if not PLAYER:
            return '-'
        return '%02x:%02x' % (self.rd(C32 + PLAYER['order'], 1)[0], self.rd(C32 + PLAYER['row'], 1)[0])

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
            st['irr'] |= 1
            self.pit.fire()
        self.gus.advance(t)
        self.gus_line()

    def record(self):
        st = self.st
        if getattr(self, 'evf', None):
            self.ev('rec', '%d' % st['timer_irqs'])
        vga = self.vga
        hdr = bytearray(8)
        img = np.zeros((FH, FW), np.uint8)
        if vga.graphics:
            ram = self.rd(0xa0000, 0x10000) if vga.chain4_ram else None
            fr = vga.render(ram)
            img[:fr.shape[0], :fr.shape[1]] = fr
            w, h = fr.shape[1], fr.shape[0]
        else:
            w = h = 0
        struct.pack_into('<HHHH', hdr, 0, w, h, 1 if vga.graphics else 0, 0)
        if st['frame'] >= self.a.start_frame:
            self.frames.write(bytes(hdr) + bytes(vga.dac) + img.tobytes())
        if PLAYER:
            order, row, cnt = (self.rd(C32 + PLAYER[k], 1)[0] for k in ('order', 'row', 'count'))
        else:
            order = row = cnt = 0
        where = '%05x' % self.pm.reg_read(UC_X86_REG_EIP)
        mode = '%dx%d%s' % (w, h, 'c4' if vga.chain4() else 'x') if vga.graphics else 'text'
        self.ticks.write('%d %.6f %d %d %d %d %s %04x %d %d %s %s\n' % (
            st['frame'], st['fs'], order, row, cnt, st['mtick'], st['part'], vga.latched_start, vga.start_writes,
            st['timer_irqs'], mode, where))
        if st['frame'] % 500 == 0:
            print('frame %d time %.2f music %s part %s eip %s flips %d timer irqs %d' % (
                st['frame'], st['fs'], self.music(), st['part'], where, vga.start_writes, st['timer_irqs']),
                flush=True)
        if st['frame'] in self.snap_frames:
            st['snap_req'] = 'f%d' % st['frame']

    def deliver(self):
        """take the highest-priority pending interrupt the PIC and IF allow; True if one is pending but blocked"""
        st = self.st
        self.gus.advance(st['now'])
        self.gus_line()
        irr = st['irr'] & ~st['pic'][0] & 0xff
        if not irr:
            return False
        irq = (irr & -irr).bit_length() - 1
        if st['isr'] and (st['isr'] & -st['isr']) <= 1 << irq:   # one of the same or higher priority in service
            return True
        if not self.pm.reg_read(UC_X86_REG_EFLAGS) & 0x200:
            return True
        st['irr'] &= ~(1 << irq)
        if irq not in st['irqv'] or st['irqv'][irq] >= 0xdead0000:
            return False
        if irq == 0:
            st['timer_irqs'] += 1
        else:
            g = self.gus
            kind = ('t' if g.status & 0xc else '') + ('r' if g.status & 0x40 else '') + ('w' if g.status & 0x20 else '')
            if g.status & 8:
                st['mtick'] += 1
                g.tick = st['mtick']
            self.glog.write('%d irq %s %.6f\n' % (g.tick, kind, st['now']))
        self.raise_irq(irq)
        return False

    def run(self):
        st = self.st
        a = self.a
        ips = a.mips * 1e6
        quantum = a.quantum
        while not st['stop'] and st['frame'] < a.frames:
            if st.get('snap_req'):
                self.save_snapshot(st.pop('snap_req'))
            if self.vga.remap:
                self.apply_remap()
            te = self.next_event_time()
            if st['now'] >= te - 1e-9:
                st['now'] = max(st['now'], te)                   # the event's own time (models compare exactly)
                self.process_events()
                continue
            blocked = self.deliver()
            if st['stop']:
                break
            n = int(min(quantum, (te - st['now']) * ips + 1))
            n = max(n, 1)
            if blocked:
                n = min(n, 300)
            st['skew'] = 0.0
            st['stop_t'] = None
            pm = self.pm
            try:
                pm.emu_start(pm.reg_read(UC_X86_REG_EIP), 0xffffffff, count=n)
            except UcError as e:
                print('error at %05x: %s (frame %d, part %s)' % (pm.reg_read(UC_X86_REG_EIP), e, st['frame'],
                                                                  st['part']), flush=True)
                break
            if st['stop_t'] is not None:
                st['now'] = max(st['now'], st['stop_t'])
            else:
                st['now'] += n / ips + st['skew']
            st['skew'] = 0.0
            pc = pm.reg_read(UC_X86_REG_EIP)
            if self.rd(C32 + pc - 1, 1) == b'\xf4':                  # hlt: wait for the next event
                if st['hlt_pc'] == pc:
                    st['now'] = max(st['now'], self.next_event_time())
                st['hlt_pc'] = pc
            else:
                st['hlt_pc'] = None
        print('ended at frame %d, time %.2f s, part %s, music %s' % (st['frame'], st['now'], st['part'],
                                                                    self.music()), flush=True)

    # ---- snapshots ------------------------------------------------------------------------------------------------
    def save_snapshot(self, tag):
        path = self.a.snap.format(frame=self.st['frame'], tag=tag)
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        vram = self.rd(0xa0000, 0x10000) if self.vga.chain4_ram else None
        d = {'mem': self.buf.raw, 'vram': vram, 'pm': self.pm.context_save(), 'st': dict(self.st),
             'vga': dict(self.vga.__dict__), 'pit': dict(self.pit.__dict__),
             'gus': self.gus.__getstate__(), 'voices': [dict(v.__dict__) for v in self.gus.v]}
        with open(path, 'wb') as f:
            pickle.dump(d, f, protocol=4)
        print('snapshot %s at frame %d' % (path, self.st['frame']), flush=True)

    def load_snapshot(self, path):
        d = pickle.load(open(path, 'rb'))
        ctypes.memmove(self.buf, d['mem'], RAM)
        self.pm.context_restore(d['pm'])
        self.st.update(d['st'])
        self.vga.__dict__.update(d['vga'])
        self.pit.__dict__.update(d['pit'])
        self.gus.__dict__.update(d['gus'])
        self.gus.v = [Voice() for _ in range(32)]
        for v, vd in zip(self.gus.v, d['voices']):
            v.__dict__.update(vd)
        if not self.vga.chain4_ram:
            self.pm.mem_unmap(0xa0000, 0x10000)
            self.pm.mmio_map(0xa0000, 0x10000, self.mmio_read, None, self.mmio_write, None)
        else:
            self.pm.mem_write(0xa0000, d['vram'])
        print('resumed at frame %d (part %s, music %s)' % (self.st['frame'], self.st['part'], self.music()),
              flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--mips', type=float, default=100.0, help='main-line speed, million instructions per second')
    ap.add_argument('--quantum', type=int, default=20000, help='instructions per emulation slice')
    ap.add_argument('--frames', type=int, default=1 << 30, help='stop at this video frame')
    ap.add_argument('--start-frame', type=int, default=0, help='only write frames from this one on')
    ap.add_argument('--snap-at', default='', help='snapshots at these frames (comma-separated)')
    ap.add_argument('--snap-parts', action='store_true', help='snapshot when main calls each part')
    ap.add_argument('--snap', default=os.path.join(HERE, 'snaps', '{tag}.snap'),
                    help='where snapshots are saved ({frame}, {tag} = part_<addr> or f<frame>)')
    ap.add_argument('--load', metavar='SNAP', help='resume from this snapshot')
    ap.add_argument('--args', default='', help='command line of the program (e.g. -M: no 50 Hz mode)')
    ap.add_argument('--events', default='', help='core scope: write the event log here')
    ap.add_argument('--no-frames', action='store_true', help='do not write the .frames file')
    ap.add_argument('--trace', default='', help='code32 offsets (hex) to report the first time they run')
    a = ap.parse_args()
    m = Machine(a)
    m.snap_frames = {int(x) for x in a.snap_at.split(',') if x}
    if a.load:
        m.load_snapshot(a.load)
    if a.events:
        m.ev_hooks()
    if a.no_frames:
        a.start_frame = 1 << 30
    m.frames = open(a.out + '.frames', 'wb')
    m.frames.write(b'OKEANFR1' + struct.pack('<IIIIII', FW, FH, REC, 32, 0, 0))
    m.ticks = open(a.out + '.ticks', 'w', buffering=1)
    m.ticks.write('# frame time_s order row speedcount mtick part start flips timer_irqs mode eip\n')
    try:
        m.run()
    finally:
        open(a.out + '.dram', 'wb').write(bytes(m.gus.dram))
        m.glog.close()
        m.frames.close()
        m.ticks.close()
        if getattr(m, 'evf', None):
            m.evf.close()
        print('unknown ports in', m.st.get('unk_in'), 'out', m.st.get('unk_out'))


if __name__ == '__main__':
    main()

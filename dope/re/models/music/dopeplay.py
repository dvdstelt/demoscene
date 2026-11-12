"""Reference implementation of DOPE's music: Oskari Tammelin's "ProTracker for GUS replay routine" (1994) in the
loader's 32-bit code, GUS path, ported routine by routine (see ../../notes-music.md), plus a GF1 model to render it.

From DOPE.MOD and unpacked/loader.bin (the player's tables) it produces
  - the GUS DRAM image (GUS init 0x45b4: DRAM probe; module load 0x2d15: PIO upload 0x4450)   -> full.dram
  - the GUS register / port write log in run_dope.py's text format, from the GUS init to the stop -> full.gus
  - the music position (order index, row, tick countdown) after every timer interrupt             -> full.ticks
and with --wav renders the song through a GF1 model (28 active voices: 22,050 frames per second) with the
interrupts placed where the GUS raises them (timer 2 overflows, volume-ramp ends).

usage:  python3 dopeplay.py                      # check against ../../full.gus / full.dram / full.ticks
        python3 dopeplay.py --out PREFIX         # also write PREFIX.gus and PREFIX.dram
        python3 dopeplay.py --ideal --out PREFIX # interrupts at their hardware times (no CPU latency)
        python3 dopeplay.py --times FILE         # per tick: time after the first row tick, order, row
        python3 dopeplay.py --info               # module, instruments, DRAM layout, effects used
        python3 dopeplay.py --wav dope_music.wav # GF1 render from the first row tick (needs numpy)
        (run inside the toolbox from this folder)

The capture's interrupt delivery: the emulator (run_dope.py) raises the GUS IRQ at the exact time of the event
but serves it only when the interrupted code has interrupts enabled. The register stream depends on that only
through the order of events, so the check replays the capture's delivery times (the `irq` lines of full.gus) and
verifies that each one is at or after the event that caused it. --ideal serves every interrupt at its event.

Routine map (code32 offsets = loader.bin offset - 0x17e0):
  0x45b4 GUS init (0x490a ULTRASND=, 0x498b menu)   0x2d15 MOD load + upload (0x4450)   0x4587 pan table
  0x2c17 start (channel reset, 0x4369)   0x2cff stop (0x43d6, 0x4545)
  0x4040 GUS IRQ: timer 2 -> 0x41e9 (0x22c4 tick, tempo -> count, 0x421a note-off ramps, 0x4299 voice update);
          volume ramp -> 0x40d8 (switch the sample, 0x4299 when the last pending ramp has ended)
  0x22c4 tick: row (effect tables 0x2579, Exy 0x25c1) or tick effects (0x2601, Exy 0x2649)
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))

C32 = 0x17e0                  # file offset of code32 in loader.bin
BASE = 0x240                  # ULTRASND=240,3,3,7,7 (the capture's environment)
IRQ_NO = 7
GF1_CLOCK = 1e6 / 1.619695497  # 617,400 voice frames per second / active voices

# code32 addresses of the player's static data
A_C2SPD = 0xb37               # 16 u16: C-2 rates per finetune 0..7, -8..-1 (8363, 8413, ..)
A_PERIODS = 0xb5b             # 96 u16: Amiga periods, 8 octaves (6848 .. 28; entries 4 and 6 are 3424 / 2832)
A_OCT1 = 0xb8b                # 12 u16 = A_PERIODS[24..35]: 1712 .. 907, the base octave of the note -> period
A_PAN = 0xd7d                 # 32 u8: initial GF1 pan per voice (5 10 10 5 ...)
A_INS = 0x11fd                # 31 instrument records of 0x46 bytes
A_SINE = 0x22a0               # 32 u8: vibrato half sine; 4xx reads 64 bytes from here (see vib_table)
A_LATCH = 0x3f66              # 16 u8: IRQ latch per IRQ number
A_VOICES = 0x3f76             # 19 u16: 617400 / n for n = 14..32 (n = 28: 22050)
A_VOLT = 0x3f9c               # 65 u16: volume 0..64 -> GF1 volume (16-bit register 9)
A_COUNT0 = 0x3f2a             # u8 0xc2: timer-2 reload before the first tick computes one (125 BPM)
A_MASTER = 0x1ab6             # u32 0x40: master volume (never changed)
AMIGA4 = 0xd87b50             # code constant at 0x2dc4: 4 x 3,546,836 Hz (PAL Amiga clock x 4)
V16 = {1, 2, 3, 4, 5, 9, 0x0a, 0x0b}


class Img:
    def __init__(self, path):
        self.b = open(path, 'rb').read()

    def u8(self, a): return self.b[C32 + a]
    def u16(self, a): return struct.unpack_from('<H', self.b, C32 + a)[0]
    def u32(self, a): return struct.unpack_from('<I', self.b, C32 + a)[0]


# ------------------------------------------------------------------------------------------------ the fake card
class Voice:
    def __init__(self):
        self.r = {0: 3, 0x0d: 3, 1: 0, 2: 0, 3: 0, 4: 0, 5: 0, 6: 0, 7: 0, 8: 0, 9: 0, 0x0a: 0, 0x0b: 0, 0x0c: 7}
        self.t = 0.0                  # time of vol
        self.vol = 0.0                # 12-bit volume (float, as run_dope.py simulates it)


class Card:
    """The GUS as run_dope.py's class Gus models and logs it (only what this player uses: no positions, no wave
    IRQs). Ports are offsets from the base 0x240. Log lines: '<tick> v<voice> r<reg> <val>' for every register
    write except the DRAM address 0x43 / 0x44 (8-bit writes to a 16-bit register log `latch | byte << 8`, and the
    latch is never set by this player: 0), '<tick> p<port> <val>' for port writes other than 3x2..3x5, 3x7, 2x8,
    '<tick> irq <kind> <time>' before each handler run. The volume ramps run in continuous time at
    rate x 617400 / voices steps per second; reg 0x89 reads return the simulated volume."""

    def __init__(self):
        self.lines = []
        self.tick = 0
        self.now = 0.0
        self.voice = 0
        self.reg = 0
        self.lo = 0
        self.addr = 0
        self.dram = bytearray(1 << 20)
        self.v = [Voice() for _ in range(32)]
        self.g = {0x0e: 13, 0x45: 0, 0x46: 0, 0x47: 0, 0x4c: 0}
        self.status = 0
        self.queue = []
        self.adlib_i = 0
        self.t2_run = False
        self.t2_next = None
        self.hw = None                # optional GF1 model: hw.write(voice, reg, value, size), hw.port(p, v)
        self.lost = 0                 # timer overflows that found the previous one still pending
        self.reordered = 0            # ramp IRQs whose capture order differs from the time order

    def rate(self):
        return GF1_CLOCK / max(14, (self.g[0x0e] & 0x1f) + 1)

    # -------------------------------------------------------------------------------------- time
    def ramp_end(self, vn):
        """time at which voice vn's running ramp reaches its limit (None if no ramp with IRQ runs)"""
        v = self.v[vn]
        rc = v.r[0x0d]
        if rc & 3 or not rc & 0x20:
            return None
        inc = (v.r[6] & 0x3f) / (1, 8, 64, 512)[v.r[6] >> 6]
        d = (v.vol - (v.r[7] << 4)) if rc & 0x40 else ((v.r[8] << 4) - v.vol)
        return v.t + max(d, 0.0) / inc / self.rate()

    def sim(self, vn, t):
        v = self.v[vn]
        dt = t - v.t
        v.t = t
        if dt <= 0:
            return
        rc = v.r[0x0d]
        if not rc & 3:
            inc = (v.r[6] & 0x3f) * dt * self.rate() / (1, 8, 64, 512)[v.r[6] >> 6]
            lo, hi = v.r[7] << 4, v.r[8] << 4
            if rc & 0x40:
                v.vol -= inc
                hit = v.vol <= lo
            else:
                v.vol += inc
                hit = v.vol >= hi
            if hit:
                assert not rc & 8, 'looped ramps are not used by this player'
                v.vol = lo if rc & 0x40 else hi
                v.r[0x0d] |= 1
                if rc & 0x20:
                    v.r[0x0d] |= 0x80
                    if vn not in self.queue:
                        self.queue.append(vn)
            v.vol = min(max(v.vol, 0.0), 4095.0)

    def events(self, t, order=None):
        """run the timer and the ramps up to t, in time order (ramps ending together: voice order). order: the
        voices in the order the capture's card queued them (it sometimes finds several ramp ends in one step
        of its main loop, and then queues them in voice order)"""
        while True:
            cand = []
            if self.t2_run and self.t2_next <= t:
                cand.append((self.t2_next, -1))
            for vn in range(32):
                te = self.ramp_end(vn)
                if te is not None and te + 1e-9 <= t:
                    cand.append((te, vn))
            if not cand:
                break
            te, vn = min(cand)
            if vn < 0:
                if self.g[0x45] & 8:
                    if self.status & 8:
                        self.lost += 1
                    self.status |= 8
                self.t2_next += (256 - self.g[0x47]) * 320e-6
            else:
                self.sim(vn, min(te + 1e-9, t))      # (a hair past the end: float rounding)
                assert self.v[vn].r[0x0d] & 1
        for vn in range(32):
            self.sim(vn, t)
        self.now = t
        if order is not None and self.queue:
            assert sorted(order) == sorted(self.queue), 'queue %s, capture %s at %.6f' % (self.queue, order, t)
            self.reordered += order != self.queue
            self.queue = list(order)
        self.irqbits()

    def next_event(self):
        """earliest pending hardware event after now: (time, kind)"""
        best = (self.t2_next, 't') if self.t2_run else (None, None)
        for vn in range(32):
            te = self.ramp_end(vn)
            if te is not None and (best[0] is None or te < best[0]):
                best = (te, 'r')
        return best

    def irqbits(self):
        self.status &= ~0x60
        for vn in self.queue:
            if self.v[vn].r[0x0d] & 0x80:
                self.status |= 0x40

    def irq(self):
        return bool(self.status & 0x6c)

    # -------------------------------------------------------------------------------------- ports
    def log_port(self, p, val):
        self.lines.append('%d p%03x %x' % (self.tick, BASE + p, val))

    def outb(self, p, val):
        val &= 0xff
        if p == 0x102:
            self.voice = val & 31
        elif p == 0x103:
            self.reg = val
        elif p == 0x104:
            self.lo = val
        elif p == 0x105:
            r = self.reg
            self._write(r, (self.lo | val << 8) if (r in V16 or r == 0x43) else val, 1)
        elif p == 0x107:
            self.dram[self.addr & 0xfffff] = val
        elif p == 0x008:
            self.adlib_i = val
        elif p == 0x009:
            if self.adlib_i == 4:
                if val & 0x80:
                    self.status &= ~0x0c
                else:
                    r2 = bool(val & 2)
                    assert not val & 1, 'timer 1 is not used'
                    if r2 and not self.t2_run:
                        self.t2_next = self.now + (256 - self.g[0x47]) * 320e-6
                    self.t2_run = r2
            self.log_port(p, val)
        else:
            self.log_port(p, val)
        if self.hw is not None and p not in (0x102, 0x103, 0x104, 0x105, 0x107):
            self.hw.port(p, val)

    def outw(self, p, val):
        assert p == 0x104
        self._write(self.reg, val & 0xffff, 2)

    def _write(self, reg, val, size):
        if reg == 0x43:
            self.addr = (self.addr & 0xff0000) | (val & 0xffff)
            return
        if reg == 0x44:
            self.addr = (self.addr & 0xffff) | ((val & 0xff) << 16)
            return
        self.lines.append('%d v%d r%02x %x' % (self.tick, self.voice, reg, val))
        if self.hw is not None:
            self.hw.write(self.voice, reg, val, size)
        if reg < 0x20:
            vn = self.voice
            v = self.v[vn]
            self.sim(vn, self.now)
            if reg == 0:
                v.r[0] = val & 0x7f
            elif reg == 0x0d:
                v.r[0x0d] = val & 0x7f
            elif reg == 0x0e:
                self.g[0x0e] = val
            else:
                v.r[reg] = val
                if reg == 9:
                    v.vol = float(val >> 4)
            if not (v.r[0] & 0x80 or v.r[0x0d] & 0x80) and vn in self.queue:
                self.queue.remove(vn)
            self.irqbits()
        else:
            self.g[reg] = val
            if reg == 0x45:
                if not val & 8:
                    self.status &= ~8
                if not val & 4:
                    self.status &= ~4

    def inb(self, p):
        if p == 0x107:
            return self.dram[self.addr & 0xfffff]
        if p == 0x105:
            return self._read(self.reg) & 0xff
        if p == 0x006:
            return self.status & 0xec
        raise ValueError('read of port %x' % p)

    def inw(self, p):
        assert p == 0x104
        return self._read(self.reg) & 0xffff

    def _read(self, reg):
        if reg == 0x8f:
            if self.queue:
                vn = self.queue.pop(0)
                v = self.v[vn]
                r = vn | 0x20 | (0 if v.r[0] & 0x80 else 0x80) | (0 if v.r[0x0d] & 0x80 else 0x40)
                v.r[0] &= ~0x80
                v.r[0x0d] &= ~0x80
                self.irqbits()
                return r
            return 0xe0
        if reg == 0x89:
            self.sim(self.voice, self.now)
            return int(self.v[self.voice].vol) << 4
        raise ValueError('read of register %x' % reg)

    # helpers in the player's own access pattern
    def w8(self, reg, val):                    # reg -> 3x3, byte -> 3x5
        self.outb(0x103, reg)
        self.outb(0x105, val)

    def w16(self, reg, val):                   # reg -> 3x3, word -> 3x4
        self.outb(0x103, reg)
        self.outw(0x104, val)


# ------------------------------------------------------------------------------------------------ the player
class Ins:
    """instrument record, 0x46 bytes at 0x11fd + 0x46 (i - 1)"""

    def __init__(self):
        self.flags = 0      # +0x31: 1 loop, 2 bidirectional ("ping" name prefix)
        self.len = 0        # +0x32 bytes (0 if <= 2)
        self.ls = 0         # +0x36 loop start
        self.le = 0         # +0x3a loop end = min(ls + replen - 2, len - 1) + 2
        self.vol = 0        # +0x3e (not clamped here)
        self.ft = 0         # +0x3f finetune 0..15
        self.dram = 0       # +0x40 DRAM start (left 0 for empty samples)
        self.c2spd = 0      # +0x44 u16 = word [0xb37 + 2 ft]


class Chan:
    """channel record, 0x3f bytes at 0x1ac0 + 0x3f ch (channel ch plays on GF1 voice ch)"""

    def __init__(self):
        self.start = 0      # +0x04 DRAM start of the sample
        self.lstart = 0     # +0x08 loop start (DRAM)
        self.end = 0        # +0x0c end (DRAM): loop end, or start + length
        self.pan = 6        # +0x10 0..15
        self.flags = 0      # +0x11 1 volume, 2 pan, 4 frequency, 8 (re)start the sample
        self.ctl = 0        # +0x12 GF1 voice control: 8 loop, 0x10 bidirectional
        self.vol = 0        # +0x13 0..64
        self.c_ls = 0xffff  # +0x14 loop start last written
        self.c_end = 0xffff  # +0x18 end last written
        self.c_fc = 0xff    # +0x1c u16 frequency last written
        self.c_pan = 7      # +0x20 pan last written
        self.ins = 0        # +0x21 instrument (1-based; 0-based after an instrument-only cell: quirk)
        self.fc = 0         # +0x22 u16 GF1 frequency
        self.offset = 0     # +0x24 sample offset (9xx), cleared on every row
        self.eff = 0        # +0x28
        self.par = 0        # +0x29
        self.period = 0     # +0x2a u16 current period (Amiga period x 4)
        self.vibpos = 0     # +0x2c
        self.vdepth = 0     # +0x2e vibrato depth (low nibble)
        self.vspeed = 0     # +0x2f vibrato speed (high nibble)
        self.base = 0       # +0x30 u16 the note's period (tone porta target, vibrato centre)
        self.c2spd = 0      # +0x32
        self.arp = [0, 0, 0]  # +0x34, +0x36, +0x38
        self.arpcnt = 0     # +0x3a 0..2, advanced on every tick that is not a row tick
        self.note = 0       # +0x3b octave << 4 | note 1..12
        self.pspeed = 0     # +0x3c tone porta speed
        self.tickcnt = 0    # +0x3d ticks since the row


class Player:
    def __init__(self, img, mod, card, eax=0x808, ebx=0):
        self.img, self.mod, self.c = img, mod, card
        self.c2spd = [img.u16(A_C2SPD + 2 * i) for i in range(16)]
        self.periods = [img.u16(A_PERIODS + 2 * i) for i in range(96)]
        self.volt = [img.u16(A_VOLT + 2 * i) for i in range(65)]
        self.vdiv = [img.u16(A_VOICES + 2 * i) for i in range(19)]
        # 4xx reads 0x22a0 + (pos >> 2), pos 0..255: the sine, then the dword [0x22c0] (EAX when the tick was
        # called: AL = 8 from the IRQ handler, the rest from the interrupted code: 0x808 in the capture), then
        # the first 28 code bytes of the tick routine 0x22c4
        win = bytearray(img.b[C32 + A_SINE:C32 + A_SINE + 64])
        win[32:36] = struct.pack('<I', eax)
        self.vibtab = list(win)
        self.sine = list(win[:32])
        self.ebx = ebx                 # EBX of the interrupted code: bit 15 goes into reg 3 / 5 / 0x0b (0x40d8)
        self.master = img.u32(A_MASTER)
        self.count = img.u8(A_COUNT0)  # [0x3f2a]
        self.rem = img.u8(A_COUNT0 - 1)  # [0x3f29]
        self.tick_fn = None            # [0x3f0c]: 0x254 (ret) or 0x41e9
        self.mvol_cache = 0            # [0x3f14]
        self.done = [1] * 32           # [0x3f2c + v]: 0 while voice v ramps down for a new note
        self.reg_sel = 0x43            # [0x3f61]: register select restored at the end of the IRQ
        self.positions = []            # after each timer IRQ: (order index, row, countdown)
        self.order, self.row, self.counter = 0, 0, 1   # [0x1aa0], [0x1a9c], [0x1a9b] in the image
        self.notes = 0
        self.stale = 0                 # ramp IRQs served after a later tick has started (latency effect)

    # ------------------------------------------------------------------------------------------ start-up
    def gus_init(self):
        """0x45b4 (after 0x490a has read ULTRASND=240,3,3,7,7: port [0x3f1c] = 0x240, IRQ [0x3f26] = 7)"""
        c = self.c
        c.outb(0x000, 0x0b)                       # mix control: line in off, output off
        c.w8(0x4c, 0)                             # reset (+ delay: 7 reads of port 0x300)
        c.w8(0x4c, 1)
        c.w8(0x41, 0)
        c.w8(0x45, 0)
        c.w8(0x49, 0)
        c.w8(0x0e, 0xdf)                          # 32 voices for now
        for v in range(31, -1, -1):
            for _ in range(2):                    # each voice twice (bl ^= 0x80)
                c.outb(0x102, v)
                c.w8(0x00, 3)
                c.w8(0x0d, 3)
                c.w16(0x09, self.volt[0])         # 0x4e20
                c.w8(0x06, 0x3f)                  # ramp rate: 63 steps per frame, never changed
                c.w8(0x0c, 7)
        c.outb(0x00f, 5)
        c.outb(0x000, 8)
        c.outb(0x00b, 0)
        c.outb(0x00f, 0)
        c.outb(0x000, 0x4b)
        c.outb(0x00b, self.img.u8(A_LATCH + IRQ_NO))   # 0x44: IRQ 7, same for both GF1 IRQs
        c.outb(0x000, 9)
        # DRAM size probe: increment the byte at 0x3ffff, 0x7ffff, 0xbffff, 0xfffff, read it back
        c.w16(0x43, 0xffff)
        c.outb(0x103, 0x44)
        self.dram_banks = 0
        for cl in (3, 7, 11, 15):
            c.outb(0x105, cl)
            b = (c.inb(0x107) + 1) & 0xff
            c.outb(0x107, b)
            if c.inb(0x107) != b:
                break
            self.dram_banks += 1                  # [0x3f27]
        c.w8(0x4c, 7)                             # run, DAC on, GF1 IRQs on
        c.w8(0x47, 0xff)                          # timer 2 count 0xff (320 us)
        c.w8(0x45, 8)                             # timer 2 IRQ on
        c.outb(0x008, 4)
        c.outb(0x009, 2)                          # start timer 2
        # IRQ 7: handler 0x4040 (the word at 0x404b is patched with `mov eax, eax`; IRQ >= 8: `out 0xa0, al`)
        c.outb(0x103, 0x8f)
        c.inb(0x105)                              # acknowledge
        self.tick_fn = None

    def load(self):
        """0x2d15: header, instruments, orders, patterns -> per-channel tracks of 5-byte events, samples -> DRAM"""
        m, c = self.mod, self.c
        tag = m[0x438:0x43c]
        if tag[1:4] == b'CHN':
            n = tag[0] - 0x30
        elif tag == b'M.K.':
            n = 4
        elif tag[2:4] == b'CH':
            n = ((tag[0] - 0x30) * 10 + ((tag[1] - 0x30) & 0xf)) & 0xff
        else:
            n = 4
        self.nch = n                              # [0xc7b] = 28
        for v in range(32):                       # 0x4587 with the table 0xd7d
            c.outb(0x102, v)
            c.w8(0x0c, self.img.u8(A_PAN + v))
        self.speed = 6                            # [0xc76]
        self.bpm = 125                            # [0xc77]
        self.maxp, self.minp = 0xd60, 0x1c0       # [0xb57] 3424 = 856 x 4, [0xb59] 448 = 112 x 4
        self.ins = []
        o = 20
        for i in range(31):
            I = Ins()
            ln, ft, vol, ls, rl = struct.unpack_from('>HBBHH', m, o + 22)
            I.len = ln * 2 if ln * 2 > 2 else 0
            I.ft = ft & 0xf
            I.c2spd = self.c2spd[I.ft]
            I.vol = vol
            I.ls = ls * 2
            rr = ((rl - 1) * 2) & 0xffffffff     # dec ebx; shl ebx, 1
            le = (I.ls + rr) & 0xffffffff
            if le > I.len:
                le = (I.len - 1) & 0xffffffff
            I.le = (le + 2) & 0xffffffff
            if rr < 0x80000000 and rr > 2:        # jle (signed): loops of 4 bytes or less are ignored
                I.flags = 1
                if m[o:o + 4] == b'ping':
                    I.flags |= 2
            I.dram = struct.unpack_from('<I', self.img.b, C32 + A_INS + 0x46 * i + 0x40)[0]
            self.ins.append(I)
            o += 30
        self.songlen = m[0x3b6]                  # [0xc7a] (the restart byte is ignored)
        self.orders = list(m[0x3b8:0x3b8 + 128]) + [0] * 128     # 0xc7d: 128 + 128 zero bytes
        self.npat = max(self.orders[:128]) + 1  # [0xc78]: over all 128 entries
        # per channel: npat x 64 events (note, instrument, volume, effect, parameter); default 0 0 ff 0 0
        self.tracks = [[[0, 0, 0xff, 0, 0] for _ in range(self.npat * 64)] for _ in range(n)]
        o = 0x43c
        for p in range(self.npat):
            for ch in range(n):
                for row in range(64):
                    b = m[o + (row * n + ch) * 4:o + (row * n + ch) * 4 + 4]
                    ev = self.tracks[ch][p * 64 + row]
                    per = ((b[0] << 8) | b[1]) & 0xfff
                    note = 0
                    if per:
                        k = next((j for j in range(0x60) if self.periods[j] == per), 0x60)
                        note = ((k // 12) << 4) + k % 12 + 1      # (unmatched: k = 0x60 -> 0x81)
                    ev[0] = note & 0xff
                    ev[1] = (b[0] & 0xf0) | (b[2] >> 4)
                    if b[2] & 0xf == 0xc:
                        ev[2] = b[3]                  # Cxx becomes the volume byte
                    else:
                        ev[3], ev[4] = b[2] & 0xf, b[3]
            o += n * 256
        # samples: one after the other from DRAM 0, by PIO; byte 0 (and byte 1 of one-shots) zeroed
        a = 0
        for I in self.ins:
            if not I.len:
                continue
            assert I.len <= 0x10000, 'a longer sample makes 0x2d15 exit the program'
            buf = bytearray(m[o:o + I.len])
            o += I.len
            I.dram = a
            buf[0] = 0
            if not I.flags & 1:
                buf[1] = 0
            # 0x4450: per byte reg 0x43 = address & 0xffff (reg 0x44 = address >> 16 when it changes), byte -> 3x7
            c.dram[a:a + I.len] = buf
            c.addr = a + I.len - 1
            a += I.len
        self.dram_end = a
        self.reg_sel = 0x43

    def start(self):
        """0x2c17: reset the 32 channel records and the position, then 0x4369"""
        self.ch = [Chan() for _ in range(32)]
        self.counter = 1                          # [0x1a9b]
        self.row = 0x3f                           # [0x1a9c]
        self.roww = 0                             # [0x1a9d]
        self.brk = 0x3f                           # [0x1aa7]
        self.nextrow = 0                          # [0x1aa8]
        self.patdelay = 0                         # [0x1aa9]
        self.ordidx = 0xff                        # [0x1a9f] (0 - 1)
        self.order = 0                            # [0x1aa0]
        self.pattern = self.orders[0]             # [0x1aa2]
        self.playing = 1                          # [0x1aa1]
        self.loopcnt = 0                          # [0x1aab]
        self.looprow = 0                          # [0x1aaa]
        # 0x4369
        n = max(self.nch, 14)
        self.div = self.vdiv[n - 14]              # [0x3f18] = 22050
        self.voices = n                           # [0x3f2b]
        self.c.w8(0x0e, (n - 1) | 0xc0)           # 0xdb: 28 voices
        self.tick_fn = 'play'                     # [0x3f0c] = 0x41e9

    def stop(self):
        """0x2cff: 0x43d6 (stop every voice), 0x4545 (IRQ vector back, GF1 reset, output off)"""
        c = self.c
        self.tick_fn = None
        for v in range(31, -1, -1):
            c.outb(0x102, v)
            for _ in range(2):
                c.w8(0x00, 3)
                c.w8(0x0d, 3)
                c.w8(0x09, 0)
        c.w8(0x4c, 0)
        c.outb(0x000, 0x0b)
        self.playing = 0

    # ------------------------------------------------------------------------------------------ interrupt
    def irq(self):
        """0x4040"""
        c = self.c
        st = c.inb(0x006)
        if st & 0x40:
            self.ramp_irq()
        elif st & 8:
            c.w8(0x47, self.count)                # the count computed after the previous tick
            c.w8(0x45, 0)
            c.w8(0x45, 8)
            if self.tick_fn:
                self.tick()
                self.tempo()
                self.note_off()
            self.positions.append((self.order, self.row, self.counter))
        c.outb(0x103, self.reg_sel)

    def tempo(self):
        """0x41e9 after the tick: BPM -> timer-2 count. n = 1000000 / (32 BPM) in 80 us units, the GUS gets
        n >> 2 units of 320 us; the remainder -(n & 3) goes to [0x3f29] and is never read"""
        n = 1000000 // (self.bpm * 32)
        self.count = -(n >> 2) & 0xff
        self.rem = -(n & 3) & 0xff

    def note_off(self):
        """0x421a: every channel that (re)starts its sample ramps its voice down first, with the ramp IRQ on"""
        c = self.c
        for v in range(32):
            ch = self.ch[v]
            self.done[v] = 1
            if ch.flags & 8:
                c.outb(0x102, v)
                self.done[v] = 0
                c.outb(0x103, 0x89)
                bx = c.inw(0x104)                 # the voice's current volume
                c.w8(0x07, 0x40)
                c.w8(0x08, bx >> 8)
                c.w8(0x0d, 0x60)                  # down to 0x400, IRQ at the end
        if sum(self.done) == 32:
            self.update()

    def ramp_irq(self):
        """0x40d8: for each voice whose ramp has ended: stop it, program the new sample, start it"""
        c = self.c
        c.outb(0x103, 0x8f)
        al = c.inb(0x105)
        bx = self.ebx & 0xffff
        while True:
            v = al & 0x1f
            c.outb(0x102, v)
            if self.done[v]:
                self.stale += 1
            self.done[v] = 1
            c.w8(0x0d, 3)
            c.w8(0x00, 3)
            ch = self.ch[v]
            for which in ('ls', 'end', 'pos'):
                if which == 'ls':
                    a = ch.lstart
                    if a == ch.c_ls:
                        continue
                    ch.c_ls = a
                    rh, rl = 2, 3
                elif which == 'end':
                    a = ch.end
                    if a == ch.c_end:
                        continue
                    ch.c_end = a
                    rh, rl = 4, 5
                else:
                    a = (ch.start + ch.offset) & 0xffffffff
                    rh, rl = 0x0a, 0x0b
                bx = ((bx >> 7) | (a << 9)) & 0xffff     # shrd bx, si, 7
                c.w16(rh, (a >> 7) & 0xffff)
                c.w8(rl, bx >> 8)                 # address bits 0..6 and one bit of the previous BX
            c.w8(0x00, ch.ctl)
            c.outb(0x103, 0x8f)
            al = c.inb(0x105)
            if al & 0x40:
                break
        if sum(self.done) == 32:
            self.update()

    def update(self):
        """0x4299: frequency, pan and volume of every channel with pending changes"""
        c = self.c
        for v in range(32):
            ch = self.ch[v]
            c.outb(0x102, v)
            cl, ch.flags = ch.flags, 0
            if not cl:
                continue
            if cl & 8:
                cl = 7
            if cl & 4:
                if ch.period:
                    hz = AMIGA4 // ch.period
                    ch.fc = ((hz * 0x400) // self.div) & 0xffff
                if ch.fc != ch.c_fc:
                    ch.c_fc = ch.fc
                    c.w16(0x01, ch.fc)
            if cl & 2 and ch.pan != ch.c_pan:
                ch.c_pan = ch.pan
                c.w8(0x0c, ch.pan)
            if self.master != self.mvol_cache or cl & 1:
                self.mvol_cache = self.master
                c.w16(0x09, self.volt[(ch.vol * self.master) >> 6])

    # ------------------------------------------------------------------------------------------ the tick
    def tick(self):
        """0x22c4"""
        self.counter = (self.counter - 1) & 0xff
        if self.counter:
            return self.tick_effects()
        self.counter = self.speed
        if self.patdelay:
            self.patdelay -= 1
            return self.tick_effects()
        if self.row == self.brk:
            self.row = self.nextrow
            self.roww = self.row
            self.ordidx = (self.ordidx + 1) & 0xff
            if self.ordidx == self.songlen:
                self.ordidx = 0
            self.order = self.ordidx
            self.pattern = self.orders[self.ordidx]
        else:
            self.row = (self.row + 1) & 0xff
        self.nextrow = 0
        self.brk = 0x3f
        for k in range(self.nch):
            self.roww = self.row
            self.row_channel(self.ch[k], self.tracks[k][self.pattern * 64 + self.row])

    def oct1(self, n):
        """word [0xb8b + 2 n] = the period table 0xb5b from index 24 on (n may be -1: index 23)"""
        n = n - (1 << 32) if n >= 1 << 31 else n
        assert 0 <= 24 + n < 96, 'period index %d outside the table' % n
        return self.periods[24 + n]

    def note_period(self, note, c2spd):
        """0x23e8 / 0x2b15: (period of the note in the octave at 0xb8b x 0x20ab0 >> octave) / C-2 rate"""
        p = self.oct1((note & 0xf) - 1) * 0x20ab0 & 0xffffffff
        return ((p >> (note >> 4)) // c2spd) & 0xffff

    def row_channel(self, C, ev):
        note, ins, vol, eff, par = ev
        C.offset = 0
        C.eff, C.par = eff, par
        if note:
            noins = 0
            bl = ins
            if bl == 0:
                bl = C.ins
                if bl == 0:
                    return self.row_noteless(C, ev)
                noins = 1
            C.ins = bl
            I = self.ins[(bl - 1) & 0xff]
            C.note = note
            C.c2spd = I.c2spd
            C.base = self.note_period(note, I.c2spd)
            if eff in (3, 5):                     # tone portamento: the note is only the target
                return self.row_noteless(C, ev)
            self.notes += 1
            C.period = C.base
            C.flags = 0xf
            C.ctl = 0
            C.vibpos = 0
            C.offset = 0
            C.start = I.dram
            C.end = (I.dram + I.len) & 0xffffffff
            if I.flags & 1:
                C.lstart = (I.ls + I.dram) & 0xffffffff
                C.end = (I.le + I.dram) & 0xffffffff
                C.ctl |= 8
                if I.flags & 2:
                    C.ctl |= 0x10
            if noins != 1:
                C.vol = min(I.vol, 0x40)
                C.flags |= 1
            if eff == 0xe and par >> 4 == 0xd:
                C.flags = 0
            return self.row_volume(C, ev)
        return self.row_noteless(C, ev)

    def row_noteless(self, C, ev):
        """0x24a4: an instrument number without a note (or with tone porta) resets the volume; the stored
        instrument number becomes 0-based here"""
        if ev[1]:
            b = (ev[1] - 1) & 0xff
            C.ins = b
            C.vol = min(self.ins[b].vol, 0x40)
            C.flags |= 1
        return self.row_volume(C, ev)

    def row_volume(self, C, ev):
        """0x24cc: the volume byte (Cxx), then the row effect"""
        if ev[2] != 0xff:
            C.flags |= 1
            C.vol = min(ev[2], 0x40)
        C.tickcnt = 0
        eff, par = C.eff, C.par
        if eff == 0:                              # 0x2ba3 arpeggio: base, +x, +x
            if par:
                C.arp[0] = C.period
                cl = C.note >> 4
                n = (((C.note & 0xf) - 1) + (par >> 4)) & 0xffffffff
                if n > 11:
                    cl += 1
                    n -= 11                       # (11, not 12)
                    if n > 11:
                        cl += 1
                        n -= 11
                p = ((self.oct1(n) << 4) >> cl) & 0xffff
                C.arp[1] = C.arp[2] = p
        elif eff == 3:                            # 0x28e8
            if not par:
                par = C.pspeed
            C.pspeed = C.par = par
        elif eff == 4:                            # 0x2854
            d, s = par & 0xf, par & 0xf0
            if not d:
                d = C.vdepth
            if not s:
                s = C.vspeed
            C.vdepth, C.vspeed = d, s
            C.par = d | s
        elif eff == 8:                            # 0x2b06
            C.pan = par >> 4
            C.flags |= 2
        elif eff == 9:                            # 0x270a
            off = par << 8
            if off:
                lim = (C.end - C.start) & 0xffffffff
                if off >= lim:
                    off = (lim - 1) & 0xffffffff
                C.offset = off
            C.flags |= 8                          # (re)starts the sample, with or without a note
        elif eff == 0xb:                          # 0x2a57
            self.brk = self.roww & 0xff
            self.ordidx = (par - 1) & 0xff
        elif eff == 0xd:                          # 0x26d7
            if par >= 0x64:
                par = 0x63
            self.nextrow = ((par & 0xf) + (par >> 4) * 2 + (par >> 4) * 8) & 0xff
            self.brk = self.roww & 0xff
        elif eff == 0xf:                          # 0x2689
            if par == 0:
                self.playing = 0                  # [0x1aa1]: nothing reads it
            elif par > 0x1f:
                self.bpm = par
            else:
                self.speed = self.counter = par
        elif eff == 0xe:
            x, y = par >> 4, par & 0xf
            if x == 1:                            # 0x29db
                p = (C.period - y * 4) & 0xffff
                C.period = self.minp if p < self.minp else p
                C.flags |= 4
            elif x == 2:                          # 0x2a05
                p = (C.period + y * 4) & 0xffff
                C.period = self.maxp if p > self.maxp else p
                C.flags |= 4
            elif x == 5:                          # 0x2b15
                C.base = C.period = self.note_period(C.note, self.c2spd[y])
            elif x == 6:                          # 0x2b51
                if y == 0:
                    self.looprow = self.roww & 0xff
                else:
                    if self.loopcnt == 0:
                        self.loopcnt = y
                    else:
                        self.loopcnt -= 1
                        if self.loopcnt == 0:
                            return
                    self.brk = self.roww & 0xff
                    self.nextrow = self.looprow
                    self.ordidx = (self.ordidx - 1) & 0xff
            elif x == 8:                          # 0x2b06 with AH = y: pan = y >> 4 = 0
                C.pan = y >> 4
                C.flags |= 2
            elif x == 0xa:                        # 0x2a2f
                C.vol = min((C.vol + y) & 0xff, 0x40)
                C.flags |= 1
            elif x == 0xb:                        # 0x2a45
                C.vol = C.vol - y if C.vol >= y else 0
                C.flags |= 1
            elif x == 0xd:                        # 0x2a96: the note starts now anyway
                C.flags |= 0xf
            elif x == 0xe:                        # 0x2a9f
                self.patdelay = y

    def tick_effects(self):
        """0x251f: every channel on the ticks between rows"""
        for C in self.ch[:self.nch]:
            C.tickcnt = (C.tickcnt + 1) & 0xff
            C.arpcnt += 1
            if C.arpcnt >= 3:
                C.arpcnt = 0
            eff, par = C.eff, C.par
            if eff == 0:                          # 0x2bf8
                if par:
                    C.period = C.arp[C.arpcnt]
                    C.flags = 4                   # (mov: replaces the flags)
            elif eff in (1, 2):                   # 0x272d / 0x2793
                if C.period:
                    if eff == 1:
                        p = (C.period - (par << 2)) & 0xffff
                        C.period = self.minp if p - 0x10000 * (p >> 15) < self.minp else p
                    else:
                        p = (C.period + (par << 2)) & 0xffff
                        C.period = self.maxp if p > self.maxp else p
                    C.flags |= 4
            elif eff in (3, 5):                   # 0x28a5 / 0x28fb
                if eff == 5:
                    self.volslide(C, ((par & 0xf) - (par >> 4)) & 0xff)
                    ax = (C.pspeed << 2) & 0xffff
                else:
                    ax = (par << 2) & 0xffff
                dx, bx = C.base, C.period
                if bx < dx:
                    bx = (bx + ax) & 0xffff
                    if bx > dx:
                        C.period = dx
                if bx > dx:
                    borrow = bx < ax
                    bx = (bx - ax) & 0xffff
                    if borrow:
                        bx = dx if eff == 3 else 0
                    if bx < dx:
                        bx = dx
                C.period = bx
                C.flags |= 4 if eff == 3 else 5
            elif eff in (4, 6):                   # 0x27f9 / 0x295b
                if eff == 6:
                    self.volslide(C, ((par & 0xf) - (par >> 4)) & 0xff)
                    p = C.vdepth | C.vspeed
                else:
                    p = par
                C.vibpos = (C.vibpos + ((p & 0xf0) >> 2)) & 0xff
                if eff == 4:
                    m = self.vibtab[C.vibpos >> 2]          # 64 entries: no `and 0x1f` here
                else:
                    m = self.sine[(C.vibpos >> 2) & 0x1f]
                ax = (m * (p & 0xf)) >> 5
                if not C.vibpos & 0x80:
                    ax = -ax
                ax = (ax + C.base) & 0xffff
                if eff == 4:
                    if ax - 0x10000 * (ax >> 15) < self.minp:
                        ax = self.minp
                elif ax < self.minp:
                    ax = self.minp
                if ax > self.maxp:
                    ax = self.maxp
                C.period = ax
                C.flags |= 4 if eff == 4 else 5
            elif eff == 0xa:                      # 0x287a: up x, else down y
                x, y = par >> 4, par & 0xf
                self.volslide(C, (-x & 0xff) if x else y)
                C.flags |= 1
            elif eff == 0xe:
                x, y = par >> 4, par & 0xf
                if x == 9 and y == C.tickcnt:     # 0x2aaa retrigger
                    C.flags |= 0xd
                    C.tickcnt = 0
                    I = self.ins[(C.ins - 1) & 0xff]
                    C.start = (I.dram + C.offset) & 0xffffffff
                    C.end = (C.start + I.len) & 0xffffffff
                    if I.flags & 1:
                        C.lstart = (I.ls + I.dram) & 0xffffffff
                        C.end = (I.le + I.dram) & 0xffffffff
                        C.ctl |= 8
                    C.vol = min(I.vol, 0x40)
                elif x == 0xc and y == C.tickcnt:  # 0x2a6e note cut
                    C.vol = 0
                    C.flags |= 1
                elif x == 0xd and y == C.tickcnt:  # 0x2a84 note delay
                    C.flags |= 0xf

    @staticmethod
    def volslide(C, ah):
        """vol - ah (8 bit): a negative result -> 0, >= 64 -> 64"""
        al = (C.vol - ah) & 0xff
        if al & 0x80:
            al = 0
        C.vol = min(al, 0x40)


# ------------------------------------------------------------------------------------------------ the capture
def read_irqs(path):
    """the capture's interrupts: (tick, kind, service time, voices in the order the ramp IRQ served them)"""
    out = []
    for ln in open(path):
        f = ln.split()
        if len(f) == 4 and f[1] == 'irq':
            out.append((int(f[0]), f[2], float(f[3]), []))
        elif out and out[-1][1] == 'r' and len(f) == 4 and f[2] == 'r0d' and f[3] == '3':
            out[-1][3].append(int(f[1][1:]))
    return out


def run(img, mod, card, irqs=None, ticks=None, eax=0x808, ebx=0, start_after=2, hw=None, use_order=True):
    """Run the player as the capture did: init at t = 0, load, `start_after` timer IRQs, start, then the IRQs
    (the capture's delivery times if `irqs` is given, else at their hardware times), then the stop.
    Returns the player and a list of (kind, event time, delivery time)."""
    pl = Player(img, mod, card, eax=eax, ebx=ebx)
    card.hw = hw
    pl.gus_init()                                 # at t = 0: timer 2 starts with count 0xff
    pl.load()                                     # (instant here; with interrupts off on the original)
    sched = []                                    # (kind, event time, delivery time, player started)
    ntimer = 0
    i = 0
    while True:
        te, k = card.next_event()
        if irqs is not None:
            if i >= len(irqs):
                break
            _, kind, tlog, order = irqs[i]
            if kind != 'r' or not use_order:
                order = None
            # the emulator raises the IRQ at the event (ramp ends: + 1e-7 s) and serves it when the interrupted
            # code allows it; the log has the service time to 1 us
            if tlog - te > 1.5e-6:
                td = tlog                         # served late
            else:
                td = te + (1e-7 if k == 'r' else 0.0)
        else:
            if ntimer >= ticks and k == 't':
                break
            td, kind, order = te + (1e-7 if k == 'r' else 0.0), k, None
        card.events(td, order)
        assert card.irq(), 'no interrupt pending at %.6f' % td
        got = 'r' if card.status & 0x40 else 't'
        assert got == kind, 'interrupt kind %s, capture %s at %.6f' % (got, kind, td)
        if kind == 't':
            ntimer += 1
            card.tick = ntimer
        sched.append((kind, te, td, bool(pl.tick_fn)))
        card.lines.append('%d irq %s %.6f' % (card.tick, kind, tlog if irqs is not None else td))
        if hw is not None:
            hw.at(td)
            if kind == 't':
                hw.marks.append((hw.frame, hw.frame_rate()))
        pl.irq()
        if kind == 't' and ntimer == start_after:
            pl.start()                            # (main, between two timer interrupts)
            pl.positions[-1] = (pl.order, pl.row, pl.counter)
            card.lost = 0
        i += 1
    if hw is not None:
        hw.at(card.t2_next)                       # one more tick period of sound
    pl.stop()
    return pl, sched


def check(a, img, mod):
    irqs = read_irqs(os.path.join(a.check, 'full.gus'))
    card = Card()
    pl, sched = run(img, mod, card, irqs=None if a.ideal else irqs, ticks=sum(1 for x in irqs if x[1] == 't'),
                    eax=a.eax, ebx=a.ebx, use_order=not a.time_order)
    ok = True
    ref = open(os.path.join(a.check, 'full.dram'), 'rb').read()
    nd = sum(1 for x, y in zip(ref, card.dram) if x != y)
    print('DRAM: %d of %d bytes differ (samples 0..0x%x, probe bytes 0x7ffff, 0xbffff, 0xfffff = 1)' % (
        nd, len(ref), pl.dram_end))
    ok &= nd == 0
    ref = [ln for ln in open(os.path.join(a.check, 'full.gus')).read().split('\n') if ln and ln[0] != '#']
    mine = card.lines
    if a.ideal:                               # the delivery times differ: compare without the irq lines' times
        ref = [' '.join(x.split()[:3]) if ' irq ' in x else x for x in ref]
        mine = [' '.join(x.split()[:3]) if ' irq ' in x else x for x in mine]
    nd = next((i for i, (x, y) in enumerate(zip(ref, mine)) if x != y), None)
    if nd is None and len(ref) == len(mine):
        print('GUS log: %d lines, identical%s' % (len(ref), ' (irq times not compared)' if a.ideal else ''))
    else:
        ok = False
        i = nd if nd is not None else min(len(ref), len(mine))
        print('GUS log: %d lines (capture %d); first difference at line %d:' % (len(mine), len(ref), i + 1))
        for j in range(max(0, i - 8), min(i + 8, max(len(ref), len(mine)))):
            print('  %-32s %s' % (ref[j] if j < len(ref) else '-', mine[j] if j < len(mine) else '-'))
    if not a.ideal:
        run_ = [x[:3] for x in sched if x[3]]
        late = [(k, te, td) for k, te, td in run_ if td - te > 1.5e-6]
        early = [(k, te, td) for k, te, td in run_ if te - td > 1.5e-6]
        print('interrupts after the start: %d timer, %d ramp; served after their event by more than 1.5 us: %d '
              '(max %.3f ms); before it: %d' % (
                  sum(1 for x in run_ if x[0] == 't'), sum(1 for x in run_ if x[0] == 'r'), len(late),
                  1e3 * max((td - te for _, te, td in late), default=0), len(early)))
        ok &= not early
    print('timer overflows lost (previous one still pending): %d; ramp IRQs served for a voice already done: %d; '
          'ramp IRQs where the capture queued the voices in another order than their ramp ends: %d'
          % (card.lost, pl.stale, card.reordered))
    bad = n = 0
    for ln in open(os.path.join(a.check, 'full.ticks')):
        if ln.startswith('#'):
            continue
        f = ln.split()
        o, r, cnt, g = int(f[2]), int(f[3]), int(f[4]), int(f[5])
        if 0 < g <= len(pl.positions):
            n += 1
            if pl.positions[g - 1] != (o, r, cnt):
                bad += 1
    print('music position (order, row, countdown): %d frames compared, %d differ' % (n, bad))
    ok &= bad == 0
    print('ALL MATCH' if ok else 'MISMATCH')
    if a.out:
        with open(a.out + '.gus', 'w') as f:
            f.write('\n'.join(card.lines) + '\n')
        open(a.out + '.dram', 'wb').write(card.dram)
    return pl, sched


# ------------------------------------------------------------------------------------------------ GF1 model
class GF1:
    """What the card does with the writes: jizz/remake/src/gf1.cpp's model (paper/re/notes-music.md section 7)
    with forward and bidirectional loops. Positions in 1/512 byte, increment FC >> 1 per frame, 12-bit log
    volume in 1/512 steps with ramps (63 steps per frame here), pan 0..15 with a linear law, 617400 / active
    frames per second, 8-bit samples linearly interpolated, stopped voices mixed at their parked position.
    8-bit writes to the address registers 3 / 5 / 0x0b combine with the low byte of the last 16-bit write.
    Register writes apply at the interrupt's time (the frames that start before it are rendered first)."""

    def __init__(self, dram):
        import numpy as np
        self.np = np
        self.dram = dram              # the card's bytearray (filled by the load before the first frame)
        self.dram_f = None
        e = np.arange(4096) >> 8
        m = np.arange(4096) & 255
        self.gain = (256 + m) * np.exp2(e - 24.0)
        self.latch = 0
        self.out = []
        self.marks = []               # (frame index, frame rate) at each timer interrupt
        self.frame = 0                # frames rendered
        self.seg = None               # (time, frame) where the current frame rate started
        self.reset()

    def reset(self):
        self.active = 14
        self.dac = False
        self.v = [{'ctl': 3, 'fc': 0, 'start': 0, 'end': 0, 'pos': 0, 'vctl': 3, 'rate': 0, 'lo': 0, 'hi': 0,
                   'vol': 0, 'pan': 7} for _ in range(32)]

    def port(self, p, val):
        pass

    def write(self, voice, reg, val, size):
        if size == 1:
            d = ((val >> 8) & 0xff) << 8 | self.latch if reg in V16 else (val & 0xff) << 8
        else:
            self.latch = val & 0xff
            d = val & 0xffff
        s = self.v[voice]
        if reg == 0x4c:
            if not d & 0x100:
                self.reset()
            self.dac = bool(d & 0x200)
        elif reg == 0x0e:
            self.active = max(14, ((d >> 8) & 31) + 1)
        elif reg == 0x00:
            s['ctl'] = (d >> 8) & 0x7f
        elif reg == 0x01:
            s['fc'] = d
        elif reg in (0x02, 0x04, 0x0a):
            k = {0x02: 'start', 0x04: 'end', 0x0a: 'pos'}[reg]
            s[k] = (s[k] & 0xffff) | ((d & 0x1fff) << 16)
        elif reg in (0x03, 0x05, 0x0b):
            k = {0x03: 'start', 0x05: 'end', 0x0b: 'pos'}[reg]
            s[k] = (s[k] & ~0xffff) | d
        elif reg == 0x06:
            s['rate'] = d >> 8
        elif reg == 0x07:
            s['lo'] = ((d >> 8) << 4) << 9
        elif reg == 0x08:
            s['hi'] = ((d >> 8) << 4) << 9
        elif reg == 0x09:
            s['vol'] = (d >> 4) << 9
        elif reg == 0x0c:
            s['pan'] = (d >> 8) & 15
        elif reg == 0x0d:
            s['vctl'] = (d >> 8) & 0x7f

    def frame_rate(self):
        return GF1_CLOCK / self.active

    @staticmethod
    def ramp_inc(s):
        return ((s['rate'] & 63) * 512) >> (3 * (s['rate'] >> 6))

    def at(self, t):
        """render the frames that start before time t (seconds, the card's clock)"""
        if self.seg is None:
            self.seg = (t, 0)
            self.rate_seen = self.frame_rate()
            return
        if self.frame_rate() != self.rate_seen:     # reg 0x0e changed: a new time base from here
            self.seg = (t, self.frame)
            self.rate_seen = self.frame_rate()
            return
        f = self.seg[1] + int(-(-(t - self.seg[0]) * self.rate_seen // 1))
        if f > self.frame:
            self.render(f - self.frame)

    def render(self, n):
        np = self.np
        if self.dram_f is None:
            self.dram_f = np.frombuffer(bytes(self.dram) + b'\0', dtype=np.int8).astype(np.float64) / 128.0
        L = np.zeros(n)
        R = np.zeros(n)
        for s in self.v[:self.active]:
            pos, vol = self.voice_frames(s, n)
            a = (pos >> 9) & 0xfffff
            f = (pos & 511).astype(np.float64) / 512.0
            smp = self.dram_f[a] + (self.dram_f[a + 1] - self.dram_f[a]) * f
            g = smp * self.gain[vol >> 9]
            L += g * ((15 - s['pan']) / 15.0)
            R += g * (s['pan'] / 15.0)
        out = np.stack([L, R], 1)
        self.out.append(out if self.dac else out * 0)
        self.frame += n

    def voice_frames(self, s, n):
        np = self.np
        ctl, p0, inc = s['ctl'], s['pos'], s['fc'] >> 1
        st, en = s['start'], s['end']
        ln = en - st
        if ctl & 3 or inc == 0:
            pos = np.full(n, p0, dtype=np.int64)
        elif ctl & 0x18 == 0x18 and ln > 0:       # bidirectional: unfold with period 2 ln (as gf1.cpp)
            per = 2 * ln
            back = bool(ctl & 0x40)
            u0 = st + per - (p0 - st) if back and st <= p0 <= en else p0
            u = u0 + np.arange(n + 1, dtype=np.int64) * inc
            m = (u - st) % per
            fold = np.where(m > ln, st + per - m, st + m)
            pp = np.where(u <= en, u, fold)
            pos = pp[:n]
            last = u[n]
            if last <= en:
                s['pos'] = int(last)
                s['ctl'] = ctl & ~0x40
            else:
                mm = int((last - st) % per)
                s['pos'] = st + per - mm if mm > ln else st + mm
                s['ctl'] = (ctl & ~0x40) | (0x40 if mm > ln else 0)
        else:
            assert not ctl & 0x40, 'backward voices are not used by this player'
            pp = p0 + np.arange(n + 1, dtype=np.int64) * inc
            over = pp > en
            if over.any():
                if ctl & 8 and ln > 0:
                    pp[over] = st + (pp[over] - en) % ln
                else:
                    pp[over] = en
            pos = pp[:n]
            p = p0 + n * inc
            if p > en:
                if ctl & 8 and ln > 0:
                    p = st + (p - en) % ln
                else:
                    p = en
                    s['ctl'] = ctl | 1
            s['pos'] = int(p)
        v0 = s['vol']
        vinc = self.ramp_inc(s)
        if not s['vctl'] & 3 and vinc:
            if s['vctl'] & 0x40:
                vol = np.maximum(v0 - np.arange(n, dtype=np.int64) * vinc, s['lo'])
                v = v0 - n * vinc
                if v <= s['lo']:
                    v = s['lo']
                    s['vctl'] |= 1
            else:
                vol = np.minimum(v0 + np.arange(n, dtype=np.int64) * vinc, s['hi'])
                v = v0 + n * vinc
                if v >= s['hi']:
                    v = s['hi']
                    s['vctl'] |= 1
            s['vol'] = int(v)
        else:
            vol = np.full(n, v0, dtype=np.int64)
        return pos, np.clip(vol, 0, (4096 << 9) - 1)


def render(a, img, mod):
    """GF1 render with the interrupts at their hardware events (timer overflows, ramp ends of the card model),
    from the first row tick to one tick period after the last of --ticks timer interrupts"""
    import numpy as np
    import wave
    card = Card()
    gf = GF1(card.dram)
    pl, sched = run(img, mod, card, irqs=None, ticks=a.ticks, eax=a.eax, ebx=a.ebx, hw=gf)
    pcm = np.concatenate(gf.out) if gf.out else np.zeros((0, 2))
    f0, rate = gf.marks[2]                        # from the first row tick (timer interrupt 3): 28 voices
    pcm = pcm[f0:]
    peak = float(np.abs(pcm).max()) if len(pcm) else 0.0
    print('rendered %d frames at %.2f Hz (%.2f s from the first row tick); peak %.3f (1.0 = one voice at full '
          'scale and gain 1, hard panned)' % (len(pcm), rate, len(pcm) / rate, peak))
    with wave.open(a.wav, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(int(round(rate)))
        w.writeframes((np.clip(pcm / max(peak, 1e-9) * 0.9, -1, 1) * 32767).astype('<i2').tobytes())


def info(img, mod):
    import collections
    card = Card()
    pl = Player(img, mod, card)
    pl.gus_init()
    pl.load()
    print('%d channels, %d patterns, song length %d (restart byte %d ignored)' % (
        pl.nch, pl.npat, pl.songlen, mod[0x3b7]))
    print('orders: ' + ' '.join('%d' % x for x in pl.orders[:pl.songlen]))
    print(' ins  name                    DRAM start  length  loop start  loop end  flags  ft  vol')
    for i, I in enumerate(pl.ins):
        name = mod[20 + 30 * i:42 + 30 * i].split(b'\0')[0].decode('latin-1')
        print('%4d  %-22s  %8x  %6d  %8x  %8x  %5d  %2d  %3d' % (
            i + 1, name, I.dram, I.len, I.dram + I.ls if I.flags else 0, I.dram + I.le if I.flags else 0,
            I.flags, I.ft, I.vol))
    used = collections.Counter()
    for o in range(pl.songlen):
        p = pl.orders[o]
        for ch in range(pl.nch):
            for r in range(64):
                note, ins, vol, eff, par = pl.tracks[ch][p * 64 + r]
                if vol != 0xff:
                    used['C (volume byte)'] += 1
                if eff == 0xe:
                    used['E%X' % (par >> 4)] += 1
                elif eff or par:
                    used['%X' % eff] += 1
                if note and not ins:
                    used['note without instrument'] += 1
                if ins and not note:
                    used['instrument without note'] += 1
                if note == 0x81:
                    used['unmatched period'] += 1
    print('cells in the song (by order): ' + ', '.join('%s x%d' % kv for kv in sorted(used.items())))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--loader', default=os.path.join(RE, 'unpacked', 'loader.bin'))
    ap.add_argument('--module', default=os.path.join(RE, '..', 'original', 'DOPE.MOD'))
    ap.add_argument('--check', default=RE, help='folder with full.gus / full.dram / full.ticks')
    ap.add_argument('--ideal', action='store_true', help='serve the interrupts at their events (no latency)')
    ap.add_argument('--out', help='write PREFIX.gus and PREFIX.dram')
    ap.add_argument('--time-order', action='store_true', help='with the capture\'s service times, queue the ramp '
                    'ends in time order instead of the order the capture served them')
    ap.add_argument('--eax', type=lambda s: int(s, 0), default=0x808,
                    help='EAX at the tick call (bytes 0x22c0..3, read by 4xx): 0x808 in the capture')
    ap.add_argument('--ebx', type=lambda s: int(s, 0), default=0,
                    help='EBX at the ramp IRQ (bit 15 -> reg 3/5/0xb bit 8): 0 in the capture')
    ap.add_argument('--ticks', type=int, default=22205, help='timer interrupts before the stop (capture: 22205)')
    ap.add_argument('--times', help='write per timer tick: time (s) after the first row tick, order, row')
    ap.add_argument('--wav', help='render through the GF1 model (needs numpy)')
    ap.add_argument('--info', action='store_true')
    a = ap.parse_args()
    img = Img(a.loader)
    mod = open(a.module, 'rb').read()
    if a.info:
        return info(img, mod)
    if a.wav:
        return render(a, img, mod)
    pl, sched = check(a, img, mod)
    if a.times:
        tt = [te for k, te, td, _ in sched if k == 't']
        with open(a.times, 'w') as f:
            f.write('# timer tick (log numbering), hardware time (s) after the first row tick (tick 3), order '
                    'index, row, countdown after the tick\n')
            for k, (t, pos) in enumerate(zip(tt, pl.positions)):
                f.write('%d %.6f %02x %02x %d\n' % (k + 1, t - tt[2], pos[0], pos[1], pos[2]))


if __name__ == '__main__':
    main()

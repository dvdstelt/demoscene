"""Reference implementation of the music of The Sea Robot of Love (KORSO2.EXE): the ProTracker player with its GUS
driver in code32 0x10414..0x14bd3, ported routine by routine (see ../../notes-music.md), plus a GF1 model to render it.

From the program image alone (unpacked/korso2.bin: the module at code32 0xae0 and the player's tables) it produces
  - the GUS DRAM image (GUS init 0x14935: DRAM probe; MOD load 0x13f28: PIO upload 0x14788)          -> full.dram
  - the GUS register / port write log in run_okean.py's text format, from the GUS init to the stop    -> full.gus
  - the music position (order index, row, tick countdown) after every timer interrupt                  -> full.ticks
and with --wav renders the song through a GF1 model (16 active voices: 38,587.5 frames per second) with the
interrupts placed where the GUS raises them (timer 2 overflows every 19.84 ms, volume-ramp ends).

usage:  python3 okeanplay.py                      # check against ../../full.gus / full.dram / full.ticks
        python3 okeanplay.py --out PREFIX         # also write PREFIX.gus and PREFIX.dram
        python3 okeanplay.py --ideal --out PREFIX # interrupts at their hardware times (no CPU latency, no lost ticks)
        python3 okeanplay.py --times FILE         # per timer interrupt: time, order, row, countdown
        python3 okeanplay.py --info               # module, instruments, DRAM layout, effects used
        python3 okeanplay.py --wav okean_music.wav  # GF1 render from the first player tick (needs numpy)
        (run inside the toolbox from this folder)

Routine map (code32 offsets = korso2.bin offset - 0xec0):
  0x142d7 detect (0x14b4b ULTRASND=)   0x142f8 driver table 0x14310 -> 0x12b44   0x14935 GUS init
  0x13f28 MOD load (0x1490c pan table, 0x14788 PIO upload)   0x13eb1 start (0x14714)   0x13f18 stop (0x1475b)
  0x148ca GUS shutdown
  0x1443c GUS IRQ: EOI, sti; status 2x6: bit 6 -> ramp path 0x144b2 (switch one voice to its new sample, set the
          flag [0x144b1]); else bit 3 -> acknowledge timer 2, call [0x14385] = 0x14591: tick 0x1343c, tempo -> timer
          count (reg 0x47), then per voice 0..15: a voice that (re)starts its sample is ramped down with the ramp
          IRQ on and the handler WAITS (interrupts enabled) for the ramp path to set [0x144b1]; then 0x14663 writes
          its frequency, pan and volume.
  0x1343c tick: row (effect tables 0x1374c, Exy 0x13794) or tick effects (0x137d4, Exy 0x1381c)
"""
import argparse
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))

C32 = 0xec0                   # file offset of code32 in korso2.bin
BASE = 0x240                  # ULTRASND=240,3,3,7,7 (the capture's environment)
IRQ_NO = 7
GF1_CLOCK = 1e6 / 1.619695497  # 617,400 voice frames per second / active voices (as run_okean.py)

# code32 addresses of the module and of the player's static data
A_MOD = 0x0ae0                # the module, a ProTracker MOD file (8CHN)
A_OCT = 0x10390               # 12 u16: periods of the base octave, 1712 .. 907 (note -> period)
A_C2SPD = 0x103a8             # 16 u16: C-2 rates per finetune 0..7, -8..-1 (8363, 8413, ..)
A_PER36 = 0x103cc             # 36 u16: ProTracker periods 856 .. 113 (period -> note, at load)
A_AMIGA = 0x10414             # u32 0xd87b50 = 4 x 3,546,836 Hz (also stored by the load at 0x13f91)
A_PAN = 0x10575               # 32 u8: GF1 pan per voice, written at load (3 7 7 7 7 8 6 c 1 e e 1 1 e e ...)
A_INS = 0x10c50               # 31 instrument records of 0x50 bytes
A_CHAN = 0x12b98              # 16 channel records of 0x40 bytes
A_ARPCLAMP = 0x12b82          # u8 1: arpeggio notes above octave 5 become B-5
A_MASTER = 0x12b8c            # u32 63: master volume (the last part lowers it, see notes)
A_BRIGHT = 0x3ceac            # u32 32: the last part's brightness, counted down with the master volume
A_VIB_BUG = 0x2f98            # 4xx / 6xy read [bx + 0x2f98] with 16-bit addressing: 32 bytes of the module
A_SINE = 0x12f98              # 32 u8: the vibrato half sine the code meant to read
A_COUNT0 = 0x1438a            # u8 0xc2: timer-2 count written by the init (125 BPM)
A_LATCH = 0x143a6             # 16 u8: IRQ latch per IRQ number (IRQ 7 -> 0x44)
A_VOLT = 0x143ba              # 64 u16: GF1 volume (register 9) for vol * master >> 6 = 0..62
V16 = {1, 2, 3, 4, 5, 9, 0x0a, 0x0b}

CAPTURE_TICKS = 9203          # timer interrupts the capture serves before main stops the music
START_AFTER = 3               # the first part calls the start 0x13eb1 after the third timer interrupt


class Img:
    def __init__(self, path):
        self.b = open(path, 'rb').read()

    def u8(self, a): return self.b[C32 + a]
    def u16(self, a): return struct.unpack_from('<H', self.b, C32 + a)[0]
    def u32(self, a): return struct.unpack_from('<I', self.b, C32 + a)[0]
    def bytes(self, a, n): return self.b[C32 + a:C32 + a + n]


# ------------------------------------------------------------------------------------------------ the fake card
class Voice:
    def __init__(self):
        self.r = {0: 3, 0x0d: 3, 1: 0, 2: 0, 3: 0, 4: 0, 5: 0, 6: 0, 7: 0, 8: 0, 9: 0, 0x0a: 0, 0x0b: 0, 0x0c: 7}
        self.t = 0.0                  # time of vol
        self.vol = 0.0                # 12-bit volume (float, as run_okean.py simulates it)


class Card:
    """The GUS as run_okean.py's class Gus models and logs it (only what this player uses: no wave IRQs). Ports are
    offsets from the base 0x240. Log lines: '<tick> v<voice> r<reg> <val>' for every register write except the
    DRAM address 0x43 / 0x44 (an 8-bit write to a 16-bit register logs `latch | byte << 8`; the latch is set only by
    8-bit writes to 3x4, which this player never makes: 0), '<tick> p<port> <val>' for port writes other than
    3x2..3x5, 3x7 and 2x8, '<tick> irq <kind> <time>' before each handler run. Volume ramps run in continuous time
    at rate x 617400 / voices steps per second. Register reads: 0x8f the IRQ queue; 0x80..0x8f the voice registers;
    any other select (this player reads select 0x09, not 0x89) answers `other_read` (run_okean.py: 0)."""

    def __init__(self, other_read=0):
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
        self.t2_raised = None         # the overflow whose IRQ is (or was last) pending
        self.hw = None                # optional GF1 model: hw.write(voice, reg, value, size)
        self.lost = 0                 # timer overflows that found the previous one still pending
        self.other_read = other_read

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

    def events(self, t):
        """run the timer and the ramps up to t, in time order"""
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
                    else:
                        self.t2_raised = self.t2_next
                    self.status |= 8
                self.t2_next += (256 - self.g[0x47]) * 320e-6
            else:
                self.sim(vn, min(te + 1e-9, t))
                assert self.v[vn].r[0x0d] & 1
        for vn in range(32):
            self.sim(vn, t)
        self.now = t
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
        return self.other_read      # select 0x09 (a write register): the emulator answers 0

    # helpers in the player's own access pattern
    def w8(self, reg, val):                    # reg -> 3x3, byte -> 3x5
        self.outb(0x103, reg)
        self.outb(0x105, val)

    def w16(self, reg, val):                   # reg -> 3x3, word -> 3x4
        self.outb(0x103, reg)
        self.outw(0x104, val)


# ------------------------------------------------------------------------------------------------ the player
class Ins:
    """instrument record, 0x50 bytes at 0x10c50 + 0x50 (i - 1)"""

    def __init__(self):
        self.flags = 0      # +0x31: 1 loop
        self.len = 0        # +0x32 bytes (0 if <= 2)
        self.ls = 0         # +0x36 loop start
        self.le = 0         # +0x3a loop end = min(ls + 2 replen - 2, len - 1) + 2
        self.vol = 0        # +0x3e (not clamped here)
        self.ft = 0         # +0x3f finetune 0..15
        self.dram = 0       # +0x40 DRAM start (left 0 for empty samples)
        self.c2spd = 0      # +0x44 u16 = word [0x103a8 + 2 ft]


class Chan:
    """channel record, 0x40 bytes at 0x12b98 + 0x40 ch (channel ch plays on GF1 voice ch)"""

    def __init__(self):
        self.start = 0      # +0x04 DRAM start of the sample
        self.lstart = 0     # +0x08 loop start (DRAM)
        self.end = 0        # +0x0c end (DRAM): loop end, or start + length - 1
        self.pan = 0        # +0x10 0..15 (7 at every note; E8x)
        self.flags = 0      # +0x11 1 volume, 2 pan, 4 frequency, 8 (re)start the sample
        self.ctl = 0        # +0x12 GF1 voice control: 8 loop
        self.vol = 0        # +0x13 0..63
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
        self.vibpos = 0     # +0x2c (word written 0 at a note)
        self.vdepth = 0     # +0x2e vibrato depth (low nibble)
        self.vspeed = 0     # +0x2f vibrato speed (high nibble)
        self.base = 0       # +0x30 u16 the note's period (tone porta target, vibrato centre)
        self.c2spd = 0      # +0x32
        self.arp = [0, 0, 0]  # +0x34, +0x36, +0x38
        self.arpcnt = 0     # +0x3a 0..2, advanced on every tick that is not a row tick
        self.note = 0       # +0x3b octave << 4 | note 1..12 (octaves 3..5 for the ProTracker range)
        self.pspeed = 0     # +0x3c tone porta speed
        self.tickcnt = 0    # +0x3d ticks since the row


class Player:
    def __init__(self, img, card, master_fn=None):
        self.img, self.c = img, card
        self.mod = img.bytes(A_MOD, 0x10000)
        self.oct = [img.u16(A_OCT + 2 * i) for i in range(12)]
        self.c2spd = [img.u16(A_C2SPD + 2 * i) for i in range(16)]
        self.per36 = [img.u16(A_PER36 + 2 * i) for i in range(36)]
        self.volt = [img.u16(A_VOLT + 2 * i) for i in range(64)]
        # 4xx / 6xy: `mov al, [bx + 0x2f98]` with an address-size prefix: 16-bit addressing truncates the table
        # address 0x12f98 to 0x2f98, so the "sine" is 32 bytes of the module's pattern data (never used by the song)
        self.vib = list(img.bytes(A_VIB_BUG, 32))
        self.arpclamp = img.u8(A_ARPCLAMP)
        self.amiga = img.u32(A_AMIGA)
        self.master0 = img.u32(A_MASTER)
        self.master_fn = master_fn     # time -> master volume [0x12b8c] (the last part's fade), or None
        self.tick_fn = None            # [0x14385]: 0x2a0 (ret) or 0x14591
        self.reg_sel = 0x43            # [0x143a1]: register select restored at the end of the IRQ
        self.flag = 1                  # [0x144b1]: set by the ramp path, cleared before each wait
        self.waiter = None             # runner callback: serve the ramp IRQ the timer path waits for
        self.positions = []            # after each timer IRQ: (order index, row, countdown)
        self.order, self.row, self.counter = 0, 0, 1   # [0x12b75], [0x12b72], [0x12b71] in the image
        self.ms = 0                    # [0x14381]: 20 per tick (EAX at the tick call, stored at [0x13438])
        self.notes = 0
        self.ramps = 0
        self.vtab = {}                 # 0x14284: [0x12fb8 + 72 ch + note - 12] = vol (written only)

    def master(self):
        return self.master_fn(self.c.now) if self.master_fn else self.master0

    # ------------------------------------------------------------------------------------------ start-up
    def gus_init(self):
        """0x14935 (after 0x14b4b has read ULTRASND=240,3,3,7,7: port [0x1436c] = 0x240, IRQ [0x14389] = 7)"""
        c = self.c
        c.outb(0x000, 0x0b)                       # mix control: line in off, output off
        c.w8(0x4c, 0)                             # reset (+ delay: 7 reads of port 0x300)
        c.w8(0x4c, 1)
        c.w8(0x41, 0)
        c.w8(0x45, 0)
        c.w8(0x49, 0)
        c.w8(0x0e, (0x10 - 1) | 0xc0)             # 16 voices: 0xcf
        for v in range(31, -1, -1):
            for _ in range(2):                    # each voice twice (bl ^= 0x80)
                c.outb(0x102, v)
                c.w16(0x09, 0)
                c.w8(0x00, 0)                     # (0: running, at address 0 with FC 0)
                c.w8(0x0d, 3)
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
            self.dram_banks += 1                  # [0x14376]
        c.w8(0x4c, 7)                             # run, DAC on, GF1 IRQs on
        c.w8(0x47, self.img.u8(A_COUNT0))         # timer 2 count [0x1438a] = 0xc2: 62 x 320 us
        c.w8(0x45, 8)                             # timer 2 IRQ on
        c.outb(0x008, 4)
        c.outb(0x009, 2)                          # start timer 2
        # IRQ 7: handler 0x1443c (the word at 0x14447 is patched with `mov eax, eax`; IRQ > 7: `out 0xa0, al`)
        c.outb(0x103, 0x8f)
        c.inb(0x105)                              # acknowledge
        self.tick_fn = None                       # [0x14385] = 0x2a0 (ret)

    def load(self):
        """0x13f28 (ESI = 0xae0): header, instruments, orders, patterns -> per-channel tracks of 5-byte events,
        samples -> DRAM. Runs with interrupts off."""
        m, c = self.mod, self.c
        tag = m[0x438:0x43c]
        n = 4                                     # [0x10473]
        if tag == b'6CHN':
            n = 6
        if tag == b'8CHN':
            n = 8
        self.nch = n
        for v in range(32):                       # 0x1490c with the table 0x10575
            c.outb(0x102, v)
            c.w8(0x0c, self.img.u8(A_PAN + v))
        self.speed = 6                            # [0x1046e]
        self.bpm = 125                            # [0x1046f]
        self.maxp, self.minp = 0xd60, 0x1c0       # [0x103c8] 3424 = 856 x 4, [0x103ca] 448 = 112 x 4
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
            I.dram = struct.unpack_from('<I', self.img.b, C32 + A_INS + 0x50 * i + 0x40)[0]
            self.ins.append(I)
            o += 30
        self.songlen = m[0x3b6]                  # [0x10472] (the restart byte is ignored)
        self.orders = list(m[0x3b8:0x3b8 + 128]) + [0] * 128     # 0x10475: 128 + 128 zero bytes
        self.npat = max(self.orders[:128]) + 1  # [0x10470]: over all 128 entries
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
                        k = next((j for j in range(36) if self.per36[j] == per), 36)
                        note = ((k // 12 + 3) << 4) + k % 12 + 1      # (unmatched: k = 36 -> 0x61)
                    ev[0] = note & 0xff
                    ev[1] = (b[0] & 0xf0) | (b[2] >> 4)
                    if b[2] & 0xf == 0xc:
                        ev[2] = b[3]                  # Cxx becomes the volume byte
                    else:
                        ev[3], ev[4] = b[2] & 0xf, b[3]
            o += n * 256
        # samples: one after the other from DRAM 0, by PIO (0x14788), unchanged
        a = 0
        for I in self.ins:
            if not I.len:
                continue
            assert I.len <= 0x10000, 'a longer sample makes 0x13f28 exit the program'
            I.dram = a
            c.dram[a:a + I.len] = m[o:o + I.len]
            o += I.len
            c.addr = a + I.len - 1
            a += I.len
        self.dram_end = a
        self.mod_end = A_MOD + o
        self.reg_sel = 0x43

    def start(self):
        """0x13eb1: volumes 0, position reset, then 0x14714"""
        self.ch = [Chan() for _ in range(16)]
        self.counter = 1                          # [0x12b71]
        self.roww = 0                             # [0x12b73]
        self.row = 0x3f                           # [0x12b72]
        self.brk = 0x3f                           # [0x12b7d]
        self.nextrow = 0                          # [0x12b7e]
        self.patdelay = 0                         # [0x12b7f]
        self.order = (self.order - 1) & 0xff      # [0x12b75]: 0 - 1
        self.playing = 1                          # [0x12b77]
        self.loopcnt = 0                          # [0x12b81]
        self.looprow = 0                          # [0x12b80]
        self.pattern = self.orders[0]             # [0x12b78] (set by the load)
        # 0x14714: [0x14377] = ((2 BPM / 5) << 32) / 0x411 (never read), [0x1437f] = 20, [0x14381] = 0
        self.ms = 0
        self.tick_fn = 'play'                     # [0x14385] = 0x14591

    def stop(self):
        """0x13f18 -> 0x1475b (stop the 16 voices), then main's `call [0x12b4c]` = 0x148ca (IRQ vector back, GF1
        reset, output off)"""
        c = self.c
        self.tick_fn = None
        for v in range(15, -1, -1):
            c.outb(0x102, v)
            c.w8(0x00, 3)
        self.playing = 0
        c.w8(0x4c, 0)
        c.outb(0x000, 0x0b)

    # ------------------------------------------------------------------------------------------ interrupt
    def irq(self):
        """0x1443c"""
        c = self.c
        st = c.inb(0x006)
        if st & 0x40:
            self.ramp_irq()
        elif st & 8:
            c.w8(0x45, 0)
            c.w8(0x45, 8)
            if self.tick_fn:
                self.timer()
            self.positions.append((self.order, self.row, self.counter))
        c.outb(0x103, self.reg_sel)

    def timer(self):
        """0x14591: the tick, the timer count, then the voices one by one"""
        c = self.c
        self.ms = (self.ms + 20) & 0xffffffff
        self.tick()
        # tempo -> timer-2 count: 2 BPM / 5 ticks per second, 1e6 / that in us, / 320 us; all divisions truncate
        hz = (self.bpm * 2) // 5
        n = (1000000 // hz) // 320
        c.w8(0x47, (0x100 - n) & 0xff)
        for v in range(16):                       # ecx = 15 .. 0, voice = ecx ^ 15
            C = self.ch[v]
            c.outb(0x102, v)
            cl, C.flags = C.flags, 0
            if not cl:
                continue
            if cl & 8:
                c.outb(0x103, 9)                  # select 9, not 0x89: see Card._read
                bx = c.inw(0x104)
                c.w8(0x07, 4)                     # ramp down to 0x040 ..
                c.w8(0x08, bx >> 8)
                c.w8(0x0d, 0x60)                  # .. with the ramp IRQ
                self.ramps += 1
                self.flag = 0
                self.bx = bx                      # BX of the code the ramp IRQ interrupts
                self.waiter()                     # sti'd busy-wait at 0x14646 until the ramp path sets [0x144b1]
                assert self.flag
                cl = 7
            self.update(C, cl)

    def ramp_irq(self):
        """0x144b2: one voice whose ramp has ended: stop it, program the new sample, start it"""
        c = self.c
        c.outb(0x103, 0x8f)
        al = c.inb(0x105)
        v = al & 0x1f
        c.outb(0x102, v)
        c.w8(0x0d, 3)
        c.w8(0x00, 3)
        C = self.ch[v]
        bx = self.bx & 0xffff
        for which in ('ls', 'end', 'pos'):
            if which == 'ls':
                a = C.lstart
                if a == C.c_ls:
                    continue
                C.c_ls = a
                rh, rl = 2, 3
            elif which == 'end':
                a = C.end
                if a == C.c_end:
                    continue
                C.c_end = a
                rh, rl = 4, 5
            else:
                a = (C.start + C.offset) & 0xffffffff
                rh, rl = 0x0a, 0x0b
            bx = ((bx >> 7) | (a << 9)) & 0xffff     # shrd bx, si, 7
            c.w16(rh, (a >> 7) & 0xffff)
            c.w8(rl, bx >> 8)                 # address bits 0..6 and one bit of the previous BX
        c.w8(0x00, C.ctl)
        self.flag = 1                             # [0x144b1] = 1

    def update(self, C, cl):
        """0x14663: frequency, pan and volume of the selected voice (cl: 4 frequency, 2 pan, 1 volume)"""
        c = self.c
        if cl & 4:
            if C.period:
                hz = self.amiga // C.period
                q, r = divmod(hz * 100, 0xe74)    # FC = Hz / 37, rounded (0xe74 >> 1 with carry: 1850)
                C.fc = (q + (1 if r >= 1850 else 0)) & 0xffff
            if C.fc != C.c_fc:
                C.c_fc = C.fc
                c.w16(0x01, C.fc)
        if cl & 2 and C.pan != C.c_pan:
            C.c_pan = C.pan
            c.w8(0x0c, C.pan)
        if cl & 1:
            c.w16(0x09, self.volt[(C.vol * self.master()) >> 6])

    # ------------------------------------------------------------------------------------------ the tick
    def tick(self):
        """0x1343c"""
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
            self.order = (self.order + 1) & 0xff
            if self.order == self.songlen:
                self.order = 0
            self.pattern = self.orders[self.order]
        else:
            self.row = (self.row + 1) & 0xff
        self.nextrow = 0
        self.brk = 0x3f
        for k in range(self.nch):
            self.roww = self.row
            C = self.ch[k]
            self.row_channel(C, self.tracks[k][self.pattern * 64 + self.row])
            if C.flags & 1:
                self.vis(C, k)

    def vis(self, C, k):
        """0x14284: a table of the last volume per channel and note (72 notes, octaves 1..6), written only"""
        if C.note:
            i = (C.note >> 4) * 12 + (C.note & 0xf) - 1
            if 12 <= i <= 0x53:
                self.vtab[k * 0x48 + i - 12] = C.vol

    def note_period(self, note, c2spd):
        """0x13574 / 0x13d0a: (period of the note in the octave at 0x10390 x 0x20ab0 >> octave) / C-2 rate"""
        n = (note & 0xf) - 1
        assert 0 <= n < 12, 'note %x' % note
        p = self.oct[n] * 0x20ab0 & 0xffffffff
        return ((p >> (note >> 4)) // c2spd) & 0xffff

    def row_channel(self, C, ev):
        note, ins, vol, eff, par = ev
        C.offset = 0
        C.eff, C.par = eff, par
        if note:
            keep = 0                              # [0x12b90]
            bl = ins
            if bl == 0:
                bl = C.ins
                if bl == 0:
                    return self.row_noteless(C, ev)
                keep = 1
            C.ins = bl
            I = self.ins[(bl - 1) & 0xff]
            C.note = note
            C.c2spd = I.c2spd
            C.base = self.note_period(note, I.c2spd)
            if eff in (3, 5):                     # tone portamento: the note is only the target
                return self.row_noteless(C, ev)
            self.notes += 1
            C.period = C.base
            C.flags = 0x0c                        # frequency + restart (no pan bit)
            C.pan = 7
            C.ctl = 0
            C.vibpos = 0
            C.offset = 0
            C.start = I.dram
            C.end = (I.dram + I.len - 1) & 0xffffffff
            if I.flags & 1:
                C.lstart = (I.ls + I.dram) & 0xffffffff
                C.end = (I.le + I.dram) & 0xffffffff
                C.ctl |= 8
            if keep != 1:
                C.vol = min(I.vol, 0x3f)
                C.flags |= 1
            if eff == 0xe and par >> 4 == 0xd:
                C.flags = 0
            return self.row_volume(C, ev)
        return self.row_noteless(C, ev)

    def row_noteless(self, C, ev):
        """0x13640: an instrument number without a note (or with tone porta) resets the volume; the stored
        instrument number becomes 0-based here"""
        if ev[1]:
            b = (ev[1] - 1) & 0xff
            C.ins = b
            C.vol = min(self.ins[b].vol, 0x3f)
            C.flags |= 1
        return self.row_volume(C, ev)

    def arp_period(self, C, x):
        """0x13db5 / 0x13e21: the note x semitones up, octave wrap by 11 (not 12), clamped to B-5 if [0x12b82]"""
        cl = C.note >> 4
        n = ((C.note & 0xf) - 1 + x) & 0xffffffff
        if n > 11:
            cl += 1
            n -= 11
            if n > 11:
                cl += 1
                n -= 11
        if self.arpclamp == 1 and cl >= 6:
            cl, n = 5, 11
        return (((self.oct[n] * 0x20ab0) & 0xffffffff) >> cl) // C.c2spd & 0xffff

    def row_volume(self, C, ev):
        """0x13670: the volume byte (Cxx), then the row effect"""
        if ev[2] != 0xff:
            C.flags |= 1
            C.vol = min(ev[2], 0x3f)
        C.tickcnt = 0
        eff, par = C.eff, C.par
        if eff == 0:                              # 0x13da0 arpeggio
            if par:
                C.arpcnt = 0
                C.arp[0] = C.period
                C.arp[1] = self.arp_period(C, par >> 4)
                C.arp[2] = self.arp_period(C, par & 0xf)
        elif eff == 3:                            # 0x13aec
            if not par:
                par = C.pspeed
            C.pspeed = C.par = par
        elif eff == 4:                            # 0x13a30
            d, s = par & 0xf, par & 0xf0
            if not d:
                d = C.vdepth
            if not s:
                s = C.vspeed
            C.vdepth, C.vspeed = d, s
            C.par = d | s
        elif eff == 9:                            # 0x138c1 (no test for 900: offset 0, restart anyway)
            off = par << 8
            lim = (C.end - C.start) & 0xffffffff
            if off >= lim:
                off = (lim - 1) & 0xffffffff
            C.offset = off
            C.flags |= 8
        elif eff == 0xb:                          # 0x13ca6
            self.brk = self.roww & 0xff
            self.order = (par - 1) & 0xff
        elif eff == 0xd:                          # 0x1388a
            if par >= 0x64:
                par = 0x63
            self.nextrow = ((par & 0xf) + (par >> 4) * 2 + (par >> 4) * 8) & 0xff
            self.brk = self.roww & 0xff
        elif eff == 0xf:                          # 0x1385c
            if par == 0:
                pass                              # (F00: nothing)
            elif par > 0x1f:
                self.bpm = par
            else:
                self.speed = self.counter = par
        elif eff == 0xe:
            x, y = par >> 4, par & 0xf
            if x == 1:                            # 0x13c1a
                p = (C.period - y * 4) & 0xffff
                C.period = self.minp if p < self.minp else p
                C.flags |= 4
            elif x == 2:                          # 0x13c48
                p = (C.period + y * 4) & 0xffff
                C.period = self.maxp if p > self.maxp else p
                C.flags |= 4
            elif x == 5:                          # 0x13d0a
                C.base = C.period = self.note_period(C.note, self.c2spd[y])
            elif x == 6:                          # 0x13d46
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
                    self.order = (self.order - 1) & 0xff
            elif x == 8:                          # 0x13cfe
                C.pan = y
                C.flags |= 2
            elif x == 0xa:                        # 0x13c76
                C.vol = (C.vol + y) & 0xff
                if C.vol > 0x3f:
                    C.vol = 0x3f
                C.flags |= 1
            elif x == 0xb:                        # 0x13c90
                C.vol = C.vol - y if C.vol >= y else 0
                C.flags |= 1
            elif x == 0xd:                        # 0x13ce5: no start on the row
                C.flags = 0
            elif x == 0xe:                        # 0x13cee
                self.patdelay = y
            # E0, E3, E4, E7, E9 (retrigger!), EC (on the row), EF: nothing

    def tick_effects(self):
        """0x136de: every channel on the ticks between rows"""
        for k, C in enumerate(self.ch[:self.nch]):
            C.tickcnt = (C.tickcnt + 1) & 0xff
            C.arpcnt += 1
            if C.arpcnt == 3:
                C.arpcnt = 0
            eff, par = C.eff, C.par
            if eff == 0:                          # 0x13e92
                if par:
                    C.period = C.arp[C.arpcnt]
                    C.flags = 4                   # (mov: replaces the flags)
            elif eff in (1, 2):                   # 0x138e3 / 0x13951
                if C.period:
                    if eff == 1:
                        p = (C.period - (par << 2)) & 0xffff
                        C.period = self.minp if p - 0x10000 * (p >> 15) < self.minp else p
                    else:
                        p = (C.period + (par << 2)) & 0xffff
                        C.period = self.maxp if p > self.maxp else p
                    C.flags |= 4
            elif eff in (3, 5):                   # 0x13a95 / 0x13b03
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
            elif eff in (4, 6):                   # 0x139bf / 0x13b7f
                if eff == 6:
                    self.volslide(C, ((par & 0xf) - (par >> 4)) & 0xff)
                    p = C.vdepth | C.vspeed
                else:
                    p = par
                C.vibpos = (C.vibpos + ((p & 0xf0) >> 2)) & 0xff
                ax = self.vib[(C.vibpos >> 2) & 0x1f] * (p & 0xf)    # mul dl
                r = ((ax << 1) | (ax >> 15)) & 0xffff                     # rol ax, 1
                ax = (r >> 8) | (r & 1) << 8                              # xchg al, ah; and ah, 1: ax >> 7
                if not C.vibpos & 0x80:
                    ax = -ax
                ax = ((ax << 2) + C.base) & 0xffff
                if eff == 4:
                    if ax - 0x10000 * (ax >> 15) < self.minp:
                        ax = self.minp
                elif ax < self.minp:
                    ax = self.minp
                if ax > self.maxp:
                    ax = self.maxp
                C.period = ax
                C.flags |= 4 if eff == 4 else 5
            elif eff == 0xa:                      # 0x13a5e: up x, else down y
                x, y = par >> 4, par & 0xf
                self.volslide(C, (-x & 0xff) if x else y)
                C.flags |= 1
            elif eff == 0xe:
                x, y = par >> 4, par & 0xf
                if x == 0xc and y == C.tickcnt:   # 0x13cbd note cut
                    C.vol = 0
                    C.flags |= 1
                elif x == 0xd and y == C.tickcnt:  # 0x13cd3 note delay
                    C.flags |= 0xf
            if C.flags & 1:
                self.vis(C, k)

    @staticmethod
    def volslide(C, ah):
        """vol - ah (8 bit): a negative result -> 0, >= 64 -> 63"""
        al = (C.vol - ah) & 0xff
        if al & 0x80:
            al = 0
        C.vol = 0x3f if al >= 0x40 else al


# ------------------------------------------------------------------------------------------------ the capture
def read_irqs(path):
    """the capture's interrupts: (tick, kind, service time)"""
    out = []
    for ln in open(path):
        f = ln.split()
        if len(f) == 4 and f[1] == 'irq':
            out.append((int(f[0]), f[2], float(f[3])))
    return out


def read_frames(path):
    """full.ticks: (frame, start time, order, row, countdown, music ticks)"""
    out = []
    for ln in open(path):
        if ln.startswith('#'):
            continue
        f = ln.split()
        out.append((int(f[0]), float(f[1]), int(f[2]), int(f[3]), int(f[4]), int(f[5])))
    return out


# The 50 Hz mode (main 0x100f0): 808 dots x 625 lines at 25.175 MHz, vertical retrace from line 502 (CRTC 10h = 0xf6,
# overflow bit 8). Each part's retrace IRQ 0 handler calls its callback at the retrace start.
FRAME = 808 * 625 / 25.175e6           # 20.0596 ms
RETRACE = 502 * 808 / 25.175e6         # 16.112 ms after the frame start


class Fade:
    """The last part's callback (0x4d6b1..0x4d70b, core scope) once per refresh: while [0x12b75] = 0x1e and the row
    word [0x12b73] >= 0x20 it lowers [0x3ceac] (32 -> 0) and with it the master volume [0x12b8c] by 1, so the
    master goes 63 -> 31 over 32 refreshes; the part's main loop then leaves and main stops the music. Model: the
    callbacks at frame k's retrace start (k x FRAME + RETRACE); the position at that time is the player's. Only the
    last part touches [0x3ceac] (32 in the image) and the condition only holds in its time, so the model runs the
    callback on every refresh from frame 0 (the 50 Hz mode's frame 0 = the capture's time 0)."""

    def __init__(self, master0, cnt0):
        self.master0 = master0
        self.dec = []                  # times of the decrements
        self.k = 0
        self.cnt = cnt0                # [0x3ceac]

    def advance(self, t, pos_at):
        """run the callbacks up to time t; pos_at(time) -> (order, row word) of the player at that time"""
        while self.k * FRAME + RETRACE < t:
            tc = self.k * FRAME + RETRACE
            o, r = pos_at(tc)
            if r >= 0x20 and o == 0x1e:
                if self.cnt:
                    self.cnt -= 1
                    if self.master0 - len(self.dec) > 0:
                        self.dec.append(tc)
            elif self.cnt != 0x20:
                self.cnt += 1
            self.k += 1

    def __call__(self, t):
        return self.master0 - sum(1 for x in self.dec if x < t)


def run(img, card, irqs=None, ticks=CAPTURE_TICKS, start_after=START_AFTER, hw=None, holds=(), other_read=0,
        fade=True):
    """Run the player as the capture did: init at t = 0, load, `start_after` timer IRQs, start, then the IRQs
    (the capture's service times if `irqs` is given, else at their hardware times, with `holds` = [(from, to)]
    windows in which the CPU has interrupts off), then the stop after `ticks` timer interrupts.
    Returns the player and a list of (kind, event time, service time, player started)."""
    times = []                                    # (service time, order, row word) after each timer IRQ
    fd = None
    pl = Player(img, card)

    def pos_at(t):
        o, r = pl.order0, 0
        for tt, oo, rr in reversed(times):
            if tt <= t:
                return oo, rr
        return o, r

    if fade:
        fd = Fade(img.u32(A_MASTER), img.u32(A_BRIGHT))
        pl.master_fn = fd
    card.hw = hw
    pl.gus_init()                                 # at t = 0: timer 2 starts with count 0xc2
    pl.load()                                     # (instant here; with interrupts off on the original)
    pl.order0 = 0
    sched = []
    it = iter(irqs) if irqs is not None else None
    state = {'ntimer': 0}

    def held(t):
        for a, b in holds:
            if a <= t < b:
                return b
        return t

    def next_irq(want=None):
        """the next interrupt: (kind, event time, service time)"""
        te, k = card.next_event()
        if it is not None:
            _, kind, tlog = next(it)
            td = tlog if tlog - te > 1.5e-6 else te + (1e-7 if k == 'r' else 0.0)
        else:
            kind, td = k, held(te + (1e-7 if k == 'r' else 0.0))
        if want:
            assert kind == want, 'expected a %s interrupt, got %s at %.6f' % (want, kind, td)
        return kind, te, td, (tlog if it is not None else td)

    def serve(kind, te, td, tlog):
        if fd is not None:
            fd.advance(td, pos_at)
        card.events(td)
        assert card.irq(), 'no interrupt pending at %.6f' % td
        got = 'r' if card.status & 0x40 else 't'
        assert got == kind, 'interrupt kind %s, capture %s at %.6f' % (got, kind, td)
        if kind == 't':
            state['ntimer'] += 1
            card.tick = state['ntimer']
        sched.append((kind, te, td, bool(pl.tick_fn)))
        card.lines.append('%d irq %s %.6f' % (card.tick, kind, tlog))
        if hw is not None:
            hw.at(td)
            if kind == 't':
                hw.marks.append((hw.frame, hw.frame_rate(), td))
        pl.irq()

    def waiter():
        serve(*next_irq('r'))

    pl.waiter = waiter
    while state['ntimer'] < ticks:
        kind, te, td, tlog = next_irq('t')
        serve(kind, te, td, tlog)
        if pl.tick_fn:
            times.append((td, pl.order, pl.roww))
        if state['ntimer'] == start_after:
            pl.prestart = pl.positions[-1]        # (frames recorded before the start see the image's values)
            pl.start()                            # (the first part's set-up, between two timer interrupts)
            pl.positions[-1] = (pl.order, pl.row, pl.counter)
            card.lost = 0
    if hw is not None:
        hw.at(card.t2_next)                       # one more tick period of sound
    pl.stop()
    pl.fade = fd
    return pl, sched


def check(a, img):
    irqs = read_irqs(os.path.join(a.check, 'full.gus'))
    card = Card(other_read=a.other_read)
    pl, sched = run(img, card, irqs=None if a.ideal else irqs, ticks=a.ticks or sum(1 for x in irqs if x[1] == 't'),
                    other_read=a.other_read, holds=capture_holds(irqs) if a.ideal else ())
    ok = True
    ref = open(os.path.join(a.check, 'full.dram'), 'rb').read()
    nd = sum(1 for x, y in zip(ref, card.dram) if x != y)
    print('DRAM: %d of %d bytes differ (samples 0..0x%x, probe bytes 0x3ffff, 0x7ffff, 0xbffff, 0xfffff = 1)' % (
        nd, len(ref), pl.dram_end))
    ok &= nd == 0
    ref = [ln for ln in open(os.path.join(a.check, 'full.gus')).read().split('\n') if ln and ln[0] != '#']
    mine = card.lines
    if a.ideal:                               # the service times differ: compare without the irq lines' times
        ref = [' '.join(x.split()[:3]) if ' irq ' in x else x for x in ref]
        mine = [' '.join(x.split()[:3]) if ' irq ' in x else x for x in mine]
    nd = next((i for i, (x, y) in enumerate(zip(ref, mine)) if x != y), None)
    if nd is None and len(ref) == len(mine):
        print('GUS log: %d lines, identical%s' % (len(ref), ' (irq times not compared)' if a.ideal else ''))
    else:
        ok = False
        i = nd if nd is not None else min(len(ref), len(mine))
        ndiff = sum(1 for x, y in zip(ref, mine) if x != y)
        print('GUS log: %d lines (capture %d), %d differ; first difference at line %d:' % (
            len(mine), len(ref), ndiff, i + 2))
        for j in range(max(0, i - 8), min(i + 8, max(len(ref), len(mine)))):
            print('  %-32s %s' % (ref[j] if j < len(ref) else '-', mine[j] if j < len(mine) else '-'))
    run_ = [x[:3] for x in sched if x[3]]
    late = [(k, te, td) for k, te, td in run_ if td - te > 1.5e-6]
    early = [(k, te, td) for k, te, td in run_ if te - td > 1.5e-6]
    print('interrupts after the start: %d timer, %d ramp; served after their event by more than 1.5 us: %d '
          '(max %.3f ms); before it: %d' % (
              sum(1 for x in run_ if x[0] == 't'), sum(1 for x in run_ if x[0] == 'r'), len(late),
              1e3 * max((td - te for _, te, td in late), default=0), len(early)))
    ok &= not early
    print('timer overflows lost (previous one still pending): %d; note starts %d, ramps %d' % (
        card.lost, pl.notes, pl.ramps))
    if pl.fade is not None and pl.fade.dec:
        print('master volume 63 -> %d: %d decrements from %.4f s to %.4f s' % (
            pl.fade(1e9), len(pl.fade.dec), pl.fade.dec[0], pl.fade.dec[-1]))
    bad = n = 0
    for f in read_frames(os.path.join(a.check, 'full.ticks')):
        o, r, cnt, g = f[2], f[3], f[4], f[5]
        if START_AFTER <= g <= len(pl.positions):
            n += 1
            if pl.positions[g - 1] != (o, r, cnt) and not (g == START_AFTER and pl.prestart == (o, r, cnt)):
                bad += 1
                if bad < 5:
                    print('  frame %d tick %d: capture %s, model %s' % (f[0], g, (o, r, cnt), pl.positions[g - 1]))
    print('music position (order, row, countdown): %d frames compared, %d differ' % (n, bad))
    ok &= bad == 0
    print('ALL MATCH' if ok else 'MISMATCH')
    if a.out:
        with open(a.out + '.gus', 'w') as f:
            f.write('# <music tick> v<voice> r<reg> <value> | p<port> <value> | irq <t/r/w> <time s>\n')
            f.write('\n'.join(card.lines) + '\n')
        open(a.out + '.dram', 'wb').write(card.dram)
    return pl, sched


def capture_holds(irqs, period=62 * 320e-6):
    """the windows in which the capture's CPU held the timer interrupt off by more than 1 ms (the parts' PIT
    calibration with cli): (time of the first overflow it held, its service time). The overflows in a window after
    the first are lost."""
    out = []
    kprev = 0
    for _, kind, t in irqs:
        if kind != 't':
            continue
        k = math.floor(t / period + 1e-6)         # the overflow served (the last one before t)
        if t - k * period > 1e-3:
            out.append(((kprev + 1) * period - 1e-9, t + 1e-9))
        kprev = k
    return out


# ------------------------------------------------------------------------------------------------ GF1 model
class GF1:
    """What the card does with the writes: dope/re/models/music/dopeplay.py's GF1 model (jizz/remake/src/gf1.cpp,
    paper/re/notes-music.md section 7), forward loops only. Positions in 1/512 byte, increment FC >> 1 per frame,
    12-bit log volume in 1/512 steps with ramps (63 steps per frame here), pan 0..15 with a linear law,
    617400 / active frames per second (16: 38,587.5 Hz), 8-bit samples linearly interpolated, stopped voices mixed
    at their parked position. 8-bit writes to the address registers 3 / 5 / 0x0b combine with the low byte of the
    last 16-bit write. Register writes apply at the interrupt's time (the frames that start before it are rendered
    first)."""

    def __init__(self, dram):
        import numpy as np
        self.np = np
        self.dram = dram
        self.dram_f = None
        e = np.arange(4096) >> 8
        m = np.arange(4096) & 255
        self.gain = (256 + m) * np.exp2(e - 24.0)
        self.latch = 0
        self.out = []
        self.marks = []
        self.frame = 0
        self.seg = None
        self.reset()

    def reset(self):
        self.active = 14
        self.dac = False
        self.v = [{'ctl': 3, 'fc': 0, 'start': 0, 'end': 0, 'pos': 0, 'vctl': 3, 'rate': 0, 'lo': 0, 'hi': 0,
                   'vol': 0, 'pan': 7} for _ in range(32)]

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
        if self.frame_rate() != self.rate_seen:
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
        else:
            assert (ctl & 0x18) != 0x18 and not ctl & 0x40, 'bidirectional / backward voices are not used'
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


def render(a, img):
    """GF1 render with the interrupts at their hardware events (timer overflows, ramp ends of the card model),
    from the first player tick to one tick period after the last of --ticks timer interrupts"""
    import numpy as np
    import wave
    card = Card(other_read=a.other_read)
    gf = GF1(card.dram)
    # (lost overflows do not advance the player: the same number of served interrupts ends at the same position,
    # 7 x 19.84 ms earlier than in the capture)
    ticks = a.ticks if a.ticks is not None else CAPTURE_TICKS
    pl, sched = run(img, card, irqs=None, ticks=ticks, hw=gf, other_read=a.other_read, fade=not a.no_fade)
    print('stopped after timer interrupt %d at position %d:%d (capture: 9203, 30:37)' % (
        ticks, pl.order, pl.row))
    pcm = np.concatenate(gf.out) if gf.out else np.zeros((0, 2))
    f0, rate, t0 = gf.marks[START_AFTER]          # from the first player tick (timer interrupt 4)
    pcm = pcm[f0:]
    peak = float(np.abs(pcm).max()) if len(pcm) else 0.0
    print('rendered %d frames at %.2f Hz (%.2f s from the first player tick at %.5f s); peak %.3f (1.0 = one voice '
          'at full scale and gain 1, hard panned)' % (len(pcm), rate, len(pcm) / rate, t0, peak))
    with wave.open(a.wav, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(int(round(rate)))
        w.writeframes((np.clip(pcm / max(peak, 1e-9) * 0.9, -1, 1) * 32767).astype('<i2').tobytes())


def info(img):
    import collections
    card = Card()
    pl = Player(img, card)
    pl.gus_init()
    pl.load()
    m = pl.mod
    print('module at code32 0x%x..0x%x: tag %s, %d channels, %d patterns, song length %d (restart byte %d '
          'ignored)' % (A_MOD, pl.mod_end, m[0x438:0x43c].decode(), pl.nch, pl.npat, pl.songlen, m[0x3b7]))
    print('orders: ' + ' '.join('%d' % x for x in pl.orders[:pl.songlen]))
    print(' ins  name                    DRAM start  length  loop start  loop end  flags  ft  c2spd  vol')
    for i, I in enumerate(pl.ins):
        if not I.len:
            continue
        name = m[20 + 30 * i:42 + 30 * i].split(b'\0')[0].decode('latin-1')
        print('%4d  %-22s  %8x  %6d  %8x  %8x  %5d  %2d  %5d  %3d' % (
            i + 1, name, I.dram, I.len, I.dram + I.ls if I.flags else 0,
            I.dram + I.le if I.flags else I.dram + I.len - 1, I.flags, I.ft, I.c2spd, I.vol))
    used = collections.Counter()
    for o in range(pl.songlen):
        p = pl.orders[o]
        for ch in range(pl.nch):
            for r in range(64):
                note, ins, vol, eff, par = pl.tracks[ch][p * 64 + r]
                if vol != 0xff:
                    used['C (volume byte)'] += 1
                    if vol > 0x3f:
                        used['C > 3F (-> 63)'] += 1
                if eff == 0xe:
                    used['E%X%X' % (par >> 4, par & 15) if par >> 4 in (9, 0xc) else 'E%X' % (par >> 4)] += 1
                elif eff or par:
                    used['%X' % eff] += 1
                if note and not ins:
                    used['note without instrument'] += 1
                if ins and not note:
                    used['instrument without note'] += 1
                if note == 0x61:
                    used['unmatched period'] += 1
    print('cells in the played orders: ' + ', '.join('%s x%d' % kv for kv in sorted(used.items())))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--image', default=os.path.join(RE, 'unpacked', 'korso2.bin'))
    ap.add_argument('--check', default=RE, help='folder with full.gus / full.dram / full.ticks')
    ap.add_argument('--ideal', action='store_true', help='serve the interrupts at their events (no latency); '
                    'the capture\'s held-off timer interrupts (part starts) are kept as holds')
    ap.add_argument('--out', help='write PREFIX.gus and PREFIX.dram')
    ap.add_argument('--other-read', type=lambda s: int(s, 0), default=0,
                    help='what a read of register select 0x09 returns (the emulator: 0)')
    ap.add_argument('--ticks', type=int, help='timer interrupts before the stop (check: the capture\'s 9203; '
                    '--wav: the same 9203)')
    ap.add_argument('--no-fade', action='store_true', help='render without the last part\'s master volume fade')
    ap.add_argument('--times', help='write per timer interrupt: service time, order, row, countdown')
    ap.add_argument('--wav', help='render through the GF1 model (needs numpy)')
    ap.add_argument('--info', action='store_true')
    a = ap.parse_args()
    img = Img(a.image)
    if a.info:
        return info(img)
    if a.wav:
        return render(a, img)
    pl, sched = check(a, img)
    if a.times:
        tt = [(te, td) for k, te, td, _ in sched if k == 't']
        with open(a.times, 'w') as f:
            f.write('# timer interrupt, overflow time (s), service time (s), order, row, countdown after it\n')
            for k, ((te, td), pos) in enumerate(zip(tt, pl.positions)):
                f.write('%d %.6f %.6f %d %d %d\n' % (k + 1, te, td, pos[0], pos[1], pos[2]))


if __name__ == '__main__':
    main()

"""Reference implementation of Reve's music: LCA's MOD-Player V3.03 as REVE.LFL links it, GUS path, mirroring the
assembly routine by routine (see ../../notes-music.md), plus a GF1 model to render it. Started from STARS's model of
V3.02 (stars/re/models/music/lcaplay.py); the differences are marked "V3.03".

From the module (ZIC\\PULSE.MOD, unpacked/files/ZIC/PULSE.MOD) and reve_img.bin (the player's tables) it produces
  - the GUS DRAM image (the sample upload 0x26e36f / 0x26e9bb)             -> compare with full.dram
  - the GF1 register-write log, in run_reve.py's text format, from the
    card detection to the stop (0x25f698, 0x26e16c, 0x26ec3e, ticks, 0x26ecc4)  -> compare with full.gus
  - the GUS timer interrupts and the music position (order, row) after each, placed on the capture's clock
    (the program time at which 0x26ec3e started the GUS timers)            -> compare with full.ticks
and with --wav renders the song through a GF1 model (16 active voices: 38,587.5 frames per second), with the
player ticks placed on the GUS timers as the card clocks them (timer 1 in 80 us steps, timer 2 in 320 us steps).

usage:  python3 lcaplay.py [--check DIR | --check ""] [--prefix full] [--ticks N] [--out PREFIX] [--times FILE]
                          [--info] [--wav out.wav]
        (run inside the toolbox from this folder; reads ../../unpacked and ../../full.*)
The run ends where main stops the music (210 refreshes after the last script entry's end), or before tick N.

The player reads no GUS register that matters (only the status registers 0x41 / 0x49 / 0x8f and port 2X6 as
acknowledgements), so the register stream does not depend on the card's answers: the same stream drives real
hardware and the capture's fake card.

Routine map (linear addresses, image at 0x20427c):
  0x25f698 detect (0x25f75b GUS: 0x25f793 ULTRASND=, 0x25f8d7 GF1 init + IRQ/DMA latches; 0x25f819 ULTRA16=;
          0x25fbc6 SB probe, only if no GUS)       0x260340 options / extra channels (BL = 0 here)
  0x2601dc load ZIC\\PULSE.MOD -> 0x26e16c convert (standard 31-sample MOD) + upload
  0x2602a1 start -> 0x26ec3e      0x2602c6 stop -> 0x26ecc4      0x26ed0e GF1 reset     0x26e8ba period -> FC table
  0x26e9bb / 0x26ea91 / 0x26eac8 upload (split, PIO, DMA)   0x26ec04 DMA IRQ handler   0x26e91d / 0x26e990 IRQ vector
  0x26eecc timer IRQ = player tick: 0x26ef18 tick effects (table 0x26ef34, E 0x26f224), 0x26f544 row (table
          0x26f5cc, E 0x26f790), 0x26fa5c note start, 0x26fb7c volume ramp + frequency write
  0x2602eb position (AL order, AH row); the demo's timer interrupt swaps it to order << 8 | row
"""
import argparse
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))

GF1_CLOCK = 617400.0          # GF1 voice frames per second * active voices (1 / 1.619695497 us)
BASE = 0x240                  # ULTRASND=240,3,3,7,7 (the capture's environment)
DMA_CH, IRQ_NO = 3, 7
IMG_BASE = 0x20427c           # REVE.LFL's image in the headless run
PIT_HZ = 1193182
VGA_HZ = 25175000 / (800 * 449)
FRAME = PIT_HZ / VGA_HZ       # PIT ticks per screen refresh
VS_LINE = 412 / 449           # record k of the capture is taken at the start of the retrace: (k + 412/449) refreshes

# addresses in the program image (V3.02 address in STARS.EXE + 0x244484, see vs302.py)
A_PTAB = 0x26db4c             # 16 finetunes x 36 periods (u16), finetune 0 first; 8..15 = -8..-1
A_SINE = 0x26dfcc             # 32 bytes: vibrato / tremolo sine (half wave)
A_VOLT = 0x26e060             # 65 u16: channel volume 0..64 -> GF1 volume (8-bit regs use the high byte)
A_PAN = 0x26e03f              # 32 bytes: pan of voice 0..31
A_DIV = 0x26e0e2              # u16 per voice count 14..32: 617400 / voices (truncated)
A_IRQLATCH, A_DMALATCH = 0x26e027, 0x26e037
A_FCNUM = 0x26e8f3            # immediate of 0x26e8f2 `mov eax, 0xda7a69e2` (3,579,545.5 Hz x 1024)
A_VOICES = 0x26e012           # [0x26e012]: GF1 voices (14 in the image, 2 x channels after the conversion)
START_PIT = 509331.124025      # program time (PIT ticks) at which 0x26ec3e starts the GUS timers in the capture
                              # (run_reve.py --music-log): the retrace of refresh 29
STOP_AFTER_LAST_ENTRY = 210   # refreshes from the retrace at which the last script entry ends to main's stop (0x2602c6)
MODULE_ADDR = 0x345e30        # where the extender loads ZIC\PULSE.MOD in the headless run (full.log, FLAT 0209):
                              # the sample DMA blocks split at the 64 KB pages of this address


def s32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


class Img:
    def __init__(self, path):
        self.b = open(path, 'rb').read()

    def u8(self, a): return self.b[a - IMG_BASE]
    def u16(self, a): return struct.unpack_from('<H', self.b, a - IMG_BASE)[0]
    def u32(self, a): return struct.unpack_from('<I', self.b, a - IMG_BASE)[0]


class Card:
    """The GUS as run_reve.py's Gus class logs it: '<tick> v<voice> r<reg> <val>' for voice registers (< 0x40,
    the value as written: byte for 8-bit writes, word for 16-bit writes), '<tick> g r<reg> <val>' for global
    registers (>= 0x40, except the DRAM address 0x43 / 0x44), '<tick> p<port> <val>' for other ports in
    0x240..0x24f and 0x340..0x347 (the voice select 0x342, the register select 0x343 and DRAM data 0x347 are not
    logged). The tick stamp is the number of GUS timer interrupts so far (run_reve.py counts an interrupt when the
    timer overflows): the writes of player tick k (0-based) carry k + 1, the start-up ones 0.
    The DRAM is written by DMA (reg 0x41 bit 0) and by PIO (0x43 / 0x44 + 0x347)."""

    def __init__(self):
        self.lines = []
        self.tick = 0
        self.voice = 0
        self.dram = bytearray(1 << 20)
        self.hw = None                # optional GF1 model: hw.write(voice, reg, value, size)
        self.timers = None            # optional Timers: AdLib control and regs 0x45..0x47

    def port(self, p, val):
        if 0x240 <= p < 0x350:
            self.lines.append('%d p%03x %x' % (self.tick, p, val))
        if self.timers is not None and p == BASE + 9:
            self.timers.control(val)  # (2X8 = 4 always precedes it in this player)

    def select(self, v):
        self.voice = v & 31

    def _w(self, reg, val, size):
        if reg in (0x43, 0x44):
            return
        if reg >= 0x40:
            self.lines.append('%d g r%02x %x' % (self.tick, reg, val))
            if self.timers is not None and 0x45 <= reg <= 0x47:
                self.timers.write(reg, val)
        else:
            self.lines.append('%d v%d r%02x %x' % (self.tick, self.voice, reg, val))
        if self.hw is not None:
            self.hw.write(self.voice, reg, val, size)

    def w8(self, reg, val):          # reg -> 3X3, byte -> 3X5
        self._w(reg, val & 0xff, 1)

    def w16(self, reg, val):         # reg -> 3X3, word -> 3X4
        self._w(reg, val & 0xffff, 2)


class Sample:
    """record of 0x1c bytes at 0x26c3cc + 0x1c * (n - 1) (pointer table 0x26ce48 + 4 n); V3.02's 0x18-byte record
    plus +0x18 (u16, the rate of a sound-effect sample added by 0x26fca2: unused here)"""

    def __init__(self):
        self.len = 0        # +0x00 length in bytes; after the upload: DRAM address of the end
        self.ft = 0         # +0x04 finetune 0..15
        self.vol = 0        # +0x05 volume 0..64
        self.flag = 0       # +0x06 GF1 voice control: 8 = loop, 0 = one-shot
        self.ls = 0         # +0x08 loop start (bytes); after the upload: DRAM address
        self.le = 0         # +0x0c loop end (bytes) = min(ls + replen, len); after the upload: DRAM address
        self.dram = 0       # +0x10 DRAM start
        self.lerel = 0      # +0x14 loop end relative to the start (the 9xx limit)


class Chan:
    """channel record, 0x34 bytes at 0x26d4cc + 0x34 * ch (offsets in the comments; as in V3.02)"""

    def __init__(self, ch):
        self.start = 0      # +0x04 DRAM address the next note starts at
        self.smp = None     # +0x08 sample record
        self.period = 0     # +0x10
        self.target = 0     # +0x14 tone portamento target (0 = reached)
        self.fc = 0         # +0x18 GF1 frequency to write (0 = nothing pending)
        self.eff = 0        # +0x20 effect word (effect << 8 | parameter) for the ticks
        self.ins = 0        # +0x22
        self.vol = 0        # +0x23 volume 0..64
        self.ft = 0         # +0x24 finetune
        self.arp = 0        # +0x25 arpeggio counter
        self.ctl = 0        # +0x26 voice control of the sample (8 loop / 0)
        self.gvol = 0       # +0x27 volume last sent to the GF1
        self.pspeed = 0     # +0x28 tone portamento speed
        self.voice = 2 * ch  # +0x29 GF1 voice: 2 ch or 2 ch + 1 (toggled at every note start)
        self.vib = 0        # +0x2a vibrato speed << 4 | depth
        self.trem = 0       # +0x2b tremolo speed << 4 | depth
        self.vibpos = 0     # +0x2c
        self.trempos = 0    # +0x2d
        self.wave = 0       # +0x2e vibrato waveform (bits 0-1), tremolo waveform (bits 4-5)
        self.tbase = 0      # +0x2f tremolo base volume
        self.retrig = 0     # +0x30 E9x counter
        self.gliss = 0      # +0x31 E3x
        self.newsmp = 0     # +0x32 notes left that write the loop registers (2 after an instrument number)


class Player:
    def __init__(self, img, mod, card):
        self.img, self.mod, self.c = img, mod, card
        self.pt = [img.u16(A_PTAB + 2 * i) for i in range(16 * 36 + 64)]   # (+64: what follows, never reached)
        self.sine = [img.u8(A_SINE + i) for i in range(32)]
        self.volt = [img.u16(A_VOLT + 2 * i) for i in range(65)]
        self.pan = [img.u8(A_PAN + i) for i in range(32)]
        self.voices = img.u8(A_VOICES)  # [0x26e012] = 14 until the conversion
        self.nch = 4                # [0x26040b] module channels
        self.extra = 0              # [0x26040e] V3.03: extra (sound effect) channels, BL of 0x25f698 = 0 here
        self.allch = 4              # [0x26040f] V3.03: module + extra channels
        self.nsmp = 31              # [0x26040a]
        self.maxvol = 0x40          # [0x26040c]
        self.base = MODULE_ADDR     # the module's physical address (decides where the DMA blocks split)
        self.positions = []

    # ------------------------------------------------------------------------------------------- start-up
    def detect(self):
        """0x25f698 (AL = 3: options, BL = 0: no extra channels; [0x26e10c] = 45000, the MIX rate, not 0):
        GUS (0x25f75b); V3.03: the SB probe 0x25fbc6 runs only when no GUS was found."""
        c = self.c
        # 0x25f793: ULTRASND= -> port 0x240, DMA 3, DMA2 3, IRQ 7, IRQ2 7 (0x26dfec / 6 / 8 / a / c)
        # 0x25f8d7: hard init (= V3.02 0x1bceb)
        c.w8(0x4c, 0)
        c.w8(0x4c, 1)
        c.port(BASE + 0x100, 3)                   # MIDI control: reset
        c.port(BASE + 0x100, 0)
        c.w8(0x41, 0)
        c.w8(0x45, 0)
        c.w8(0x49, 0)
        c.w8(0x0e, (self.voices - 1) | 0xc0)      # 14 voices
        for v in range(31, -1, -1):
            c.select(v)
            c.w8(0x00, 3)
            c.w8(0x0d, 3)
        c.w8(0x4c, 7)
        for v in range(31, -1, -1):
            c.select(v)
            for r in (2, 3, 4, 5):
                c.w16(r, 0)
            c.w8(0x06, 0x3f)
            c.w16(0x09, 0)
            c.w16(0x0a, 0)
            c.w16(0x0b, 0)
            c.w8(0x0c, 7)
        # IRQ / DMA latches: IRQ 7 -> 4, same IRQ for both -> | 0x40; DMA 3 -> 2, same -> | 0x40
        irq = self.img.u8(A_IRQLATCH + IRQ_NO) | 0x40
        dma = self.img.u8(A_DMALATCH + DMA_CH) | 0x40
        c.port(BASE + 0xf, 5)
        c.port(BASE, 0xb)
        c.port(BASE + 0xb, 0)
        c.port(BASE + 0xf, 0)
        c.port(BASE, 0xb)
        c.port(BASE + 0xb, dma | 0x80)
        c.port(BASE, 0x4b)
        c.port(BASE + 0xb, irq)
        c.port(BASE, 0xb)
        c.port(BASE + 0xb, dma)
        c.port(BASE, 0x4b)
        c.port(BASE + 0xb, irq)
        c.select(0)
        c.port(BASE, 0xb)
        c.select(0)
        c.port(BASE, 8)
        # 0x25f819: no ULTRA16= in the environment

    def convert(self):
        """0x26e16c (V3.03: called by the load entry 0x2601dc; in V3.02 this standard-MOD converter, 0x19dd0, had no
        caller): ESI = module, EDI = pattern buffer. Channels, samples, orders, patterns, upload, period -> FC
        table, player state."""
        m, c = self.mod, self.c
        sig = m[0x438:0x43a]
        al, ah = sig[0], sig[1]
        if al == 0x4d:                            # 'M.K.'
            al, ah = 0x30, 0x34
        if 0x30 <= ah <= 0x39:
            al, ah = ah, al
        assert 0x30 <= al <= 0x39                 # (other signatures: 0x26e1e7, not this module)
        if not (0x30 <= ah < 0x39):
            ah = 0x30
        self.nch = ((ah - 0x30) * 10 + (al - 0x30)) & 0xff   # '8CHN' -> 8
        if self.nch > 7:
            self.voices = 2 * self.nch            # 8 channels -> 16 voices
        o = 0x14
        self.smp = [None]
        for i in range(self.nsmp):                # 0x26e26a
            s = Sample()
            ln, ft, vol, ls, lr = struct.unpack_from('>HBBHH', m, o + 0x16)
            s.len, s.ft, s.vol, s.ls = ln * 2, ft, vol, ls * 2
            s.flag = 8 if lr * 2 > 2 else 0
            s.le = min(ls * 2 + lr * 2, s.len)
            self.smp.append(s)
            o += 0x1e
        self.songlen = m[o]
        self.restart = m[o + 1] if m[o + 1] <= m[o] else 0
        self.ordtab = list(m[o + 2:o + 2 + 128])  # 128 entries (V3.02's 16CH format: 256)
        npat = max(self.ordtab) + 1
        o += 2 + 128 + 4                          # (+4: the signature at 0x438)
        self.stride = 6 * self.nch                # [0x260414]
        self.events = []                          # 6-byte events: ins, -, period, effect << 8 | param
        for i in range(npat * 64 * self.nch):     # 0x26e31b
            b0, b1, b2, b3 = m[o:o + 4]
            self.events.append(((b0 & 0xf0) | (b2 >> 4), ((b0 & 0xf) << 8) | b1, ((b2 & 0xf) << 8) | b3))
            o += 4
        self.npat = npat
        # upload (0x26e36f..0x26e3fc): mix control 0xb, then per sample DMA + a 16-byte tail by PIO
        c.port(BASE, 0xb)
        edi = 0
        self.uploads = []
        for n in range(1, self.nsmp + 1):
            s = self.smp[n]
            s.dram = edi
            if s.len:
                if edi >> 18 != (edi + s.len) >> 18:   # a sample must not cross a 256 KB bank
                    edi = ((edi + s.len) >> 18) << 18
                    s.dram = edi
                edi = self.upload(o, edi, s.len)
                tail = s.le if 0 < s.le <= 16 else 16
                edi = self.upload(o + s.ls, edi, tail)  # the bytes from the loop start, after the sample
                o += s.len
                if s.flag == 0:
                    s.len -= 1                    # 0x26e3cc `dec`: the end address is the last byte
                    s.le = s.len
            s.lerel = s.le
            s.len += s.dram
            s.le += s.dram
            s.ls += s.dram
            edi = (edi + 0x1f) & ~0x1f
        self.dram_end = edi                       # V3.03: [0x26fd65] = [0x26fd69] (where 0x26fca2 adds samples)
        # V3.03 0x26e402: channels = module + extra; voices = 2 x channels if more than 7
        self.allch = (self.extra + self.nch) & 0xff
        self.voices = 2 * self.allch if self.allch > 7 else 14
        # 0x26e8ba: period -> FC table (index 55..4000)
        div = self.img.u16(A_DIV + 2 * (self.voices - 14))
        num = self.img.u32(A_FCNUM)
        self.freq = [0] * 4001
        for p in range(55, 4001):
            q, r = divmod(num // p + div, div)
            q += (2 * r) >> 32                    # add edx, edx; adc eax, 0: never carries
            self.freq[p] = q & ~1
        self.ch = [Chan(i) for i in range(32)]
        self.patptr = [p * 64 * self.stride for p in range(128)]
        self.row = 0                              # [0x26042c]
        self.order = 0                            # [0x26042d]
        self.counter = 1                          # [0x260431]: ticks left in the row
        self.speed = 6                            # [0x260432]
        self.loopcnt = 0                          # [0x260435]
        self.looprow = self.looporder = self.loopptr = 0   # [0x260436], [0x260437], [0x260438]
        self.tempo(125)                           # 0x26e4aa: timer 1, count 6 (only stored)
        self.ptr = self.patptr[self.ordtab[0]]    # [0x260418]: current row's events

    def upload(self, src, edi, n):
        """0x26e9bb (ESI source, EDI DRAM address, ECX bytes): PIO 0x26ea91 for up to 20 bytes, else DMA 0x26eac8
        straight from the module's memory (only sources at or above [0x260424] = 16 MB go through the bounce
        buffer), in blocks that do not cross a 64 KB page of the source's physical address: at a page end the block
        is split, the DRAM address is brought to a multiple of 32 by PIO-ing up to 31 bytes, and the rest goes by
        DMA again. Each DMA block starts at the DRAM address rounded up to 32 bytes. Returns the DRAM address after
        the data. self.base: the module's address (0x345e30 in the headless run)."""
        c = self.c

        def pio(s, d, k):                         # regs 0x43 / 0x44, port 3X7 (not logged)
            c.dram[d:d + k] = self.mod[s:s + k]
            return d + k

        if n <= 0x14:
            return pio(src, edi, n)
        while n:
            phys = self.base + src
            k = min(n, 0x10000 - (phys & 0xffff))  # to the end of the source's 64 KB page
            a = (edi + 0x1f) >> 5 << 1             # DRAM address >> 4, rounded up to 32 bytes
            c.w16(0x42, a)
            c.w8(0x41, 0x21)                       # start, 8-bit channel, IRQ on completion
            d = a << 4
            c.dram[d:d + k] = self.mod[src:src + k]
            self.uploads.append((d, k))
            edi, src, n = d + k, src + k, n - k
            if n and edi & 0x1f:                   # split: realign the DRAM address by PIO
                j = min(32 - (edi & 0x1f), n)
                edi = pio(src, edi, j)
                src, n = src + j, n - j
        return edi

    def gf1_reset(self):
        """0x26ed0e (= V3.02 0x1a9ec)"""
        c = self.c
        c.w8(0x4c, 0)
        c.w8(0x4c, 0)
        c.w8(0x4c, 1)
        c.w8(0x41, 0)
        c.w8(0x45, 0)
        c.w8(0x49, 0)
        c.w8(0x0e, (self.voices - 1) | 0xc0)     # 16 voices: 617400 / 16 = 38,587.5 Hz
        for v in range(self.voices):
            c.select(v)
            c.w8(0x00, 3)
            c.w8(0x00, 3)
            c.w8(0x06, 0x3f)
            c.w8(0x06, 0x3f)
            c.w16(0x09, self.volt[0])
            c.w8(0x0d, 3)
            c.w8(0x0d, 3)
            c.w8(0x0c, self.pan[v])
            c.w8(0x0c, self.pan[v])
        c.w8(0x4c, 7)

    def start(self):
        """0x26ec3e (= V3.02 0x1a91c)"""
        c = self.c
        self.gf1_reset()
        c.port(BASE + 8, 4)
        c.port(BASE + 9, 0xe0)                    # AdLib timer control: reset, mask
        c.w8(0x4c, 7)
        c.port(BASE + 8, 4)
        c.port(BASE + 9, 3)                       # start timer 1 and timer 2 (t_s)
        c.w8(0x45, 0)
        c.w8(0x46, 6)                             # timer 1: (256 - 6) x 80 us = 20 ms (125 BPM)
        c.w8(0x45, 4)                             # timer 1 IRQ only
        c.port(BASE, 9)                           # mix control: line out on, latches on

    def stop(self):
        """0x26ecc4 (= V3.02 0x1a9a2)"""
        c = self.c
        c.port(BASE, 0xb)
        c.port(BASE, 0xb)
        c.port(BASE + 8, 4)
        c.port(BASE + 9, 0xe0)
        c.w8(0x45, 0)
        self.gf1_reset()

    def tempo(self, bpm):
        """0x26e4aa / 0x26f9c8: tick period 2.5 s / bpm in timer units, truncated twice"""
        n = 1000000 // (bpm * 32)                 # 80 us units (31250 / bpm)
        if n >> 8:                                # too long for timer 1: timer 2, 320 us units
            n >>= 2
            self.treg, self.tctl = 0x47, 8
        else:
            self.treg, self.tctl = 0x46, 4
        self.tcount = -n & 0xff                   # [0x26e00f] reg, [0x26e010] reg 0x45 value, [0x26e011] count

    # ------------------------------------------------------------------------------------------- helpers
    def snap(self, ax, ft):
        """period -> the finetune's table: first i with period >= table0[i]"""
        ax &= 0xffff
        i = 0
        while i < 36 and ax < self.pt[i]:
            i += 1
        return self.pt[ft * 36 + i]

    def fc_of(self, p):
        p = s32(p)
        assert 0 <= p <= 4000, 'period %d outside the FC table' % p
        return self.freq[p]

    def vol_update(self, ch):
        """0x26fb7c: ramp from the last sent volume to the channel volume (63 steps per frame)"""
        if ch.gvol != ch.vol:
            a, b = self.volt[ch.gvol], self.volt[ch.vol]
            if a < b:
                self.c.w16(7, a)
                self.c.w16(8, b)
                self.c.w8(0x0d, 0)
            else:
                self.c.w16(7, b)
                self.c.w16(8, a)
                self.c.w8(0x0d, 0x40)
            ch.gvol = ch.vol

    def fc_update(self, ch):
        """frequency, then the voice control (starts the voice)"""
        if ch.fc:
            fc, ch.fc = ch.fc, 0
            self.c.w16(1, fc)
            self.c.w8(0, ch.ctl)

    def note_start(self, ch):
        """0x26fa68 / 0x26f2e3: ramp the old voice down, switch to the other voice of the pair, set its position"""
        c = self.c
        c.w16(7, self.volt[0])
        c.w16(8, self.volt[ch.vol])
        c.w8(0x0d, 0x40)
        ch.gvol = 0
        ch.voice ^= 1
        c.select(ch.voice)
        if ch.newsmp:
            ch.newsmp -= 1
            s = ch.smp
            if ch.ctl:
                c.w16(2, s.ls >> 7)
                c.w16(3, s.ls << 9)
            c.w16(4, s.le >> 7)
            c.w16(5, s.le << 9)
        c.w16(0x0b, ch.start << 9)
        c.w16(0x0b, ch.start << 9)
        c.w16(0x0a, ch.start >> 7)
        c.w16(0x0a, ch.start >> 7)

    def volslide(self, ch, p):
        if not p & 0xf:
            ch.vol = (ch.vol + (p >> 4)) & 0xff
            if ch.vol > self.maxvol:
                ch.vol = self.maxvol
        else:
            v = (ch.vol - (p & 0xf)) & 0xff
            ch.vol = 0 if v & 0x80 else v

    def toneporta(self, ch):
        spd, a, t = ch.pspeed, ch.period, ch.target
        if a > t:
            a = (a - spd) & 0xffffffff
            if a & 0x80000000:
                ch.period, ch.target = t, 0
                return
            ch.period = a
            if s32(a) > s32(t):
                return self.gliss(ch)
        elif a < t:
            a = (a + spd) & 0xffffffff
            ch.period = a
            if s32(a) < s32(t):
                return self.gliss(ch)
        ch.period, ch.target = t, 0

    def gliss(self, ch):
        """(E3x on): the period is rounded to the semitone table in place"""
        if ch.gliss:
            ax = ch.period & 0xffff
            i = 0
            while i < 36 and ax < self.pt[i]:
                i += 1
            ch.period = self.pt[ch.ft * 36 + i]

    def vibrato(self, ch):
        """returns period + vibrato (the period itself is not changed)"""
        pos = ch.vibpos
        idx = (pos >> 2) & 0x1f
        w = ch.wave & 3
        if w == 0:
            m = self.sine[idx]
        elif w == 1:
            m = (idx << 3) & 0xff
            if pos & 0x80:
                m = -m & 0xff
        else:
            m = 0xff
        d = ((ch.vib & 0xf) * m) >> 7
        if pos & 0x80:
            d = -d
        ch.vibpos = (pos + ((ch.vib >> 2) & 0x3c)) & 0xff
        return (ch.period + d) & 0xffffffff

    @staticmethod
    def nibbles(old, p):
        if p & 0xf:
            old = (old & 0xf0) | (p & 0xf)
        if p & 0xf0:
            old = (old & 0x0f) | (p & 0xf0)
        return old

    # ------------------------------------------------------------------------------------------- the tick
    def tick(self):
        """0x26eecc: GUS timer IRQ. V3.03: counter 0 -> row; 0xff -> stuck; anything else -> tick effects (V3.02:
        `jle` -> row unless negative -> stuck)"""
        c = self.c
        c.w8(0x45, 0)
        c.w8(0x45, self.tctl)                     # acknowledge, re-enable
        self.counter = (self.counter - 1) & 0xff
        if self.counter == 0:
            self.row_tick()
        elif self.counter == 0xff:                # 0x26fc89: stopped (after F00)
            self.counter = 0
        else:
            for ch in self.ch[:self.allch]:
                self.tick_effect(ch)
        self.positions.append((self.order, self.row))

    def tick_effect(self, ch):
        """0x26ef18: one channel on a tick between rows (table 0x26ef34)"""
        c = self.c
        e = ch.eff
        if not e:
            return
        p, x = e & 0xff, e >> 8
        if x == 0:                                # arpeggio: counter 0 -> +y, 1 -> +x, 2 -> base
            a = ch.arp
            ch.arp = (a + 1) & 0xff
            if a >= 2:
                ch.arp = 0
                return self.out_freq(ch, ch.period)
            n = (p & 0xf) if a == 0 else (p >> 4)
            b = ch.ft * 36
            dx = ch.period & 0xffff
            for i in range(36):
                if dx >= self.pt[b + i]:
                    return self.out_freq(ch, self.pt[b + i + n])
            return self.out_freq(ch, self.pt[b + 35 + n])
        if x in (1, 2):                           # portamento (no limits)
            ch.period = (ch.period + (-p if x == 1 else p)) & 0xffffffff
            return self.out_freq(ch, ch.period)
        if x == 3:                                # tone portamento
            if not ch.target:
                return
            self.toneporta(ch)
            return self.out_freq(ch, ch.period)
        if x == 4:                                # vibrato
            if p:
                ch.vib = self.nibbles(ch.vib, p)
            return self.out_freq(ch, self.vibrato(ch))
        if x == 5:                                # tone portamento + volume slide
            if not ch.target:
                self.volslide(ch, p)
                return self.out_vol(ch)
            self.toneporta(ch)
            self.volslide(ch, p)
            return self.out_freq(ch, ch.period)
        if x == 6:                                # vibrato + volume slide
            a = self.vibrato(ch)
            self.volslide(ch, p)
            return self.out_freq(ch, a)
        if x == 7:                                # tremolo
            if p:
                ch.trem = self.nibbles(ch.trem, p)
            pos = ch.trempos
            idx = (pos >> 2) & 0x1f
            w = (ch.wave >> 4) & 3
            if w == 0:
                m = self.sine[idx]
            elif w == 1:
                m = (idx << 3) & 0xff
                if pos & 0x80:
                    m = -m & 0xff
            else:
                m = 0xff
            d = ((ch.trem & 0xf) * m) >> 6
            if pos & 0x80:
                d = -d
            v = (d + ch.tbase) & 0xff
            ch.vol = 0 if v & 0x80 else min(v, self.maxvol)
            ch.trempos = (pos + ((ch.trem >> 2) & 0x3c)) & 0xff
            return self.out_vol(ch)
        if x == 9:                                # back to the sample start for later notes
            ch.start = ch.smp.dram
            ch.eff = 0
            return
        if x == 0xa:                              # volume slide
            self.volslide(ch, p)
            return self.out_vol(ch)
        if x == 0xe:                              # 0x26f214 (table 0x26f224)
            y, v = p >> 4, p & 0xf
            if y == 9:                            # retrigger
                ch.retrig = (ch.retrig - 1) & 0xff
                if ch.retrig:
                    return
                ch.retrig = v
                return self.retrigger(ch)
            if y == 0xc:                          # note cut, when the row's tick counter equals x
                if self.counter != v:
                    return
                ch.vol = 0
                c.select(ch.voice)
                c.w16(7, self.volt[0])
                c.w16(8, self.volt[ch.vol])
                c.w8(0x0d, 0x40)
                ch.gvol = 0
                return
            if y == 0xd:                          # note delay, when the tick counter equals x
                if self.counter != v:
                    return
                return self.retrigger(ch)
            if y == 0xf:                          # V3.03 0x26f484: the effect is cleared and the note restarted
                ch.eff = 0                        # (the sound-effect call 0x26034e sets EF0 on its channel)
                return self.retrigger(ch)
        return                                    # 8, B, C, D, F and the other E commands: nothing

    def out_freq(self, ch, per):
        """0x26f496: FC of a period, then 0x26f4a0"""
        ch.fc = self.fc_of(per)
        self.out_vol(ch)

    def out_vol(self, ch):
        """0x26f4a0: select the channel's voice, volume ramp, pending frequency"""
        self.c.select(ch.voice)
        self.vol_update(ch)
        self.fc_update(ch)

    def retrigger(self, ch):
        """0x26f2e3"""
        self.c.select(ch.voice)
        self.note_start(ch)
        a = ch.period
        if ch.ft:
            a = ch.period = self.snap(a, ch.ft)
        ch.fc = self.fc_of(a)
        self.vol_update(ch)
        self.fc_update(ch)

    def row_tick(self):
        """0x26f544: read a row (the module's channels only, [0x26040b])"""
        c = self.c
        self.counter = self.speed
        edi = self.ptr
        for ch in self.ch[:self.nch]:
            c.select(ch.voice)
            k = edi // 6
            assert 0 <= k < len(self.events) and edi % 6 == 0
            ins, per, eff = self.events[k]
            if ins:
                assert ins <= 31
                s = self.smp[ins]
                ch.ins, ch.smp = ins, s
                ch.vol = min(s.vol, self.maxvol)
                ch.ft, ch.ctl, ch.start = s.ft, s.flag, s.dram
                ch.newsmp = 2
            self.row_effect(ch, per, eff)
            edi += 6
        self.row = (self.row + 1) & 0xff
        if self.row >= 0x40:
            self.row = 0
            self.order = (self.order + 1) & 0xff
            if self.order >= self.songlen:
                self.order = self.restart         # V3.03: the song goes on from the restart order (V3.02 stopped)
            self.ptr = self.patptr[self.ordtab[self.order]]
        else:
            self.ptr += self.stride

    def row_effect(self, ch, a, e):
        """effect table 0x26f5cc, then 0x26fa5c (note start), 0x26fb44 (period), 0x26fb7c (volume, frequency)"""
        x, p = e >> 8, e & 0xff
        if x == 0:
            ch.arp = 0
        elif x in (3, 5):                         # tone portamento: the note is the target
            if a:
                if ch.ft:
                    a = self.snap(a, ch.ft)
                ch.target = a
                a = 0
            if x == 3 and p:
                ch.pspeed = p
        elif x == 7:
            ch.tbase = ch.vol
        elif x == 9:                              # sample offset (bytes = param << 8)
            off = p << 8
            if ch.smp.lerel >= off:
                ch.start = ch.smp.dram + off
            else:
                e = 0
        elif x == 0xb:                            # position jump
            self.row = 0x3f
            self.order = (p - 1) & 0xff
            e = 0
        elif x == 0xc:
            ch.vol = min(p, self.maxvol)
            e = 0
        elif x == 0xd:                            # pattern break (BCD row)
            v = ((p >> 4) * 10 + (p & 0xf)) & 0xff
            if p == 0 or v > 0x3f:
                self.row = 0x3f
            else:
                self.row = (v - 1) & 0xff
                self.order = (self.order + 1) & 0xff
                if self.order >= self.songlen:
                    self.order = self.restart
                self.ptr = self.patptr[self.ordtab[self.order]] + self.stride * self.row
            e = 0
        elif x == 0xe:                            # table 0x26f790
            y, v = p >> 4, p & 0xf
            if y in (1, 2):                       # fine portamento
                if not a:
                    ch.eff = 0
                    a = (ch.period - v) if y == 1 else (ch.period + v)
                    return self.row_period(ch, a & 0xffffffff)
                if ch.ft:
                    a = self.snap(a, ch.ft)
                a = (a - v) if y == 1 else (a + v)
                e = 0
            elif y == 3:
                ch.gliss = v & 1
                e = 0
            elif y == 4:
                ch.wave = (ch.wave & 0xf0) | v
                e = 0
            elif y == 5:
                ch.ft = v
                e = 0
            elif y == 6:                          # pattern loop
                if v == 0:
                    self.looporder = self.order
                    r = (self.row - 1) & 0xff
                    if r & 0x80:
                        r = 0x3f
                        self.looporder = (self.looporder - 1) & 0xff
                    self.looprow = r
                    self.loopptr = self.ptr - self.stride
                elif self.loopcnt >= v:
                    self.loopcnt = 0
                else:
                    self.row, self.order, self.ptr = self.looprow, self.looporder, self.loopptr
                    self.loopcnt += 1
                e = 0
            elif y == 7:
                ch.wave = (ch.wave & 0xf) | ((p << 4) & 0xff)
                e = 0
            elif y == 9:
                ch.retrig = v
            elif y == 0xa:
                ch.vol = min((ch.vol + v) & 0xff, self.maxvol)
                e = 0
            elif y == 0xb:
                vv = (ch.vol - v) & 0xff
                ch.vol = 0 if vv & 0x80 else vv
                e = 0
            elif y == 0xd:                        # note delay: nothing now
                ch.period = a
                ch.eff = e
                return
            elif y == 0xe:                        # pattern delay: counter = x * speed
                self.counter = (v * self.speed) & 0xff
                e = 0
        elif x == 0xf:
            if p >= 0x20:
                self.tempo(p)
                self.c.w8(0x45, 0)
                self.c.w8(self.treg, self.tcount)
                self.c.w8(0x45, self.tctl)
            else:
                self.speed = self.counter = p
            e = 0
        # 0x26fa5c
        ch.eff = e & 0xffff
        if not a:
            return self.row_volume(ch)
        self.note_start(ch)
        self.row_period(ch, a)

    def row_period(self, ch, a):
        if ch.ft:
            a = self.snap(a, ch.ft)
        ch.period = a
        ch.fc = self.fc_of(a)
        self.row_volume(ch)

    def row_volume(self, ch):
        self.vol_update(ch)
        self.fc_update(ch)


# ------------------------------------------------------------------------------------------------ GF1 model
class GF1:
    """What the card does with the writes (STARS's model: stars/re/models/music/lcaplay.py class GF1, see
    paper/re/notes-music.md section 7): positions in 1/512 byte, increment FC >> 1 per frame, 12-bit log volume
    in 1/512 steps with ramps, pan 0..15 (linear law), 617400 / active frames per second, 8-bit samples linearly
    interpolated, stopped voices mixed at their parked position, forward loops."""

    def __init__(self, dram):
        import numpy as np
        self.np = np
        self.dram = dram
        self.dram_f = np.frombuffer(bytes(dram) + b'\0', dtype=np.int8).astype(np.float64) / 128.0
        e = np.arange(4096) >> 8
        m = np.arange(4096) & 255
        self.gain = (256 + m) * np.exp2(e - 24.0)
        self.latch = 0
        self.reset()

    def reset(self):
        self.active = 14
        self.dac = False
        self.v = [{'ctl': 3, 'fc': 0, 'start': 0, 'end': 0, 'pos': 0, 'vctl': 3, 'rate': 0, 'lo': 0, 'hi': 0,
                   'vol': 0, 'pan': 7} for _ in range(32)]

    def write(self, voice, reg, val, size):
        if size == 1:
            d = ((val & 0xff) << 8) | self.latch
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

    def render(self, n):
        np = self.np
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
        return out if self.dac else out * 0

    def voice_frames(self, s, n):
        np = self.np
        ctl, p0, inc = s['ctl'], s['pos'], s['fc'] >> 1
        st, en = s['start'], s['end']
        if ctl & 3 or inc == 0:
            pos = np.full(n, p0, dtype=np.int64)
        else:
            assert not ctl & 0x50, 'backward / bidirectional voices are not used by this player'
            Lp = en - st
            pp = p0 + np.arange(n + 1, dtype=np.int64) * inc
            over = pp > en
            if over.any():
                if ctl & 8 and Lp > 0:
                    pp[over] = st + (pp[over] - en) % Lp
                else:
                    pp[over] = en
            pos = pp[:n]
            p = p0 + n * inc
            if p > en:
                if ctl & 8 and Lp > 0:
                    p = st + (p - en) % Lp
                else:
                    p = en
                    s['ctl'] = ctl | 1
            s['pos'] = p
        v0 = s['vol']
        inc = ((s['rate'] & 63) * 512) >> (3 * (s['rate'] >> 6))
        if not s['vctl'] & 3 and inc:
            if s['vctl'] & 0x40:
                vol = np.maximum(v0 - np.arange(n, dtype=np.int64) * inc, s['lo'])
                v = v0 - n * inc
                if v <= s['lo']:
                    v = s['lo']
                    s['vctl'] |= 1
            else:
                vol = np.minimum(v0 + np.arange(n, dtype=np.int64) * inc, s['hi'])
                v = v0 + n * inc
                if v >= s['hi']:
                    v = s['hi']
                    s['vctl'] |= 1
            vol[0] = v0
            s['vol'] = v
        else:
            vol = np.full(n, v0, dtype=np.int64)
        return pos, np.clip(vol, 0, (4096 << 9) - 1)


# ------------------------------------------------------------------------------------------------ timing
class Timers:
    """The two GUS timers as run_reve.py (and DOSBox) clock them, in integer microseconds after the start (AdLib
    register 4 = 3 in 0x26ec3e): a started timer overflows every (256 - count) steps (timer 1: 80 us, timer 2:
    320 us) and reloads the count register as it is at that moment, so a count written by a tick takes effect one
    period later. Both run from the start, whether their IRQ is enabled or not; an overflow raises the GUS IRQ (a
    player tick) when reg 0x45 enables that timer (bit 2 timer 1, bit 3 timer 2). This song switches between
    them: F50 (80 BPM) -> timer 2, F96 (150 BPM) -> timer 1, so after a switch the first tick comes at the next
    overflow of the free-running other timer."""

    UNIT = (80, 320)

    def __init__(self):
        self.now = 0
        self.nxt = [None, None]
        self.count = [0, 0]
        self.enable = 0

    def write(self, reg, val):
        if reg == 0x45:
            self.enable = val & 0xff
        elif reg == 0x46:
            self.count[0] = val & 0xff
        elif reg == 0x47:
            self.count[1] = val & 0xff

    def control(self, val):
        if val & 0x80:
            return                               # reset the IRQ flags
        for i in range(2):
            if not val & (1 << i):
                self.nxt[i] = None
            elif self.nxt[i] is None:
                self.nxt[i] = self.now + (256 - self.count[i]) * self.UNIT[i]

    def next_irq(self):
        """time (us) of the next counted overflow; also returns which timer and the overflows passed on the way"""
        while True:
            c = [i for i in range(2) if self.nxt[i] is not None]
            if not c or not self.enable & 0x0c:
                return None
            i = min(c, key=lambda j: (self.nxt[j], j))
            t = self.nxt[i]
            self.now = t
            self.nxt[i] = t + (256 - self.count[i]) * self.UNIT[i]
            if self.enable & (4 << i):
                for j in range(2):                # the other timer overflowing at the same instant does so before
                    if self.nxt[j] == t:          # the handler can change reg 0x45
                        self.nxt[j] = t + (256 - self.count[j]) * self.UNIT[j]
                return t


def last_end_position(img):
    """the script table 0x205ed0 (entries of 0x14 bytes, ended by a zero dword): the last entry's end position"""
    a, end = 0x205ed0, None
    while img.u32(a):
        end = img.u32(a + 0x10)
        a += 0x14
    return end


def first_retrace_after(start_refresh, t_us):
    """the first refresh whose retrace comes after music time t (us): retrace k is at (k - start_refresh) / VGA_HZ"""
    return math.floor(start_refresh + t_us * 1e-6 * VGA_HZ) + 1


def run(img, mod, ticks=None, wav=False, start_pit=START_PIT):
    """detect, convert, start, the timer IRQs, stop. The stop comes, as main makes it, STOP_AFTER_LAST_ENTRY
    refreshes after the retrace at which the timer interrupt sees the last script entry's end position (or before
    tick `ticks` if given). Returns the player, the card, the tick times (us after the timer start), the stop time,
    the end refresh, and with wav the GF1 model and the rendered blocks."""
    card = Card()
    timers = Timers()
    pl = Player(img, mod, card)
    pl.detect()
    pl.convert()
    hw = None
    if wav:
        hw = GF1(card.dram)
        card.hw = hw
    card.timers = timers
    out, frame = [], 0
    pl.start()
    times = []
    start_refresh = start_pit / FRAME - VS_LINE
    last_end = last_end_position(img)
    stop_us = end_refresh = None
    k = 0
    while True:
        t = timers.next_irq()
        if ticks is not None and k == ticks:
            stop_us = t
            break
        if stop_us is not None and t > stop_us:
            break
        times.append(t)
        if hw is not None:
            f = int(-(-t * 1e-6 * hw.frame_rate() // 1))   # frames that start before the tick
            if f > frame:
                out.append(hw.render(f - frame))
                frame = f
        card.tick = k + 1
        n0 = len(card.lines)
        pl.tick()
        if hw is not None:
            del card.lines[n0:]
        if stop_us is None and ticks is None and (pl.order << 8 | pl.row) >= last_end:
            end_refresh = first_retrace_after(start_refresh, t) + STOP_AFTER_LAST_ENTRY
            stop_us = math.floor((end_refresh - start_refresh) / VGA_HZ * 1e6)
        k += 1
    card.tick = len(times)
    if hw is not None:
        f = int(-(-stop_us * 1e-6 * hw.frame_rate() // 1))
        if f > frame:
            out.append(hw.render(f - frame))
    pl.stop()
    return pl, card, times, stop_us, end_refresh, hw, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--img', default=os.path.join(RE, 'unpacked', 'reve_img.bin'))
    ap.add_argument('--module', default=os.path.join(RE, 'unpacked', 'files', 'ZIC', 'PULSE.MOD'))
    ap.add_argument('--ticks', type=int, default=None, help='stop before this player tick instead of where main '
                    'stops the music (for a partial capture)')
    ap.add_argument('--check', default=RE, help='folder with full.gus / full.dram / full.ticks ("" = no check)')
    ap.add_argument('--prefix', default='full', help='capture name in --check (full)')
    ap.add_argument('--start', type=float, default=START_PIT, help='PIT tick (program time x 1,193,182) at '
                    'which 0x26ec3e starts the GUS timers in the capture (run_reve.py --music-log)')
    ap.add_argument('--out', help='write PREFIX.gus (log) and PREFIX.dram')
    ap.add_argument('--wav', help='render through the GF1 model (needs numpy)')
    ap.add_argument('--times', help='write the tick times (s after the timer start), order and row per tick')
    ap.add_argument('--info', action='store_true', help='print the module, the DRAM layout and the effects used')
    a = ap.parse_args()

    img = Img(a.img)
    mod = open(a.module, 'rb').read()
    if a.info:
        card = Card()
        pl = Player(img, mod, card)
        pl.detect()
        pl.convert()
        return info(pl)
    ref_ticks = []
    if a.check:
        for ln in open(os.path.join(a.check, a.prefix + '.ticks')):
            if not ln.startswith('#'):
                f = ln.split()
                ref_ticks.append((int(f[0]), float(f[1]), int(f[3]), int(f[4]), int(f[5])))
    pl, card, times, stop_us, end_refresh, hw, out = run(img, mod, a.ticks, bool(a.wav), a.start)
    ticks = len(times)
    start_refresh = a.start / FRAME - VS_LINE
    print('%d ticks; timer start at refresh %.10f; stop at %.6f s after it%s' % (
        ticks, start_refresh, stop_us * 1e-6, '' if end_refresh is None else ', refresh %d' % end_refresh))

    if a.out:
        with open(a.out + '.gus', 'w') as f:
            f.write('\n'.join(card.lines) + '\n')
        open(a.out + '.dram', 'wb').write(card.dram)
    if a.times:
        with open(a.times, 'w') as f:
            f.write('# tick, time in s after the timer start (0x26ec3e), order and row after the tick\n')
            for k in range(ticks):
                f.write('%d %.6f %02x %02x\n' % (k, times[k] / 1e6, pl.positions[k][0], pl.positions[k][1]))

    if a.check and not a.wav:
        ok = True
        p = os.path.join(a.check, a.prefix + '.dram')
        if os.path.exists(p):
            ref = open(p, 'rb').read()
            nd = sum(1 for x, y in zip(ref, card.dram) if x != y)
            print('DRAM: %d of %d bytes differ' % (nd, len(ref)))
            ok &= nd == 0
        else:
            print('DRAM: no %s' % p)
        ref = open(os.path.join(a.check, a.prefix + '.gus')).read().split('\n')
        if ref and ref[-1] == '':
            ref.pop()
        mine = card.lines
        nd = next((i for i, (x, y) in enumerate(zip(ref, mine)) if x != y), None)
        if nd is None and len(ref) == len(mine):
            print('GUS log: %d lines, identical' % len(ref))
        else:
            ok = False
            i = nd if nd is not None else min(len(ref), len(mine))
            print('GUS log: %d lines (capture %d); first difference at line %d:' % (len(mine), len(ref), i + 1))
            for j in range(max(0, i - 8), min(i + 8, max(len(ref), len(mine)))):
                print('  %-28s %s' % (ref[j] if j < len(ref) else '-', mine[j] if j < len(mine) else '-'))
        # per capture record: GUS interrupts before the retrace (the emulator's float clock: start + tick times)
        # and the position after them. The emulator delivers an interrupt only between its slices: a tick whose
        # overflow came while the demo's timer interrupt waited for this retrace has not run yet when the record is
        # taken ("late": the record shows the position one tick earlier; hardware runs the tick right after the timer
        # interrupt's sti, before it reads the position).
        tpit = [a.start + t * 1e-6 * PIT_HZ for t in times]
        bad_n = bad_p = late = n = 0
        k = 0
        first = None
        for fr, ts, g, o, r in ref_ticks:
            vs = (fr + VS_LINE) * FRAME
            while k < len(tpit) and tpit[k] < vs:
                k += 1
            n += 1
            if k != g:
                bad_n += 1
                if first is None:
                    first = (fr, k, g)
            want = pl.positions[k - 1] if 0 < k <= len(pl.positions) else (0, 0)
            if want != (o, r):
                before = pl.positions[k - 2] if k >= 2 else (0, 0)
                if before == (o, r):
                    late += 1
                    print('  record %d: late (%d interrupts, capture %02x:%02x, model %02x:%02x)' % (
                        (fr, g, o, r) + want))
                else:
                    bad_p += 1
                    if bad_p <= 5:
                        print('  record %d: %d interrupts, capture %02x:%02x, model %02x:%02x'
                              % ((fr, g, o, r) + want))
        print('timer interrupts before the retrace: %d records, %d differ%s' % (n, bad_n, '' if first is None else
                                                                              ' (first: record %d, model %d, capture %d)'
                                                                              % first))
        print('music position at the retrace: %d records, %d identical, %d late in the capture, %d other differences'
              % (n, n - late - bad_p, late, bad_p))
        ok &= bad_n == 0 and bad_p == 0
        print('ALL MATCH' if ok else 'MISMATCH')

    if a.wav:
        import numpy as np
        import wave
        pcm = np.concatenate(out)
        rate = hw.frame_rate()
        peak = float(np.abs(pcm).max())
        print('rendered %d frames at %.2f Hz (%.2f s from the timer start); peak %.3f (1.0 = one voice at full scale '
              'and gain 1, hard panned)' % (len(pcm), rate, len(pcm) / rate, peak))
        with wave.open(a.wav, 'wb') as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(int(round(rate)))
            w.writeframes((np.clip(pcm / max(peak, 1e-9) * 0.9, -1, 1) * 32767).astype('<i2').tobytes())


def info(pl):
    import collections
    print('%d channels, %d GF1 voices (%.2f Hz), %d patterns, song length %d, restart %d' % (
        pl.nch, pl.voices, GF1_CLOCK / pl.voices, pl.npat, pl.songlen, pl.restart))
    print('orders: ' + ' '.join('%d' % x for x in pl.ordtab[:pl.songlen]))
    print('smp  DRAM start  end/last  loop start  loop end  ctl  ft  vol  9xx limit')
    for n in range(1, pl.nsmp + 1):
        s = pl.smp[n]
        print('%3d  %8x  %8x  %8x  %8x  %3d  %2d  %3d  %6d' % (n, s.dram, s.len, s.ls, s.le, s.flag, s.ft, s.vol,
                                                               s.lerel))
    print('uploads (DRAM, bytes): ' + ' '.join('%x:%x' % u for u in pl.uploads))
    print('end of the samples in DRAM: %x' % pl.dram_end)
    used = collections.Counter()
    for o in range(pl.songlen):
        p = pl.ordtab[o]
        for k in range(p * 64 * pl.nch, (p + 1) * 64 * pl.nch):
            ins, per, eff = pl.events[k]
            x, y = eff >> 8, eff & 0xff
            key = (x, y if x in (0xb, 0xd, 0xf) else (y >> 4 if x == 0xe else None))
            if eff:
                used[key] += 1
    print('effects in the song (by order): ' + ', '.join(
        '%X%s x%d' % (x, ('%02x' % y if x != 0xe else '%x' % y) if y is not None else '', n) for (x, y), n in sorted(
            used.items(), key=lambda kv: (kv[0][0], -1 if kv[0][1] is None else kv[0][1]))))


if __name__ == '__main__':
    main()

"""Reference implementation of STARS.EXE's music: LCA's MOD-Player V3.02, GUS path, mirroring the assembly
routine by routine (see ../../notes-music.md), plus a GF1 model to render it.

From the module (overlay file 1, unpacked/files/01.bin) and stars_img.bin (the player's tables) it produces
  - the GUS DRAM image (the sample upload 0x1a3ff / 0x1a699)                -> compare with full.dram
  - the GF1 register-write log, in run_stars.py's text format, from the
    card detection to the stop (0x1ba6c, 0x1a1b0, 0x1a91c, ticks, 0x1a9a2)  -> compare with full.gus
  - the music position (order, row) after every player tick                -> compare with full.ticks
and with --wav renders the song through a GF1 model (32 active voices: 19,293.75 frames per second), with the
player ticks placed on the GUS timer as the card clocks them (timer 1, then timer 2 in 320 us steps).

usage:  python3 lcaplay.py [--check DIR | --check ""] [--ticks N] [--out PREFIX] [--times FILE] [--info]
                          [--wav out.wav [--stop SECONDS]]
        (run inside the toolbox from this folder; reads ../../unpacked and ../../full.*)

The player reads no GUS register that matters (only the status registers 0x41 / 0x49 / 0x8f and port 2X6 as
acknowledgements), so the register stream does not depend on the card's answers: the same stream drives real
hardware and the capture's fake card.

Routine map (image offsets):
  0x1ba6c detect (0x1bb0a GUS: 0x1bba7 ULTRASND=, 0x1bceb init + IRQ/DMA latches; 0x1bc2d ULTRA16=;
          0x1bfda SB probe)          0x1c71b load -> 0x1a1b0 convert + upload    0x1c759 start -> 0x1a91c
  0x1c77e stop -> 0x1a9a2            0x1a9ec GF1 reset                          0x1a594 period -> FC table
  0x1a699 / 0x1a76f / 0x1a7a6 upload (split, PIO, DMA)   0x1a8e2 DMA IRQ handler   0x1a5f7 / 0x1a66c IRQ vector
  0x1abb0 timer IRQ = player tick: 0x1abea tick effects (table 0x1ac04, E 0x1aef4), 0x1b210 row (table 0x1b298,
          E 0x1b45c), 0x1b728 note start, 0x1b848 / 0x1b164 volume ramp + frequency write
  0x1c7a3 position (AL order, AH row), 0x29f80 = AH order, AL row
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))

GF1_CLOCK = 617400.0          # GF1 voice frames per second * active voices (1 / 1.619695497 us)
BASE = 0x240                  # ULTRASND=240,3,3,7,7 (the capture's environment)
DMA_CH, IRQ_NO = 3, 7

# addresses in the program image
A_PTAB = 0x296c8              # 16 finetunes x 36 periods (u16), finetune 0 first; 8..15 = -8..-1
A_SINE = 0x29b48              # 32 bytes: vibrato / tremolo sine (half wave)
A_VOLT = 0x29bdc              # 65 u16: channel volume 0..64 -> GF1 volume (8-bit regs use the high byte)
A_PAN = 0x29bbb               # 32 bytes: pan of voice 0..31
A_DIV = 0x29c5e               # u16 per voice count 14..32: 617400 / voices (truncated)
A_IRQLATCH, A_DMALATCH = 0x29ba3, 0x29bb3
FC_NUM = 0xda7a69e2           # 3579545.5 Hz (NTSC Amiga clock) * 1024


def s32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


class Img:
    def __init__(self, path):
        self.b = open(path, 'rb').read()

    def u8(self, a): return self.b[a]
    def u16(self, a): return struct.unpack_from('<H', self.b, a)[0]


class Card:
    """The GUS as run_stars.py's Gus class logs it: '<tick> v<voice> r<reg> <val>' for voice registers (< 0x40,
    the value as written: byte for 8-bit writes, word for 16-bit writes), '<tick> g r<reg> <val>' for global
    registers (>= 0x40, except the DRAM address 0x43 / 0x44), '<tick> p<port> <val>' for other ports in
    0x240..0x34f (the voice select 0x342, the register select 0x343 and DRAM data 0x347 are not logged).
    The DRAM is written by DMA (reg 0x41 bit 0) and by PIO (0x43 / 0x44 + 0x347)."""

    def __init__(self):
        self.lines = []
        self.tick = 0
        self.voice = 0
        self.dram = bytearray(1 << 20)
        self.hw = None                # optional GF1 model: hw.write(voice, reg, value, size)

    def port(self, p, val):
        if 0x240 <= p < 0x350:
            self.lines.append('%d p%03x %x' % (self.tick, p, val))
        if self.hw is not None:
            self.hw.port(p, val)

    def select(self, v):
        self.voice = v & 31

    def _w(self, reg, val, size):
        if reg in (0x43, 0x44):
            return
        if reg >= 0x40:
            self.lines.append('%d g r%02x %x' % (self.tick, reg, val))
        else:
            self.lines.append('%d v%d r%02x %x' % (self.tick, self.voice, reg, val))
        if self.hw is not None:
            self.hw.write(self.voice, reg, val, size)

    def w8(self, reg, val):          # reg -> 3X3, byte -> 3X5
        self._w(reg, val & 0xff, 1)

    def w16(self, reg, val):         # reg -> 3X3, word -> 3X4
        self._w(reg, val & 0xffff, 2)


class Sample:
    """record of 0x18 bytes at 0x287c8 + 0x18 * (n - 1) (pointer table 0x28ac4 + 4 n)"""

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
    """channel record, 0x34 bytes at 0x29048 + 0x34 * ch (offsets in the comments)"""

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
        self.voices = 14            # [0x29b8e]
        self.nch = 4                # [0x1c807]
        self.nsmp = 31              # [0x1c806]
        self.maxvol = 0x40          # [0x1c808]
        self.positions = []

    # ------------------------------------------------------------------------------------------- start-up
    def detect(self):
        """0x1ba6c: GUS (0x1bb0a), then the SB probe (0x1bfda), which finds nothing; [0x1c802] = 0x20."""
        c = self.c
        # 0x1bba7: ULTRASND= -> port 0x240, DMA 3, DMA2 3, IRQ 7, IRQ2 7 (0x29b68 / 82 / 84 / 86 / 88)
        # 0x1bceb: hard init
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
        # IRQ / DMA latches (0x1beec): IRQ 7 -> 4, same IRQ for both -> | 0x40; DMA 3 -> 2, same -> | 0x40
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
        # 0x1bc2d: no ULTRA16= in the environment. 0x1bfda: SB reset at 0x210..0x280 (out 2X6 1, 0); no answer
        for p in range(0x210, 0x290, 0x10):
            c.port(p + 6, 1)
            c.port(p + 6, 0)

    def convert(self):
        """0x1a1b0 (EAX = -1, ESI = module, EDI = pattern buffer): channels, samples, orders, patterns, upload,
        period -> FC table, player state."""
        m, c = self.mod, self.c
        sig = m[0x4b8:0x4ba]
        al, ah = sig[0], sig[1]
        if al == 0x4d:                            # 'M.K.'
            al, ah = 0x30, 0x34
        if 0x30 <= ah <= 0x39:
            al, ah = ah, al
        assert 0x30 <= al <= 0x39                 # (other signatures: 0x1a23e, not this module)
        if not (0x30 <= ah < 0x39):
            ah = 0x30
        self.nch = ((ah - 0x30) * 10 + (al - 0x30)) & 0xff
        if self.nch > 7:
            self.voices = 2 * self.nch            # 16 channels -> 32 voices
        o = 0x14
        self.smp = [None]
        for i in range(self.nsmp):                # 0x1a2cf
            s = Sample()
            ln, ft, vol, ls, lr = struct.unpack_from('>HBBHH', m, o + 0x16)
            s.len, s.ft, s.vol, s.ls = ln * 2, ft, vol, ls * 2
            s.flag = 8 if lr * 2 > 2 else 0
            s.le = min(ls * 2 + lr * 2, s.len)
            self.smp.append(s)
            o += 0x1e
        self.songlen = m[o]
        self.restart = m[o + 1] if m[o + 1] <= m[o] else 0
        self.ordtab = list(m[o + 2:o + 2 + 256])
        npat = max(self.ordtab) + 1
        o += 2 + 256 + 4                          # (+4: the signature)
        self.stride = 6 * self.nch                # [0x1c810]
        self.events = []                          # 6-byte events: ins, -, period, effect << 8 | param
        for i in range(npat * 64 * self.nch):     # 0x1a38e
            b0, b1, b2, b3 = m[o:o + 4]
            self.events.append(((b0 & 0xf0) | (b2 >> 4), ((b0 & 0xf) << 8) | b1, ((b2 & 0xf) << 8) | b3))
            o += 4
        self.npat = npat
        # upload (0x1a3d3..0x1a477): mix control 0xb, then per sample DMA + a 16-byte tail by PIO
        c.port(BASE, 0xb)
        edi = 0
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
                    s.len -= 1                    # += EAX (-1): the end address is the last byte
                    s.le = s.len
            s.lerel = s.le
            s.len += s.dram
            s.le += s.dram
            s.ls += s.dram
            edi = (edi + 0x1f) & ~0x1f
        # 0x1a594: period -> FC table (index 55..4000), 0x1a4a6: channel records, 0x1a4b6: pattern pointers
        div = self.img.u16(A_DIV + 2 * (self.voices - 14))
        self.freq = [0] * 4001
        for p in range(55, 4001):
            q, r = divmod(FC_NUM // p + div, div)
            q += (2 * r) >> 32                    # add edx, edx; adc eax, 0: never carries
            self.freq[p] = q & ~1
        self.ch = [Chan(i) for i in range(32)]
        self.patptr = [p * 64 * self.stride for p in range(256)]
        self.row = 0                              # [0x1c828]
        self.order = 0                            # [0x1c829]
        self.counter = 1                          # [0x1c82d]: ticks left in the row
        self.speed = 6                            # [0x1c82e]
        self.loopcnt = 0                          # [0x1c831]
        self.looprow = self.looporder = self.loopptr = 0   # [0x1c832], [0x1c833], [0x1c834]
        self.tempo(125)                           # 0x1a514: timer 1, count 6 (only stored)
        self.ptr = self.patptr[self.ordtab[0]]    # [0x1c814]: current row's events

    def upload(self, src, edi, n):
        """0x1a699: DMA for more than 20 bytes (one transfer here: no sample is longer than 64 KB and the
        bounce buffer is 64 KB aligned), PIO otherwise. Returns the DRAM address after the data."""
        c = self.c
        if n <= 0x14:
            c.dram[edi:edi + n] = self.mod[src:src + n]          # 0x1a76f: regs 0x43 / 0x44, port 3X7
            return edi + n
        assert n <= 0x10000
        a = (edi + 0x1f) >> 5 << 1                # DRAM address >> 4, rounded up to 32 bytes
        c.w16(0x42, a)
        c.w8(0x41, 0x21)                          # start, 8-bit channel, IRQ on completion
        d = a << 4
        c.dram[d:d + n] = self.mod[src:src + n]
        return d + n

    def gf1_reset(self):
        """0x1a9ec"""
        c = self.c
        c.w8(0x4c, 0)
        c.w8(0x4c, 0)
        c.w8(0x4c, 1)
        c.w8(0x41, 0)
        c.w8(0x45, 0)
        c.w8(0x49, 0)
        c.w8(0x0e, (self.voices - 1) | 0xc0)     # 32 voices: 617400 / 32 = 19,293.75 Hz
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
        """0x1a91c"""
        c = self.c
        self.gf1_reset()
        c.port(BASE + 8, 4)
        c.port(BASE + 9, 0xe0)                    # AdLib timer control: reset, mask
        c.w8(0x4c, 7)
        c.port(BASE + 8, 4)
        c.port(BASE + 9, 3)                       # start timer 1 and timer 2
        c.w8(0x45, 0)
        c.w8(0x46, 6)                             # timer 1: (256 - 6) x 80 us = 20 ms (125 BPM)
        c.w8(0x45, 4)                             # timer 1 IRQ only
        c.port(BASE, 9)                           # mix control: line out on, latches on

    def stop(self):
        """0x1a9a2"""
        c = self.c
        c.port(BASE, 0xb)
        c.port(BASE, 0xb)
        c.port(BASE + 8, 4)
        c.port(BASE + 9, 0xe0)
        c.w8(0x45, 0)
        self.gf1_reset()

    def tempo(self, bpm):
        """0x1a514 / 0x1b694: tick period 2.5 s / bpm in timer units, truncated twice"""
        n = 1000000 // (bpm * 32)                 # 80 us units (31250 / bpm)
        if n >> 8:                                # too long for timer 1: timer 2, 320 us units
            n >>= 2
            self.treg, self.tctl = 0x47, 8
        else:
            self.treg, self.tctl = 0x46, 4
        self.tcount = -n & 0xff                   # [0x29b8b] reg, [0x29b8c] reg 0x45 value, [0x29b8d] count

    # ------------------------------------------------------------------------------------------- helpers
    def snap(self, ax, ft):
        """period -> the finetune's table: first i with period >= table0[i] (0x1b2ee and copies)"""
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
        """0x1b848 / 0x1b172: ramp from the last sent volume to the channel volume (63 steps per frame)"""
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
        """0x1b8a4 / 0x1b1c6: frequency, then the voice control (starts the voice)"""
        if ch.fc:
            fc, ch.fc = ch.fc, 0
            self.c.w16(1, fc)
            self.c.w8(0, ch.ctl)

    def note_start(self, ch):
        """0x1b734 / 0x1afb3: ramp the old voice down, switch to the other voice of the pair, set its position"""
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
        """0x1aeac"""
        if not p & 0xf:
            ch.vol = (ch.vol + (p >> 4)) & 0xff
            if ch.vol > self.maxvol:
                ch.vol = self.maxvol
        else:
            v = (ch.vol - (p & 0xf)) & 0xff
            ch.vol = 0 if v & 0x80 else v

    def toneporta(self, ch):
        """0x1acd4"""
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
        """0x1ad14 (E3x on): the period is rounded to the semitone table in place"""
        if ch.gliss:
            ax = ch.period & 0xffff
            i = 0
            while i < 36 and ax < self.pt[i]:
                i += 1
            ch.period = self.pt[ch.ft * 36 + i]

    def vibrato(self, ch):
        """0x1ad70: returns period + vibrato (the period itself is not changed)"""
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
        """0x1abb0: GUS timer IRQ"""
        c = self.c
        c.w8(0x45, 0)
        c.w8(0x45, self.tctl)                     # acknowledge, re-enable
        old = self.counter
        self.counter = (old - 1) & 0xff
        zf, sf, of = self.counter == 0, self.counter >> 7, old == 0x80
        if zf or sf != of:                        # jle
            if sf:                                # js 0x1b95d: stopped (speed 0 / end of the song)
                self.counter = 0
            else:
                self.row_tick()
        else:
            for ch in self.ch[:self.nch]:
                self.tick_effect(ch)
        self.positions.append((self.order, self.row))

    def tick_effect(self, ch):
        """0x1abea: one channel on a tick between rows (table 0x1ac04)"""
        c = self.c
        e = ch.eff
        if not e:
            return
        p, x = e & 0xff, e >> 8
        if x == 0:                                # 0x1ac44 arpeggio: counter 0 -> +y, 1 -> +x, 2 -> base
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
        if x in (1, 2):                           # 0x1aca8 / 0x1acb4 portamento (no limits)
            ch.period = (ch.period + (-p if x == 1 else p)) & 0xffffffff
            return self.out_freq(ch, ch.period)
        if x == 3:                                # 0x1acbc tone portamento
            if not ch.target:
                return
            self.toneporta(ch)
            return self.out_freq(ch, ch.period)
        if x == 4:                                # 0x1ad48 vibrato
            if p:
                ch.vib = self.nibbles(ch.vib, p)
            return self.out_freq(ch, self.vibrato(ch))
        if x == 5:                                # 0x1adc0 tone portamento + volume slide
            if not ch.target:
                self.volslide(ch, p)
                return self.out_vol(ch)
            self.toneporta(ch)
            self.volslide(ch, p)
            return self.out_freq(ch, ch.period)
        if x == 6:                                # 0x1ade4 vibrato + volume slide
            a = self.vibrato(ch)
            self.volslide(ch, p)
            return self.out_freq(ch, a)
        if x == 7:                                # 0x1ae00 tremolo
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
        if x == 9:                                # 0x1ae90: back to the sample start for later notes
            ch.start = ch.smp.dram
            ch.eff = 0
            return
        if x == 0xa:                              # 0x1aea4 volume slide
            self.volslide(ch, p)
            return self.out_vol(ch)
        if x == 0xe:                              # 0x1aee4 (table 0x1aef4)
            y, v = p >> 4, p & 0xf
            if y == 9:                            # 0x1af34 retrigger
                ch.retrig = (ch.retrig - 1) & 0xff
                if ch.retrig:
                    return
                ch.retrig = v
                return self.retrigger(ch)
            if y == 0xc:                          # 0x1af48 note cut, when the row's tick counter equals x
                if self.counter != v:
                    return
                ch.vol = 0
                c.select(ch.voice)
                c.w16(7, self.volt[0])
                c.w16(8, self.volt[ch.vol])
                c.w8(0x0d, 0x40)
                ch.gvol = 0
                return
            if y == 0xd:                          # 0x1afa4 note delay, when the tick counter equals x
                if self.counter != v:
                    return
                return self.retrigger(ch)
        return                                    # 8, B, C, D, F and the other E commands: nothing

    def out_freq(self, ch, per):
        """0x1b15a: FC of a period, then 0x1b164"""
        ch.fc = self.fc_of(per)
        self.out_vol(ch)

    def out_vol(self, ch):
        """0x1b164: select the channel's voice, volume ramp, pending frequency"""
        self.c.select(ch.voice)
        self.vol_update(ch)
        self.fc_update(ch)

    def retrigger(self, ch):
        """0x1afb3"""
        self.c.select(ch.voice)
        self.note_start(ch)
        a = ch.period
        if ch.ft:
            a = ch.period = self.snap(a, ch.ft)
        ch.fc = self.fc_of(a)
        self.vol_update(ch)
        self.fc_update(ch)

    def row_tick(self):
        """0x1b210: read a row"""
        c = self.c
        self.counter = self.speed
        edi = self.ptr
        for ch in self.ch[:self.nch]:
            c.select(ch.voice)
            k = edi // 6
            ins, per, eff = self.events[k] if 0 <= k < len(self.events) else (0, 0, 0)
            assert 0 <= k < len(self.events) and edi % 6 == 0
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
                self.counter = 0                  # the song stops (0x1b901)
                self.order = self.restart
            self.ptr = self.patptr[self.ordtab[self.order]]
        else:
            self.ptr += self.stride

    def row_effect(self, ch, a, e):
        """effect table 0x1b298, then 0x1b728 (note start), 0x1b810 (period), 0x1b848 (volume, frequency)"""
        x, p = e >> 8, e & 0xff
        if x == 0:
            ch.arp = 0
        elif x in (3, 5):                         # 0x1b2e4 / 0x1b328 tone portamento: the note is the target
            if a:
                if ch.ft:
                    a = self.snap(a, ch.ft)
                ch.target = a
                a = 0
            if x == 3 and p:
                ch.pspeed = p
        elif x == 7:                              # 0x1b368
            ch.tbase = ch.vol
        elif x == 9:                              # 0x1b374 sample offset (bytes = param << 8)
            off = p << 8
            if ch.smp.lerel >= off:
                ch.start = ch.smp.dram + off
            else:
                e = 0
        elif x == 0xb:                            # 0x1b39c position jump
            self.row = 0x3f
            self.order = (p - 1) & 0xff
            e = 0
        elif x == 0xc:                            # 0x1b3b4
            ch.vol = min(p, self.maxvol)
            e = 0
        elif x == 0xd:                            # 0x1b3cc pattern break (BCD row)
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
        elif x == 0xe:                            # 0x1b450 (table 0x1b45c)
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
            elif y == 0xd:                        # 0x1b660 note delay: nothing now
                ch.period = a
                ch.eff = e
                return
            elif y == 0xe:                        # 0x1b66c pattern delay: counter = x * speed
                self.counter = (v * self.speed) & 0xff
                e = 0
        elif x == 0xf:                            # 0x1b688
            if p >= 0x20:
                self.tempo(p)
                self.c.w8(0x45, 0)
                self.c.w8(self.treg, self.tcount)
                self.c.w8(0x45, self.tctl)
            else:
                self.speed = self.counter = p
            e = 0
        # 0x1b728
        ch.eff = e & 0xffff
        if not a:
            return self.row_volume(ch)
        self.note_start(ch)
        self.row_period(ch, a)

    def row_period(self, ch, a):
        """0x1b810"""
        if ch.ft:
            a = self.snap(a, ch.ft)
        ch.period = a
        ch.fc = self.fc_of(a)
        self.row_volume(ch)

    def row_volume(self, ch):
        """0x1b848"""
        self.vol_update(ch)
        self.fc_update(ch)


# ------------------------------------------------------------------------------------------------ GF1 model
class GF1:
    """What the card does with the writes (same model as jizz/re/models/music/mxmplay.py class GF1, see
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

    def port(self, p, val):
        pass

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
TIMER_US = {0x46: 80, 0x47: 320}


class Timer:
    """When the player ticks, in microseconds after the timers are started (AdLib control 4 = 3 in 0x1a91c), as
    the GUS timers and run_stars.py clock them: a started timer overflows every (256 - count) steps (timer 1:
    80 us, timer 2: 320 us) and reloads the count register as it is at that moment, so a count written by a tick
    takes effect one period later. Tick 0 is timer 1's first overflow (its count register was still 0 when the
    timers started, 0x46 = 6 is written just after: 20.48 ms); tick 0 switches to timer 2 (F63 at 00:00), whose
    first period had started together with timer 1, with its count still 0: tick 1 at 81.92 ms. From then on
    every tick is one timer 2 period after the previous one: (256 - 0xb2) x 320 us = 24.96 ms (40.064 Hz) for
    tempo 99, (256 - 0xbd) x 320 us = 21.44 ms (46.642 Hz) for tempo 115 (from the tick after 0b:00)."""

    def __init__(self):
        self.k = 0
        self.c2 = 0                              # timer 2 count register
        self.nxt = (256 - self.c2) * 320.0       # timer 2's next overflow

    def next_tick(self):
        if self.k == 0:
            t = 256 * 80.0
        else:
            t = self.nxt
            self.nxt += (256 - self.c2) * 320.0  # reloaded at this overflow
        self.k += 1
        return t

    def after_tick(self, lines):
        for ln in lines:                         # the tick's writes to reg 0x47
            f = ln.split()
            if f[1] == 'g' and f[2] == 'r47':
                self.c2 = int(f[3], 16)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--img', default=os.path.join(RE, 'unpacked', 'stars_img.bin'))
    ap.add_argument('--module', default=os.path.join(RE, 'unpacked', 'files', '01.bin'))
    ap.add_argument('--ticks', type=int, default=16871, help='player ticks before main stops the music '
                    '(16871 in the capture: main calls 0x1c77e at frame 26359)')
    ap.add_argument('--check', default=RE, help='folder with full.gus / full.dram / full.ticks ("" = no check)')
    ap.add_argument('--out', help='write PREFIX.gus (log) and PREFIX.dram')
    ap.add_argument('--wav', help='render through the GF1 model (needs numpy)')
    ap.add_argument('--times', help='write the tick times (s after the timer start), order and row per tick')
    ap.add_argument('--stop', type=float, help='with --wav: time of the stop (s after the timer start; default: one '
                    'tick period after the last tick)')
    ap.add_argument('--info', action='store_true', help='print the module, the DRAM layout and the effects used')
    a = ap.parse_args()

    img = Img(a.img)
    mod = open(a.module, 'rb').read()
    card = Card()
    pl = Player(img, mod, card)
    pl.detect()
    pl.convert()
    if a.info:
        return info(pl)
    timer = Timer()
    hw = None
    if a.wav:
        hw = GF1(card.dram)
        card.hw = hw
        out, frame = [], 0                       # frames rendered since the timer start
        first = None
    pl.start()
    times = []
    for k in range(a.ticks):
        t = timer.next_tick()
        times.append(t)
        if hw is not None:
            f = int(-(-t * 1e-6 * hw.frame_rate() // 1))   # frames that start before the tick
            if f > frame:
                buf = hw.render(f - frame)
                if first is not None:
                    out.append(buf)
                frame = f
            if first is None:
                first = frame
        card.tick = k
        n0 = len(card.lines)
        pl.tick()
        timer.after_tick(card.lines[n0:])
        if hw is not None:
            del card.lines[n0:]                  # (the log check runs without --wav)
    card.tick = a.ticks - 1
    if hw is not None:
        t = timer.next_tick() if a.stop is None else a.stop * 1e6
        f = int(-(-t * 1e-6 * hw.frame_rate() // 1))
        if f > frame:
            out.append(hw.render(f - frame))
    pl.stop()

    if a.out:
        with open(a.out + '.gus', 'w') as f:
            f.write('\n'.join(card.lines) + '\n')
        open(a.out + '.dram', 'wb').write(card.dram)
    if a.times:
        with open(a.times, 'w') as f:
            f.write('# tick, time in s after the timer start (0x1a91c), order and row after the tick\n')
            for k in range(a.ticks):
                f.write('%d %.6f %02x %02x\n' % (k, times[k] / 1e6, pl.positions[k][0], pl.positions[k][1]))

    if a.check and not a.wav:
        ok = True
        ref = open(os.path.join(a.check, 'full.dram'), 'rb').read()
        nd = sum(1 for x, y in zip(ref, card.dram) if x != y)
        print('DRAM: %d of %d bytes differ' % (nd, len(ref)))
        ok &= nd == 0
        ref = open(os.path.join(a.check, 'full.gus')).read().split('\n')
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
        bad = n = 0
        for ln in open(os.path.join(a.check, 'full.ticks')):
            if ln.startswith('#'):
                continue
            f = ln.split()
            g, o, r = int(f[3]), int(f[4]), int(f[5])
            if 0 < g <= len(pl.positions):
                n += 1
                if pl.positions[g - 1] != (o, r):
                    bad += 1
        print('music position: %d frames compared, %d differ' % (n, bad))
        ok &= bad == 0
        print('ALL MATCH' if ok else 'MISMATCH')

    if a.wav:
        import numpy as np
        import wave
        pcm = np.concatenate(out)
        rate = hw.frame_rate()
        peak = float(np.abs(pcm).max())
        print('rendered %d frames at %.2f Hz (%.2f s from tick 0); peak %.3f (1.0 = one voice at full scale and '
              'gain 1, hard panned)' % (len(pcm), rate, len(pcm) / rate, peak))
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
    used = collections.Counter()
    seen = set()
    for o in range(pl.songlen):
        p = pl.ordtab[o]
        for k in range(p * 64 * pl.nch, (p + 1) * 64 * pl.nch):
            ins, per, eff = pl.events[k]
            x, y = eff >> 8, eff & 0xff
            key = (x, y if x in (0xb, 0xd, 0xe, 0xf) else (y >> 4 if x == 0xe else None))
            if eff and (o, k) not in seen:
                used[key] += 1
                seen.add((o, k))
    print('effects in the song (by order): ' + ', '.join(
        '%X%s x%d' % (x, '%02x' % y if y is not None else '', n) for (x, y), n in sorted(
            used.items(), key=lambda kv: (kv[0][0], -1 if kv[0][1] is None else kv[0][1]))))


if __name__ == '__main__':
    main()

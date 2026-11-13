"""Reference implementation of CLONE.EXE's music player: MXMPlay, the build that calls itself "cubic tiny gus xm player
v1.4 (c) '95/96 Niklas Beisert / pascal" (code 0x4018d4..0x40266c, tables 0x40266d..0x40296c), mirroring the
assembly routine by routine, plus a GF1 model (8- and 16-bit voices) to render it.

From the module (modsynth.py builds it from clone_img.bin, or a dump of the buffer at 0x428470) and clone_img.bin
(frequency, vibrato and jump tables) it produces
  - the GUS DRAM image (xmpInit's upload 0x4025de)                          -> compare with full.dram
  - the GF1 register-write log, in run_clone.py's text format              -> compare with full.gus
  - the music position (order, row) and the player timer after every IRQ   -> compare with full.ticks
and with --wav renders the song through the GF1 model (617,400 / 18 voices = 34,300 Hz).

usage:  python3 mxmplay.py [--module module.bin] [--check ../..] [--out PREFIX] [--real-volume-read]
                           [--wav out.wav] [--positions out.txt]

Volume reads: setvol 0x402425 reads the voice's current volume (reg 0x89) and ramps from it; the capture's fake GUS
answers with the last value written to reg 9 (the default here, to match full.gus). --real-volume-read (implied by
--wav) answers from the GF1 model, as on hardware; then the IRQ's busy wait for the volume ramps (0x4024ee) also
takes GF1 time, and the register stream differs from full.gus.

Routine map (notes-music.md section 4; JIZZ's addresses in brackets):
  0x4018d8 data block pointer [0x296dc]   0x4018e3 vibrato + volume tables [0x296e7: volume table only]
  0x40196a xmpInit [0x29738]  0x4025de sample upload [0x2a26f]  0x402621 ULTRASND parser [0x2a2cd]
  0x402129 xmpPlay [0x29db1]  0x40219a xmpStop [0x29e22]  0x402245 timer install [0x29ed1]
  0x40229d timer remove [0x29f2a]  0x4021ac IRQ0 handler [0x29e38]  0x4022b3 xmpGetTimer [0x29f44]
  0x401bf4 xmpGetPos [0x299c4]  0x401bde xmpSetVolume [0x299ae]  0x401be9 xmpGetSync [0x299b9]
  0x4024ab GUS update [0x2a13c]  0x401e9e player tick [0x29b26]  0x401a47 new note [0x29801]
  0x4019ee pitch -> frequency [0x297a8]  0x401c02 pitch clamp [0x299d4]  0x402399 GF1 reset [0x2a02a]
  0x4025ae GUS start [0x2a23f]  0x4025cd GUS stop [0x2a25e]  0x402425 set volume [0x2a0b6]
  0x40248b voice off [0x2a11c]  0x401e88 effect dispatch [0x29b10], jump table 0x40274d (0x88 dwords + 0x401c21)
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)

BASE = 0x400000
PIT_HZ = 1193182
TEMPO_K = 0x2d86bf          # PIT clocks per tick * BPM (135 BPM -> 22,100)
FREQ_DIV = 0x1ee            # FC = freq * voices / 494, bit 0 cleared
FT0, FT1, FT2, FT3 = 0x4026ad, 0x4026cd, 0x4026ed, 0x40270d   # frequency tables (16 x u16, u16, u16, u32)
VIB_SRC = 0x40266d          # 64 bytes: quarter sine 0..64, expanded to 256 signed bytes at data + 0x2daa
JUMPTAB, FX_BASE = 0x40274d, 0x401c21
GF1_CLOCK = 617400.0
MASTER_VOL = 0x20           # main 0x400b0e: xmpSetVolume(0x20) right after xmpPlay

U32 = 0xffffffff


def s32(v):
    v &= U32
    return v - (1 << 32) if v & 0x80000000 else v


def s16(v):
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


def s8(v):
    v &= 0xff
    return v - 0x100 if v & 0x80 else v


class GusLogger:
    """The GUS as run_clone.py's Gus class sees it: logs register writes as text ("tick vN rXX val",
    "tick pPPP val"), answers voice register reads with the last written value (| 1 for 0x80 / 0x8d)."""

    def __init__(self, base=0x240):
        self.base = base
        self.voice = 0
        self.regs = {}
        self.lines = []
        self.tick = 0
        self.dram = bytearray(1 << 20)

    def port(self, p, val):                                 # out base + p (p = 0: mix control)
        self.lines.append('%d p%03x %x' % (self.tick, self.base + p, val))

    def select(self, v):                                    # 0x40236a: out base+0x102 (not logged)
        self.voice = v & 31

    def _w(self, reg, val):
        self.regs[(self.voice, reg)] = val
        self.lines.append('%d v%d r%02x %x' % (self.tick, self.voice, reg, val))

    def w8(self, reg, val):                                 # 0x4022da: reg -> base+0x103, byte -> base+0x105
        self._w(reg, val & 0xff)

    def w16(self, reg, val):                                # 0x402314: reg -> base+0x103, word -> base+0x104
        self._w(reg, val & 0xffff)

    def r8(self, reg):                                      # 0x40232a: byte from base+0x105
        v = self.regs.get((self.voice, reg - 0x80), 0)
        if reg in (0x80, 0x8d):
            v |= 1
        if reg in (0x81, 0x82, 0x83, 0x84, 0x85, 0x89, 0x8a, 0x8b):
            return (v >> 8) & 0xff
        return v & 0xff

    def r16(self, reg):                                     # 0x402341: word from base+0x104
        v = self.regs.get((self.voice, reg - 0x80), 0)
        if reg in (0x80, 0x8d):
            v |= 1
        return v & 0xffff

    def poke(self, addr, b):                                # DRAM write (regs 0x43/0x44 + base+0x107, not logged)
        self.dram[addr & 0xfffff] = b

    def wait_ramps(self, voices):                           # the busy wait 0x4024ee: no time passes here
        pass


class GF1(GusLogger):
    """GF1 synthesiser model: what the card does with the writes (it also logs them like GusLogger).

    JIZZ's model (jizz/re/models/music/mxmplay.py, after paper/re/notes-music.md section 7) with 16-bit voices:
    positions in 1/512 address units (address << 9 | fraction), increment FC >> 1 per frame; 12-bit log volume
    kept in 1/512 steps; ramps; pan 0..15. One frame computes every active voice once: 617400 / active frames per
    second (18 voices: 34,300 Hz). Voice control bit 2 = 16-bit data: the 20-bit address a is a word address,
    the DRAM byte address is (a & 0xc0000) | (a & 0x1ffff) << 1 (bit 17 is ignored: the module sets it in every
    16-bit address); 8-bit voices read DRAM[a]. Linear interpolation between sample a and a + 1. Loops forward
    (bit 3) and bidirectional (bits 3 + 4, direction bit 6), as DOSBox-X models them. Stopped voices keep being
    mixed at their parked position."""

    def __init__(self):
        super().__init__()
        self.latch = 0
        self.out = None
        self.waited = 0
        self.reset()

    def reset(self):
        self.active = 14
        self.dac = False
        self.v = [{'ctl': 3, 'fc': 0, 'start': 0, 'end': 0, 'pos': 0, 'vctl': 3, 'rate': 0, 'lo': 0, 'hi': 0,
                   'vol': 0, 'pan': 7} for _ in range(32)]

    def w8(self, reg, val):
        super().w8(reg, val)
        self.hw(reg, ((val & 0xff) << 8) | self.latch)

    def w16(self, reg, val):
        super().w16(reg, val)
        self.latch = val & 0xff
        self.hw(reg, val & 0xffff)

    def r8(self, reg):
        s = self.v[self.voice]
        if reg == 0x80:
            return s['ctl']
        if reg == 0x8d:
            return s['vctl']
        return 0

    def r16(self, reg):
        s = self.v[self.voice]
        if reg == 0x89:
            return ((s['vol'] >> 9) << 4) & 0xffff
        return 0

    def hw(self, reg, d):
        s = self.v[self.voice]
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

    def wait_ramps(self, voices):
        """0x4024ee polls reg 0x8d of every channel until bit 0 (ramp stopped) is set; the GF1 runs meanwhile."""
        n = 0
        for i in range(voices):
            s = self.v[i]
            if not s['vctl'] & 3:
                inc = self.ramp_inc(s)
                if inc:
                    dist = (s['vol'] - s['lo']) if s['vctl'] & 0x40 else (s['hi'] - s['vol'])
                    n = max(n, max(1, -(-dist // inc)))
        self.run(n)
        self.waited += n
        return n

    def run(self, n):
        if n <= 0:
            return
        if self.out is not None:
            self.out.append(self.render(n))
        else:
            for i in range(self.active):
                self.voice_frames(self.v[i], n, False)

    def prepare_render(self):
        import numpy as np
        self.np = np
        d = bytes(self.dram)
        self.dram8 = np.frombuffer(d + b'\0', dtype=np.int8).astype(np.float64) / 128.0
        self.dram16 = np.frombuffer(d + b'\0\0', dtype='<i2').astype(np.float64) / 32768.0    # even bytes
        e = np.arange(4096) >> 8
        m = np.arange(4096) & 255
        self.gain = (256 + m) * np.exp2(e - 24.0)            # GF1 log volume: (256 + mantissa) << exponent
        self.out = []

    def fetch(self, a, bit16):
        np = self.np
        if not bit16:
            return self.dram8[a & 0xfffff]
        w = ((a & 0xc0000) >> 1) | (a & 0x1ffff)            # word index in DRAM
        return self.dram16[w & 0x7ffff]

    def render(self, n):
        """n frames of stereo output (1.0 = one voice at full scale and gain 1, hard panned)"""
        np = self.np
        L = np.zeros(n)
        R = np.zeros(n)
        for i in range(self.active):
            s = self.v[i]
            bit16 = bool(s['ctl'] & 4)
            pos, vol = self.voice_frames(s, n, True)
            a = (pos >> 9) & 0xfffff
            f = (pos & 511).astype(np.float64) / 512.0
            s0 = self.fetch(a, bit16)
            s1 = self.fetch(a + 1, bit16)
            g = (s0 + (s1 - s0) * f) * self.gain[vol >> 9]
            L += g * ((15 - s['pan']) / 15.0)
            R += g * (s['pan'] / 15.0)
        out = np.stack([L, R], 1)
        return out if self.dac else out * 0

    def voice_frames(self, s, n, arrays):
        """advance one voice by n frames; with arrays=True return the position and volume of each frame"""
        np = self.np if arrays else None
        pos = vol = None
        # --- wave position ---
        ctl, p0, inc = s['ctl'], s['pos'], s['fc'] >> 1
        st, en = s['start'], s['end']
        if ctl & 3 or inc == 0:
            if arrays:
                pos = np.full(n, p0, dtype=np.int64)
        else:
            k = np.arange(n + 1, dtype=np.int64) if arrays else None
            Lp = en - st
            bidi = (ctl & 0x18) == 0x18 and Lp > 0
            back = bool(ctl & 0x40)
            if bidi:
                # unfolded forward coordinate u: plain up to the loop end, then folded with period 2 * Lp
                # (a voice moving backwards inside the loop is mapped to u = st + 2 * Lp - (p0 - st))
                u0 = st + 2 * Lp - (p0 - st) if back and st <= p0 <= en else p0
                if arrays:
                    u = u0 + k[:n] * inc
                    m = np.mod(u - st, 2 * Lp)
                    pos = np.where(u <= en, u, np.where(m > Lp, st + 2 * Lp - m, st + m))
                u = u0 + n * inc
                if u <= en:
                    s['pos'], b_end = u, False
                else:
                    m = (u - st) % (2 * Lp)
                    s['pos'], b_end = (st + 2 * Lp - m, True) if m > Lp else (st + m, False)
                s['ctl'] = (ctl & ~0x40) | (0x40 if b_end else 0)
            elif back:
                if arrays:
                    pp = p0 - k * inc
                    under = pp < st
                    if under.any():
                        if ctl & 8 and Lp > 0:
                            pp[under] = en - (st - pp[under]) % Lp
                        else:
                            pp[under] = st
                    pos = pp[:n]
                p = p0 - n * inc
                if p < st:
                    if ctl & 8 and Lp > 0:
                        p = en - (st - p) % Lp
                    else:
                        p = st
                        s['ctl'] = ctl | 1
                s['pos'] = p
            else:
                if arrays:
                    pp = p0 + k * inc
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
        # --- volume ramp ---
        v0 = s['vol']
        if not s['vctl'] & 3 and self.ramp_inc(s):
            inc = self.ramp_inc(s)
            if s['vctl'] & 0x40:
                if arrays:
                    vol = np.maximum(v0 - np.arange(n, dtype=np.int64) * inc, s['lo'])
                    vol[0] = v0
                v = v0 - n * inc
                if v <= s['lo']:
                    v = s['lo']
                    s['vctl'] |= 1
            else:
                if arrays:
                    vol = np.minimum(v0 + np.arange(n, dtype=np.int64) * inc, s['hi'])
                    vol[0] = v0
                v = v0 + n * inc
                if v >= s['hi']:
                    v = s['hi']
                    s['vctl'] |= 1
            s['vol'] = v
        elif arrays:
            vol = np.full(n, v0, dtype=np.int64)
        if arrays:
            return pos, np.clip(vol, 0, (4096 << 9) - 1)
        return None


class Chan:
    """channel record, 0x100 bytes at data + 0x9aa + 0x100 * ch (offsets in the comments; the same layout as JIZZ's
    build, which has it at data + 0x9ac)"""

    def __init__(self):
        self.active = 0       # +0x00 voice has a sample
        self.start = 0        # +0x01 u24 DRAM start (16-bit samples: GF1 word address)
        self.end = 0          # +0x05 u24 DRAM end
        self.loop = 0         # +0x09 u24 DRAM loop start
        self.mode = 0         # +0x0d GF1 voice control (16 bit / loop / bidi)
        self.stop = 0         # +0x0e stop the voice before restarting it
        self.newsmp = 0       # +0x0f write loop start / end
        self.offset = 0       # +0x10 restart at start + offset (-1 = keep playing)
        self.gfreq = 0        # +0x14 GF1 FC
        self.gvol = 0         # +0x16 GF1 volume (16 bit)
        self.gpan = 0         # +0x18 GF1 pan 0..15
        self.vol = 0          # +0x19 volume 0..64
        self.fvol = 0         # +0x1a volume of this tick (then 0..64 after the envelope / fade / master 0x20)
        self.pan = 0          # +0x1b panning 0..255
        self.fpan = 0         # +0x1c panning of this tick
        self.pitch = 0        # +0x1d s32 pitch (1/256 semitone, 0x3000 - note * 256 - relnote; lower = higher)
        self.fpitch = 0       # +0x21 pitch of this tick (arpeggio / vibrato add to it)
        self.ins = 0          # +0x25 instrument number
        self.insp = 0         # +0x26 instrument pointer (module offset here; 0 = none)
        self.rel = 0          # +0x2a u16 relative note * 256 + finetune of the sample
        self.keyon = 0        # +0x2c 1 until key off
        self.fade = 0         # +0x2d u16 fadeout level (0x8000 = full)
        self.envpt = 0        # +0x31 volume envelope point
        self.envpos = 0       # +0x35 u16 ticks into that point
        self.svol = 0         # +0x3d sample volume
        self.span = 0         # +0x3e sample panning
        self.eff = 0          # +0x3f effect of this row (0xff = none; xmpPlay clears it to 0, the first row sets it)
        self.volcmd = 0       # +0x40 volume column command (high nibble)
        self.target = 0       # +0x41 s32 tone portamento target pitch
        self.tpspeed = 0      # +0x45 s32 tone portamento speed (3xx: xx * 16)
        self.vslide = 0       # +0x49 volume slide memory
        self.pupspeed = 0     # +0x4f s32 portamento up speed (1xx: xx * 16)
        self.pdnspeed = 0     # +0x53 s32 portamento down speed (2xx: xx * 16)
        self.vibspeed = 0     # +0x5b vibrato speed (x * 4)
        self.vibpos = 0       # +0x5c vibrato position (a byte; +0x5d stays 0)
        self.vibdepth = 0     # +0x5e vibrato depth (y * 4)
        self.arpidx = 0       # +0x65 arpeggio step 0..2
        self.arp = [0, 0, 0]  # +0x66..+0x68 arpeggio: 0, x, y semitones
        self.rtcount = 0      # +0x6a retrigger tick counter
        self.rtint = 0        # +0x6b retrigger interval (y)
        self.rtvol = 0        # +0x6c retrigger volume change (x)
        self.note = 0         # +0x6d last note
        self.ofsmem = 0       # +0x6e sample offset memory (9xx)


class Player:
    def __init__(self, mod, img, gus, port=0x240, mastervol=MASTER_VOL):
        self.m = mod
        self.d = mod.data
        self.g = gus
        self.port = port                                    # +0x4f (0 = no sound)
        rd = lambda a, fmt: struct.unpack_from(fmt, img, a - BASE)
        self.ft = (rd(FT0, '<16H'), rd(FT1, '<16H'), rd(FT2, '<16H'), rd(FT3, '<16I'))
        # data block (zeroed by xmpInit)
        self.globalvol = 0      # +0x00
        self.mastervol = 0x40   # +0x01 (xmpInit), xmpSetVolume 0x401bde (main: 0x20 right after xmpPlay)
        self.mastervol_after_play = mastervol
        self.sync = 0           # +0x02 (read by 0x401be9 xmpGetSync, never called; only ever written 0)
        self.tick = 0           # +0x03
        self.speed = 0          # +0x04
        self.newrow = 0         # +0x05
        self.row = 0            # +0x06 u32
        self.patpos = 0         # +0x0a pattern data pointer
        self.patrows = 0        # +0x0e
        self.order = 0          # +0x12
        self.jumporder = 0      # +0x16 (0xffffffff = none)
        self.jumprow = 0        # +0x1a
        self.patdelay = 0       # +0x1e (nothing in this build sets it)
        self.cur = 0            # +0x35 current PIT period
        self.timer = 0          # +0x39
        self.period = 0         # +0x41 tick period in PIT clocks
        self.remain = 0         # +0x45
        self.voices = 0         # +0x53 active GF1 voices
        self.ch = [Chan() for _ in range(32)]
        # 0x4018e3: 256-byte vibrato sine at +0x2daa, 257-word volume table at +0x7a8
        q = list(img[VIB_SRC - BASE:VIB_SRC - BASE + 64])
        half = q + [0x40] + q[63:0:-1]
        self.vibtab = [s8(x) for x in half] + [s8(-x) for x in half]
        self.volt = self.voltab()
        # effect dispatch 0x401e88: jump table 0x40274d, handler = dword + 0x401c21
        self.fxtab = [(struct.unpack_from('<I', img, JUMPTAB - BASE + 4 * i)[0] + FX_BASE) & U32
                      for i in range(0x88)]
        self.handlers = {
            0x401c21: None,                 # ret
            0x401c6b: self.fx_arp_row, 0x401c88: self.fx_arp_tick,
            0x401cec: self.fx_pup_row, 0x401d0a: self.fx_pup_tick,
            0x401cfb: self.fx_pdn_row, 0x401d22: self.fx_pdn_tick,
            0x401d3a: self.fx_tp_row, 0x401d49: self.fx_tp_tick,
            0x401d78: self.fx_vib_row, 0x401d93: self.fx_vib_tick,
            0x401dbd: self.fx_vslide_row, 0x401dc8: self.fx_vslide_tick,
            0x401c51: self.fx_pan, 0x401c41: self.fx_vol, 0x401c22: self.fx_speed,
            0x401df3: self.fx_retrig_row, 0x401e0c: self.fx_retrig_tick,
            0x401cbe: self.vc_vol1, 0x401cba: self.vc_vol2, 0x401cb6: self.vc_vol3, 0x401cb2: self.vc_vol4,
            0x401cae: self.vc_vol5, 0x401cdb: self.vc_finedn, 0x401cc8: self.vc_fineup, 0x401c5b: self.vc_pan,
        }
        self.stats = {}

    # --- 0x4018e3 (second half): volume table, 257 words: linear 0..256 -> GF1 log volume ---
    @staticmethod
    def voltab():
        t = [0] * 257
        t[0x100] = 0xefff
        for i in range(1, 256):
            e = 7 + i.bit_length()
            t[i] = ((((i << (20 - e)) & 0xfff) | (e << 12)) - 0x1000) & 0xffff
        return t

    # --- 0x40196a xmpInit: header copy and pointer fix-up, sample upload 0x4025de, tables 0x4018e3 ---
    def init(self):
        m = self.m
        n = m.smp16 * 2 + m.smp8
        if self.port:
            src = m.smpstart
            for a in range((n >> 4) << 4):                  # 16 bytes per block (with interrupts off)
                self.g.poke(a, self.d[src + a])

    # --- 0x402399 GF1 reset (voices = max(channels, 14)) ---
    def gf1_reset(self, nch):
        g = self.g
        self.voices = nch if (nch & 0xff) >= 14 else (nch & ~0xff) | 14     # cmp al, 0xe; jae; mov al, 0xe
        g.w8(0x4c, 0)                                       # (each followed by two 7-read delays)
        g.w8(0x4c, 1)
        g.w8(0x0e, ((self.voices - 1) | 0xc0) & 0xff)
        for v in range(32):
            g.select(v)
            g.w16(0x09, 0)
            g.w8(0x00, 3); g.w8(0x00, 3)                    # 0x4022f2: write, delay, write again
            g.w8(0x0d, 3); g.w8(0x0d, 3)
            g.w8(0x06, 0x3f)
        g.w8(0x4c, 7)
        g.select(0)
        g.port(0, 8)

    # --- 0x402129 xmpPlay(order) ---
    def play(self, order=0):
        m = self.m
        self.jumporder = order
        self.order = order
        self.row = 0
        if self.port:                                       # 0x4025ae
            self.gf1_reset(m.channels)
            self.g.select(0)
            self.g.port(0, 9)
        self.ch = [Chan() for _ in range(32)]
        for c, p in zip(self.ch, m.panpos):
            c.pan = p
        self.globalvol = 0x40
        self.jumprow = 0
        self.sync = 0
        self.speed = m.speed
        self.tick = (m.speed - 1) & 0xff
        self.period = TEMPO_K // m.bpm
        # 0x402245 timer install
        self.remain = 0
        self.cur = 0x10000
        self.timer = (0xffff0000 - self.period) & U32
        self.busy = 0
        self.mastervol = self.mastervol_after_play          # main: xmpSetVolume(0x20) before the first IRQ

    # --- 0x40219a xmpStop ---
    def stop(self):
        if self.port:                                       # 0x4025cd
            self.gf1_reset(14)

    # --- 0x4021ac IRQ0 ---
    def irq(self):
        """returns True if a player tick ran in this interrupt"""
        self.timer = (self.timer + self.cur) & U32
        ebx, ecx = 0x10000, self.period
        edx = self.remain - ebx
        if not (self.remain > ebx):                         # sub edx, ebx; ja (unsigned)
            edx = ecx
        self.remain = edx & U32
        self.cur = min(self.remain, ebx)
        if self.remain == ecx:
            self.gus_update()
            self.player_tick()
            return True
        return False

    def get_timer(self, elapsed):
        """0x4022b3: PIT clocks since the music started (timer + clocks elapsed in the current PIT period)"""
        return s32(self.timer + elapsed)

    def get_pos(self):
        """0x401bf4: AH = order, AL = row"""
        return ((self.order & 0xff) << 8) | (self.row & 0xff)

    # --- 0x4024ab GUS update (start of every IRQ tick, before the player) ---
    def gus_update(self):
        if not self.port:
            return
        g = self.g
        n = self.m.channels
        for i in range(n):
            c = self.ch[i]
            if c.active and c.stop:
                g.select(i)
                g.w8(0x00, c.mode | 3); g.w8(0x00, c.mode | 3)
                self.voice_off()
                c.stop = 0
        g.wait_ramps(n)                                     # 0x4024ee: until bit 0 of reg 0x8d, every channel
        for i in range(n):
            g.select(i)
            c = self.ch[i]
            if not c.active:
                self.voice_off()
            else:
                if c.newsmp:
                    self.waddr(0x02, c.loop)
                    self.waddr(0x04, c.end)
                if c.offset != -1:
                    a = (c.offset + c.start) & U32
                    if a >= c.end:
                        a = c.end - 1
                    self.waddr(0x0a, a)
                    ctl = g.r8(0x80)
                    g.w8(0x00, (ctl & 0x40) | c.mode)
                g.r8(0x80)                                  # result ignored: the code tests bl (= 0x80), not al
                g.w8(0x0c, c.gpan)
                g.w16(0x01, c.gfreq)
                self.setvol(c.gvol)
            c.newsmp = 0
            c.offset = -1

    def waddr(self, reg, a):                                # 0x40237a
        self.g.w16(reg, (a >> 7) & 0x1fff)
        self.g.w16(reg + 1, (a << 9) & 0xffff)

    def voice_off(self):                                    # 0x40248b: ramp down to 0x04xx
        g = self.g
        g.w8(0x07, 0x04)
        g.w8(0x08, 0xfc)
        g.w8(0x0d, 0x40); g.w8(0x0d, 0x40)

    def setvol(self, target):                               # 0x402425
        g = self.g
        cur = g.r16(0x89)
        lo, hi, mode = cur, target, 0
        if cur > target:
            lo, hi, mode = target, cur, 0x40
        diff = hi - lo
        if diff == 0:
            return
        if diff < 0x1000:                                   # small step: set directly
            g.w16(0x09, target)
            return
        if lo < 0x1000:
            lo = 0x1000
        if hi > 0xfc00:
            hi = 0xfc00
        g.w8(0x07, lo >> 8)
        g.w8(0x08, hi >> 8)
        g.w8(0x0d, mode); g.w8(0x0d, mode)

    # --- 0x4019ee: pitch -> frequency (2^(-pitch/3072) via 4 nibble tables) ---
    def freq(self, pitch):
        x = (pitch + 0x8000) & U32
        t0, t1, t2, t3 = self.ft
        f = t3[(x >> 12) & 15]
        f = ((f * t2[(x >> 8) & 15]) >> 15) & U32
        f = ((f * t1[(x >> 4) & 15]) >> 15) & U32
        f = ((f * t0[x & 15]) >> 15) & U32
        return f

    def clamp_pitch(self, p):                               # 0x401c02
        if not p > self.m.lowpitch:                         # jg
            p = self.m.lowpitch
        if not p < self.m.highpitch:                        # jl
            p = self.m.highpitch
        return p

    # --- 0x401e9e player tick ---
    def player_tick(self):
        m = self.m
        n = m.channels
        self.newrow = 0
        for c in self.ch[:n]:
            c.fvol = c.vol
            c.fpan = c.pan
            c.fpitch = c.pitch
        self.tick = (self.tick + 1) & 0xff
        if self.tick == self.speed:
            self.tick = 0
            if self.patdelay:
                self.patdelay = (self.patdelay - 1) & 0xff
            else:
                self.newrow = 1
                self.row = (self.row + 1) & U32
                newpat = True
                if self.jumporder == U32:
                    if self.row < self.patrows:
                        newpat = False
                    else:
                        self.jumporder = (self.order + 1) & U32
                        self.jumprow = 0
                if newpat:
                    if self.jumporder != self.order:
                        pass                                # clears +0x63 / +0x64 of every channel (unused)
                    o = self.jumporder
                    if o >= m.ordnum:
                        o = m.restart
                    self.order = o
                    self.row = self.jumprow
                    self.jumporder = U32
                    pat = m.orders[self.order]
                    p = struct.unpack_from('<I', self.d, 0x350 + 4 * pat)[0]
                    self.patrows = struct.unpack_from('<I', self.d, p)[0]
                    p += 4
                    while self.jumprow:                     # skip rows (counts jumprow down to 0)
                        while True:
                            f = self.d[p]
                            p += 1
                            if f == 0:
                                break
                            if f & 0x20: p += 2
                            if f & 0x40: p += 1
                            if f & 0x80: p += 2
                        self.jumprow -= 1
                    self.patpos = p
                self.read_row()
        # 0x402018: effects of every tick, then the GF1 values
        for i in range(n):
            c = self.ch[i]
            self.fx(0x78 + c.volcmd, c)
            if c.eff < 0x34:
                self.fx(0x34 + c.eff, c)
            if c.insp:
                self.volume_env(c)
            c.gvol = self.volt[2 * c.fvol]
            c.gpan = c.fpan >> 4
            c.gfreq = ((self.freq(c.fpitch) * self.voices) // FREQ_DIV) & 0xfffe

    def read_row(self):
        d = self.d
        p = self.patpos
        for i in range(self.m.channels):
            c = self.ch[i]
            self.n_note = 0
            self.n_ins = self.n_vol = self.n_eff = self.n_par = 0
            c.eff = 0xff
            f = d[p]
            if f == 0 or (f & 0x1f) != i:                   # no entry for this channel: nothing else runs
                continue
            p += 1
            if f & 0x20:
                self.n_note, self.n_ins = d[p], d[p + 1]
                p += 2
            if f & 0x40:
                self.n_vol = d[p]
                p += 1
            if f & 0x80:
                self.n_eff, self.n_par = d[p], d[p + 1]
                p += 2
            self.n_flag24 = 0                               # +0x24 (never set to 1 in this build)
            self.new_note(c)
            v = self.n_vol
            self.n_vol &= 0x0f
            c.volcmd = v >> 4
            self.fx(0x68 + (v >> 4), c)
            e = self.n_eff
            if e < 0x34:
                c.eff = e
                self.fx(e, c)
        self.patpos = p + 1

    # --- 0x401a47 new note (row tick, per channel with an entry) ---
    def new_note(self, c):
        porta = 0                                           # +0x57
        if self.n_eff == 3 or self.n_eff == 5 or self.n_vol >= 0xf0:
            porta = 1
        if self.n_note == 0x61:                             # key off: no K00 and no "no envelope: cut" here
            c.keyon = 0
            self.n_note = 0
        if self.n_ins != 0 and self.n_ins <= self.m.insnum:
            c.ins = self.n_ins
        if c.ins == 0:
            return
        note = self.n_note
        if note != 0:
            if self.n_ins != 0:
                c.keyon = 1
            c.note = note
            if self.n_eff == 0x31 and self.n_par != 0:      # EDx (note delay): the note is dropped
                return
            note -= 1
            if porta:
                c.target = s16(0x3000 - ((note << 8) + c.rel))      # 0x401b84: only the target
            else:
                c.stop = 1
                insp = self.m.ins_ptr(c.ins)
                smp = self.d[insp + 4 + note]
                if smp >= struct.unpack_from('<I', self.d, insp)[0]:
                    return
                h = insp + 0x100 + 16 * smp
                c.active = 1
                c.newsmp = 1
                c.start = int.from_bytes(self.d[h:h + 3], 'little')
                c.loop = int.from_bytes(self.d[h + 3:h + 6], 'little')
                c.end = int.from_bytes(self.d[h + 6:h + 9], 'little')
                c.mode = self.d[h + 9]
                if self.n_ins != 0:
                    c.insp = insp
                    c.svol = self.d[h + 0xa]
                    c.span = self.d[h + 0xb]
                c.rel = struct.unpack_from('<H', self.d, h + 0xc)[0]
                p = s16(0x3000 - ((note << 8) + c.rel))
                c.pitch = c.fpitch = c.target = p
                ofs = 0
                if self.n_eff == 9:                         # 9xx: sample offset xx * 256 (memory)
                    if self.n_par:
                        c.ofsmem = self.n_par
                    ofs = c.ofsmem << 8
                c.offset = ofs
                c.vibpos = 0                                # +0x5c; also +0x60, +0x65, +0x6a, +0x70
                c.arpidx = 0
                c.rtcount = 0
        # 0x401b97
        if c.keyon and self.n_ins != 0:
            if self.n_flag24 != 1:
                c.vol = c.fvol = c.svol
                if not self.m.opt & 2:
                    c.pan = c.fpan = c.span
            c.fade = 0x8000
            c.envpt = 0
            c.envpos = 0

    # --- 0x40203c..0x4020e2: volume (master, global, fadeout, envelope) ---
    def volume_env(self, c):
        d = self.d
        ip = c.insp
        v = ((c.fvol * self.mastervol) >> 6) & 0xff
        v = (v * self.globalvol) & 0xffff
        v = (((v * c.fade) >> 16) >> 4) & 0xff
        c.fvol = v
        if not c.keyon:
            f = c.fade - struct.unpack_from('<H', d, ip + 0x64)[0]
            c.fade = f if f >= 0 else 0
        pt = c.envpt
        if c.envpos != 0 and (pt & 0xff) == d[ip + 0x6d]:
            pt = (pt & ~0xff) | d[ip + 0x6c]
            c.envpt = pt
        e = ip + 0x6e + 4 * pt
        if (pt & 0xff) == d[ip + 0x6a]:
            val = d[e + 2]
            c.fvol = ((val * c.fvol) >> 6) & 0xff
        else:
            ln, v0 = struct.unpack_from('<Hh', d, e)
            v1 = struct.unpack_from('<h', d, e + 6)[0]
            num, den = s16(v1 - v0) * s16(c.envpos), s16(ln)    # imul word / idiv word: truncates toward 0
            q = abs(num) // abs(den) * (1 if (num < 0) == (den < 0) else -1)
            val = (q + d[e + 2]) & 0xff
            c.fvol = ((val * c.fvol) >> 6) & 0xff
            pos = c.envpos
            if not (pos == 0 and c.keyon and (c.envpt & 0xff) == d[ip + 0x6b]):
                pos += 1
                if pos >= ln:
                    pos = 0
                    c.envpt += 1
            c.envpos = pos & 0xffff

    # --- effects: jump table 0x40274d (0x00..0x33 row, 0x34..0x67 every tick, 0x68..0x77 / 0x78..0x87 volume
    #     column on the row / every tick) ---
    def fx(self, idx, c):
        h = self.handlers[self.fxtab[idx]]
        if h is not None:
            self.stats[idx] = self.stats.get(idx, 0) + 1
            h(c)

    def fx_arp_row(self, c):                                # 0x401c6b: 0xy (0x00 also for "no effect")
        par = self.n_par
        if par == 0:
            c.eff = 0xff
        c.arp = [0, par >> 4, par & 15]

    def fx_arp_tick(self, c):                               # 0x401c88: every tick, the row tick too
        p = self.clamp_pitch(c.fpitch - (c.arp[c.arpidx] << 8))
        c.fpitch = p
        c.arpidx = (c.arpidx + 1) & 0xff
        if c.arpidx == 3:
            c.arpidx = 0

    def fx_pup_row(self, c):                                # 0x401cec: 1xx memory
        if self.n_par:
            c.pupspeed = self.n_par << 4

    def fx_pdn_row(self, c):                                # 0x401cfb: 2xx memory
        if self.n_par:
            c.pdnspeed = self.n_par << 4

    def fx_pup_tick(self, c):                               # 0x401d0a: not on the row tick
        if not self.newrow:
            c.pitch = c.fpitch = self.clamp_pitch(c.pitch - c.pupspeed)

    def fx_pdn_tick(self, c):                               # 0x401d22
        if not self.newrow:
            c.pitch = c.fpitch = self.clamp_pitch(c.pitch + c.pdnspeed)

    def fx_tp_row(self, c):                                 # 0x401d3a: 3xx memory
        if self.n_par:
            c.tpspeed = self.n_par << 4

    def fx_tp_tick(self, c):                                # 0x401d49: slide pitch to target, not on the row tick
        p = c.pitch
        if not self.newrow and p != c.target:
            if p > c.target:
                p -= c.tpspeed
                if p < c.target:
                    p = c.target
            else:
                p += c.tpspeed
                if p > c.target:
                    p = c.target
        c.pitch = c.fpitch = s32(p)

    def fx_vib_row(self, c):                                # 0x401d78: 4xy, x / y = 0 keep the memory
        par = self.n_par
        if par & 0x0f:
            c.vibdepth = ((par & 0x0f) << 2) & 0xff
        if par & 0xf0:
            c.vibspeed = (par & 0xf0) >> 2

    def fx_vib_tick(self, c):                               # 0x401d93: every tick, the position moves after row
        t = self.vibtab[c.vibpos & 0xff]                    # movsx eax, byte [table + pos]
        prod = (s8(t) * s8(c.vibdepth)) & 0xffff            # imul byte: AX only
        eax = ((t & U32) & 0xffff0000) | prod               # the upper half of EAX is the sign of the table byte
        if t >= 0:
            eax = prod
        delta = s32(eax) >> 3                               # sar eax, 3
        if delta != s16(prod) >> 3:
            self.stats['vibrato upper-half quirk'] = self.stats.get('vibrato upper-half quirk', 0) + 1
        c.fpitch = self.clamp_pitch(s32(delta + c.fpitch))
        if not self.newrow:
            c.vibpos = (c.vibpos + c.vibspeed) & 0xff

    def fx_vslide_row(self, c):                             # 0x401dbd: 5xy / 6xy / Axy memory
        if self.n_par:
            c.vslide = self.n_par

    def fx_vslide_tick(self, c):                            # 0x401dc8: Axy, not on the row tick
        bl = c.vslide
        al = c.vol
        if not self.newrow:
            if bl & 0xf0:
                al = min(al + (bl >> 4), 0x40)
            else:
                al = al - bl if al >= bl else 0
        c.vol = c.fvol = al

    def fx_pan(self, c):                                    # 0x401c51: 8xx
        c.pan = c.fpan = self.n_par

    def fx_vol(self, c):                                    # 0x401c41: Cxx
        c.vol = c.fvol = min(self.n_par, 0x40)

    def fx_speed(self, c):                                  # 0x401c22: Fxx
        par = self.n_par
        if par >= 0x20:
            self.period = TEMPO_K // par
        elif par:
            self.speed = par

    def fx_retrig_row(self, c):                             # 0x401df3: Rxy (x = volume change, y = interval)
        if self.n_par:
            c.rtint = self.n_par & 15
            c.rtvol = self.n_par >> 4
            c.rtcount = 0

    def fx_retrig_tick(self, c):                            # 0x401e0c: every tick, the row tick too
        al = c.rtcount
        c.rtcount = (c.rtcount + 1) & 0xff
        if s8(al) < s8(c.rtint):
            return
        c.rtcount = 0
        c.offset = 0                                        # restart the sample (no stop / ramp first)
        al = c.vol
        bl = c.rtvol
        ah = ((0x80 << (bl & 7)) | (0x80 >> (8 - (bl & 7)))) & 0xff if bl & 7 else 0x80   # rol ah, cl
        if not bl & 7:
            return
        if not bl & 8:
            if bl <= 5:
                al = (al - ah) & 0xff
            if bl == 6:
                al = ((al + (al >> 2)) & 0xff) >> 1
            if bl == 7:
                al >>= 1
        else:
            if bl <= 13:
                al = (al + ah) & 0xff
            if bl == 14:
                al = (al + (al >> 1)) & 0xff
            if bl == 15:
                al = (al << 1) & 0xff
                if al & 0x80:
                    al -= 1
        if s8(al) < 0:
            al = 0
        if al > 0x40:
            al = 0x40
        c.vol = c.fvol = al

    def vc_vol1(self, c):                                   # 0x401cbe: volume column 0x10..0x1f
        c.vol = c.fvol = self.n_vol

    def vc_vol2(self, c):
        self.n_vol = (self.n_vol + 0x10) & 0xff
        self.vc_vol1(c)

    def vc_vol3(self, c):
        self.n_vol = (self.n_vol + 0x10) & 0xff
        self.vc_vol2(c)

    def vc_vol4(self, c):
        self.n_vol = (self.n_vol + 0x10) & 0xff
        self.vc_vol3(c)

    def vc_vol5(self, c):                                   # 0x401cae: 0x50 (the nibble is replaced)
        self.n_vol = 0x10
        self.vc_vol4(c)

    def vc_finedn(self, c):                                 # 0x401cdb: 0x8x
        c.vol = c.fvol = c.vol - self.n_vol if c.vol >= self.n_vol else 0

    def vc_fineup(self, c):                                 # 0x401cc8: 0x9x
        c.vol = c.fvol = min((c.vol + self.n_vol) & 0xff, 0x40)

    def vc_pan(self, c):                                    # 0x401c5b: 0xCx
        c.pan = c.fpan = ((self.n_vol << 4) | self.n_vol) & 0xff


def load_module(a):
    import mxm
    if a.module:
        data = open(a.module, 'rb').read()
    else:
        import modsynth
        data = bytes(modsynth.build(modsynth.load_img(a.img)))
    return mxm.Module(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--img', default=os.path.join(RE, 'unpacked', 'clone_img.bin'))
    ap.add_argument('--module', help='module buffer (default: build it with modsynth.py from clone_img.bin)')
    ap.add_argument('--check', default=RE, help='directory with full.gus / full.dram / full.ticks ("" = none)')
    ap.add_argument('--out', help='write <out>.gus')
    ap.add_argument('--positions', help='write the music position per IRQ: log tick, order, row, timer')
    ap.add_argument('--play-tick', type=int, default=10, help='log tick of xmpPlay (main line)')
    ap.add_argument('--stop-tick', type=int, default=11723, help='log tick of xmpStop (main line, after that IRQ)')
    ap.add_argument('--real-volume-read', action='store_true')
    ap.add_argument('--wav', help='render through the GF1 model (implies --real-volume-read; needs numpy)')
    ap.add_argument('--stats', action='store_true', help='print how often each effect handler ran')
    a = ap.parse_args()

    img = open(a.img, 'rb').read()
    mod = load_module(a)
    real = a.real_volume_read or a.wav
    gus = GF1() if real else GusLogger()
    pl = Player(mod, img, gus)
    pl.init()
    if a.wav:
        gus.prepare_render()
    gus.tick = a.play_tick
    pl.play(0)
    positions = {}
    facc = 0.0
    ticks_run = 0
    for t in range(a.play_tick + 1, a.stop_tick + 1):
        gus.tick = t
        if pl.irq():
            ticks_run += 1
        positions[t] = (pl.order & 0xff, pl.row & 0xff, s32(pl.timer + pl.cur))
        if real:
            facc += gus.frame_rate() * pl.cur / PIT_HZ - gus.waited
            gus.waited = 0
            n = int(facc)
            facc -= n
            gus.run(n)
    gus.tick = a.stop_tick
    rate = gus.frame_rate() if real else None             # 617,400 / 18 voices (xmpStop resets to 14)
    pl.stop()
    print('module: %d orders, %d channels, %d instruments, speed %d, %d BPM -> tick period %d PIT clocks '
          '(%.4f Hz)' % (mod.ordnum, mod.channels, mod.insnum, mod.speed, mod.bpm, pl.period, PIT_HZ / pl.period))
    print('player ticks run: %d; last position %02x:%02x' % (ticks_run, pl.order, pl.row))
    if a.stats:
        for k in sorted(pl.stats, key=str):
            print('  handler %s: %d' % ('%#04x -> %06x' % (k, pl.fxtab[k]) if isinstance(k, int) else k,
                                        pl.stats[k]))
    if a.out:
        open(a.out + '.gus', 'w').write('\n'.join(gus.lines) + '\n')
    if a.positions:
        with open(a.positions, 'w') as f:
            for t in sorted(positions):
                f.write('%d %d %d %d\n' % ((t,) + positions[t]))
    if a.wav:
        import numpy as np
        import wave
        pcm = np.concatenate(gus.out)
        peak = np.abs(pcm).max()
        print('rendered %d frames at %.1f Hz (%.2f s); peak %.3f (1.0 = one voice at full scale and volume, hard '
              'panned)' % (len(pcm), rate, len(pcm) / rate, peak))
        with wave.open(a.wav, 'wb') as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(int(round(rate)))            # 34,300 Hz
            w.writeframes((np.clip(pcm / max(peak, 1e-9) * 0.9, -1, 1) * 32767).astype('<i2').tobytes())
    if a.check and not real:
        ok = True
        ref = open(os.path.join(a.check, 'full.dram'), 'rb').read()
        same = ref == bytes(gus.dram)
        print('DRAM: %s' % ('identical (1 MB)' if same else 'DIFFERS in %d bytes' % sum(
            1 for x, y in zip(ref, gus.dram) if x != y)))
        ok &= same
        ref = open(os.path.join(a.check, 'full.gus')).read().split('\n')
        if ref and ref[-1] == '':
            ref.pop()
        diff = next((i for i, (x, y) in enumerate(zip(ref, gus.lines)) if x != y), None)
        if diff is None and len(ref) == len(gus.lines):
            print('GUS log: %d lines, identical' % len(ref))
        else:
            ok = False
            i = diff if diff is not None else min(len(ref), len(gus.lines))
            print('GUS log: %d vs %d lines, first difference at line %d: %r vs %r' % (
                len(ref), len(gus.lines), i + 1, ref[i] if i < len(ref) else None,
                gus.lines[i] if i < len(gus.lines) else None))
        bad = []
        n = 0
        for line in open(os.path.join(a.check, 'full.ticks')):
            if line.startswith('#'):
                continue
            f = line.split()
            t = int(f[0])
            if t in positions:
                n += 1
                if (int(f[3]), int(f[4]), int(f[5])) != positions[t]:
                    bad.append(t)
        print('music position and timer: %d ticks compared, %d differ%s' % (
            n, len(bad), (' (first at tick %d)' % bad[0]) if bad else ''))
        ok &= not bad
        print('ALL MATCH' if ok else 'MISMATCH')


if __name__ == '__main__':
    main()

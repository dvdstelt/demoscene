"""Reference implementation of JIZZ.EXE's music: MXMPlay (Niklas Beisert's GUS XM player, code 0x296dc..0x2a316,
in the cut-down build the intro links), mirroring the assembly routine by routine, plus a GF1 model to render it.

From the module (dump/smokeout.mxm, or mp4synth.py's output) and jizz_img.bin (frequency tables) it produces
  - the GUS DRAM image (xmpInit's upload 0x2a26f)            -> compare with full.dram
  - the GF1 register-write log, in run_jizz.py's text format  -> compare with full.gus
  - the music position (order, row) after every IRQ           -> compare with full.ticks
and with --wav renders the song through the GF1 model (44,100 Hz: 14 active voices).

usage:  python3 mxmplay.py [--module ../../dump/smokeout.mxm | --synth] [--check ../..] [--out PREFIX]
                           [--real-volume-read] [--wav out.wav]

Volume reads: setvol 0x2a0b6 reads the voice's current volume (reg 0x89) and ramps from it; the capture's fake
GUS answers with the last value written to reg 9. That is the default here (to match full.gus).
--real-volume-read (implied by --wav) answers from the GF1 model, as on hardware; then the IRQ handler's busy wait
for the volume ramps (0x2a17f) also takes GF1 time, and the register stream differs from full.gus.

Routine map (see notes-music.md):
  0x296dc data block pointer   0x296e7 volume table   0x29738 xmpInit   0x2a26f sample upload
  0x29db1 xmpPlay  0x29e22 xmpStop  0x29ed1 timer install  0x29f2a timer remove  0x29e38 IRQ0 handler
  0x29f44 xmpGetTimer  0x299c4 xmpGetPos  0x299ae xmpSetVolume (unused)  0x299b9 xmpGetSync (unused)
  0x2a13c GUS update (per IRQ)  0x29b26 player tick  0x29801 new note  0x297a8 pitch -> frequency
  0x2a02a GF1 reset  0x2a23f GUS start  0x2a25e GUS stop  0x2a0b6 set volume  0x2a11c voice off (ramp down)
  effects: jump table 0x2a3f7 (0x88 entries: 0x00..0x33 row effects, 0x34..0x67 tick effects,
  0x68..0x77 volume column on the row, 0x78..0x87 volume column per tick)
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)

PIT_HZ = 1193182
TEMPO_K = 0x2d86bf          # PIT clocks per tick * BPM (2,983,615 = 2.5 * 1193446; 151 BPM -> 19759)
FREQ_DIV = 0x1ee            # FC = freq * voices / 494, bit 0 cleared
FT0, FT1, FT2, FT3 = 0x2a357, 0x2a377, 0x2a397, 0x2a3b7   # frequency tables (16 x u16, u16, u16, u32)
GF1_CLOCK = 617400.0

U32 = 0xffffffff


def s32(v):
    v &= U32
    return v - (1 << 32) if v & 0x80000000 else v


def s16(v):
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


class GusLogger:
    """The GUS as run_jizz.py's Gus class sees it: logs register writes as text ("tick vN rXX val",
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

    def select(self, v):                                    # out base+0x102 (not logged)
        self.voice = v & 31

    def _w(self, reg, val):
        self.regs[(self.voice, reg)] = val
        self.lines.append('%d v%d r%02x %x' % (self.tick, self.voice, reg, val))

    def w8(self, reg, val):                                 # 0x29f6b: reg -> base+0x103, byte -> base+0x105
        self._w(reg, val & 0xff)

    def w16(self, reg, val):                                # 0x29fa5: reg -> base+0x103, word -> base+0x104
        self._w(reg, val & 0xffff)

    def r8(self, reg):                                      # 0x29fbb: byte from base+0x105
        v = self.regs.get((self.voice, reg - 0x80), 0)
        if reg in (0x80, 0x8d):
            v |= 1
        if reg in (0x81, 0x82, 0x83, 0x84, 0x85, 0x89, 0x8a, 0x8b):
            return (v >> 8) & 0xff
        return v & 0xff

    def r16(self, reg):                                     # 0x29fd2: word from base+0x104
        v = self.regs.get((self.voice, reg - 0x80), 0)
        if reg in (0x80, 0x8d):
            v |= 1
        return v & 0xffff

    def poke(self, addr, b):                                # DRAM write (regs 0x43/0x44 + base+0x107, not logged)
        self.dram[addr & 0xfffff] = b

    def wait_ramps(self, voices):                           # the busy wait 0x2a17f: no time passes here
        pass


class GF1(GusLogger):
    """GF1 synthesiser model: what the card does with the writes (it also logs them like GusLogger).

    Per voice: positions in 1/512 byte (address << 9 | fraction; start / end keep a 4-bit fraction), increment
    FC >> 1 per frame (FC bit 0 unused); 12-bit log volume kept in 1/512 steps; ramps; pan 0..15. One frame
    computes every active voice once: 617400 / active frames per second (14 voices: 44,100 Hz). Per frame and
    voice: out += sample(pos) * gain(vol) * pan law, then the position and the ramp advance. Stopped voices keep
    being mixed at their parked position. 8-bit samples, linear interpolation between DRAM[a] and DRAM[a + 1].
    Loops: forward (bit 3) and bidirectional (bits 3 + 4, direction bit 6), as DOSBox-X models them.
    Sources: GUS SDK 2.10 ULTRADOC, DOSBox-X gus.cpp (see paper/re/notes-music.md section 7)."""

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
        """0x2a17f polls reg 0x8d of every channel until bit 0 (ramp stopped) is set; the GF1 runs meanwhile."""
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
        self.dram_f = np.frombuffer(bytes(self.dram) + b'\0', dtype=np.int8).astype(np.float64) / 128.0
        e = np.arange(4096) >> 8
        m = np.arange(4096) & 255
        self.gain = (256 + m) * np.exp2(e - 24.0)            # GF1 log volume: (256 + mantissa) << exponent
        self.out = []

    def render(self, n):
        """n frames of stereo output (1.0 = one voice at full scale and gain 1, hard panned)"""
        np = self.np
        L = np.zeros(n)
        R = np.zeros(n)
        for i in range(self.active):
            s = self.v[i]
            pos, vol = self.voice_frames(s, n, True)
            a = (pos >> 9) & 0xfffff
            f = (pos & 511).astype(np.float64) / 512.0
            smp = self.dram_f[a] + (self.dram_f[a + 1] - self.dram_f[a]) * f
            g = smp * self.gain[vol >> 9]
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
            if arrays:
                k = np.arange(n + 1, dtype=np.int64)
            else:
                k = None
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
    """channel record, 0x100 bytes at data + 0x9ac + 0x100 * ch (offsets in the comments)"""

    def __init__(self):
        self.active = 0       # +0x00 voice has a sample
        self.start = 0        # +0x01 u24 DRAM start
        self.end = 0          # +0x05 u24 DRAM end
        self.loop = 0         # +0x09 u24 DRAM loop start
        self.mode = 0         # +0x0d GF1 voice control (loop / bidi / 16 bit)
        self.stop = 0         # +0x0e stop the voice before restarting it
        self.newsmp = 0       # +0x0f write loop start / end
        self.offset = 0       # +0x10 restart at start + offset (-1 = keep playing)
        self.gfreq = 0        # +0x14 GF1 FC
        self.gvol = 0         # +0x16 GF1 volume (16 bit)
        self.gpan = 0         # +0x18 GF1 pan 0..15
        self.vol = 0          # +0x19 volume 0..64
        self.fvol = 0         # +0x1a volume of this tick (then 0..128 after the envelope / fade / master)
        self.pan = 0          # +0x1b panning 0..255
        self.fpan = 0         # +0x1c panning of this tick
        self.pitch = 0        # +0x1d s32 pitch (1/256 semitone, 0x3000 - note * 256 - relnote; lower = higher)
        self.fpitch = 0       # +0x21 pitch of this tick
        self.ins = 0          # +0x25 instrument number
        self.insp = 0         # +0x26 instrument pointer (module offset here; 0 = none)
        self.rel = 0          # +0x2a u16 relative note * 256 + finetune of the sample
        self.keyon = 0        # +0x2c 1 until key off
        self.fade = 0         # +0x2d u16 fadeout level (0x8000 = full)
        self.envpt = 0        # +0x31 volume envelope point
        self.envpos = 0       # +0x35 u16 ticks into that point
        self.svol = 0         # +0x3d sample volume
        self.span = 0         # +0x3e sample panning
        self.eff = 0xff       # +0x3f effect of this row (0xff = none)
        self.volcmd = 0       # +0x40 volume column command (high nibble)
        self.target = 0       # +0x41 tone portamento target pitch (written, never used by this build)
        self.vslide = 0       # +0x49 volume slide memory
        self.fpup = 0         # +0x57 fine porta up memory
        self.fpdn = 0         # +0x58 fine porta down memory
        self.retrig = 0       # +0x69 retrigger interval (read by E9x per tick, never written: E9x does nothing)
        self.note = 0         # +0x6d last note


class Player:
    def __init__(self, mod, img, gus, port=0x240):
        self.m = mod
        self.d = mod.data
        self.g = gus
        self.port = port                                    # +0x50 (0 = no sound)
        self.ft = (struct.unpack_from('<16H', img, FT0), struct.unpack_from('<16H', img, FT1),
                   struct.unpack_from('<16H', img, FT2), struct.unpack_from('<16I', img, FT3))
        # data block (zeroed by xmpInit)
        self.globalvol = 0      # +0x00
        self.mastervol = 0x40   # +0x01 (xmpInit; xmpSetVolume 0x299ae would change it, never called)
        self.sync = 0           # +0x02 (read by 0x299b9, only ever written 0)
        self.tick = 0           # +0x03
        self.speed = 0          # +0x04
        self.newrow = 0         # +0x05
        self.row = 0            # +0x06 u32
        self.patpos = 0         # +0x0a pattern data pointer
        self.patrows = 0        # +0x0e
        self.order = 0          # +0x12
        self.jumporder = 0      # +0x16 (0xffffffff = none)
        self.jumprow = 0        # +0x1a
        self.patdelay = 0       # +0x1e
        self.timer = 0          # +0x3a
        self.cur = 0            # +0x36 current PIT period
        self.period = 0         # +0x42 tick period in PIT clocks
        self.remain = 0         # +0x46
        self.voices = 0         # +0x54 active GF1 voices
        self.ch = [Chan() for _ in range(32)]
        self.volt = self.voltab()

    # --- 0x296e7: volume table, 257 words: linear 0..256 -> GF1 log volume ---
    @staticmethod
    def voltab():
        t = [0] * 257
        t[0x100] = 0xefff
        for i in range(1, 256):
            e = 7 + i.bit_length()
            t[i] = ((((i << (20 - e)) & 0xfff) | (e << 12)) - 0x1000) & 0xffff
        return t

    # --- 0x29738 xmpInit: (header copy and pointer fix-up), sample upload 0x2a26f, volume table ---
    def init(self):
        m = self.m
        n = m.smp16 * 2 + m.smp8
        if self.port:
            src = m.smpstart
            for a in range((n >> 4) << 4):                  # 16 bytes per block (with interrupts off)
                self.g.poke(a, self.d[src + a])

    # --- 0x2a02a GF1 reset (voices = max(channels, 14)) ---
    def gf1_reset(self, nch):
        g = self.g
        self.voices = nch if (nch & 0xff) >= 14 else (nch & ~0xff) | 14     # cmp al, 0xe; jae; mov al, 0xe
        g.w8(0x4c, 0)                                       # (each followed by two 7-read delays)
        g.w8(0x4c, 1)
        g.w8(0x0e, ((self.voices - 1) | 0xc0) & 0xff)
        for v in range(32):
            g.select(v)
            g.w16(0x09, 0)
            g.w8(0x00, 3); g.w8(0x00, 3)                    # 0x29f83: write, delay, write again
            g.w8(0x0d, 3); g.w8(0x0d, 3)
            g.w8(0x06, 0x3f)
        g.w8(0x4c, 7)
        g.select(0)
        g.port(0, 8)

    # --- 0x29db1 xmpPlay(order) ---
    def play(self, order=0):
        m = self.m
        self.jumporder = order
        self.order = order
        self.row = 0
        if self.port:                                       # 0x2a23f
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
        # 0x29ed1 timer install
        self.remain = 0
        self.cur = 0x10000
        self.timer = (0xffff0000 - self.period) & U32
        self.busy = 0

    # --- 0x29e22 xmpStop ---
    def stop(self):
        if self.port:                                       # 0x2a25e
            self.gf1_reset(14)

    # --- 0x29e38 IRQ0 ---
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
        """0x29f44: PIT clocks since the music started (timer + clocks elapsed in the current PIT period)"""
        return s32(self.timer + elapsed)

    def get_pos(self):
        """0x299c4: order << 8 | row"""
        return ((self.order & 0xff) << 8) | (self.row & 0xff)

    # --- 0x2a13c GUS update (start of every IRQ tick, before the player) ---
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
        g.wait_ramps(n)                                     # 0x2a17f: until bit 0 of reg 0x8d, every channel
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

    def waddr(self, reg, a):                                # 0x2a00b
        self.g.w16(reg, (a >> 7) & 0x1fff)
        self.g.w16(reg + 1, (a << 9) & 0xffff)

    def voice_off(self):                                    # 0x2a11c: ramp down to 0x04xx
        g = self.g
        g.w8(0x07, 0x04)
        g.w8(0x08, 0xfc)
        g.w8(0x0d, 0x40); g.w8(0x0d, 0x40)

    def setvol(self, target):                               # 0x2a0b6
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

    # --- 0x297a8: pitch -> frequency (2^(-pitch/3072) via 4 nibble tables) ---
    def freq(self, pitch):
        x = (pitch + 0x8000) & U32
        t0, t1, t2, t3 = self.ft
        f = t3[(x >> 12) & 15]
        f = ((f * t2[(x >> 8) & 15]) >> 15) & U32
        f = ((f * t1[(x >> 4) & 15]) >> 15) & U32
        f = ((f * t0[x & 15]) >> 15) & U32
        return f

    # --- 0x29b26 player tick ---
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
        # 0x29ca0: effects of every tick, then the GF1 values
        for i in range(n):
            c = self.ch[i]
            self.fx(0x78 + c.volcmd, c)
            if c.eff < 0x34:
                self.fx(0x34 + c.eff, c)
            if c.insp:
                self.volume_env(c)
            c.gvol = self.volt[2 * c.fvol]                  # fvol <= 0x80 here (64 * 64 * 0x8000 >> 20)
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
            if f != 0 and (f & 0x1f) == i:
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

    # --- 0x29801 new note (row tick, per channel) ---
    def new_note(self, c):
        porta = 0
        keyoff = 0
        if self.n_eff == 3 or self.n_eff == 5 or self.n_vol >= 0xf0:
            porta = 1
        if self.n_eff == 0x14 and self.n_par == 0:
            keyoff = 1
            c.keyon = 0
        elif self.n_note == 0x61:
            self.n_note = 0
            keyoff = 1
            c.keyon = 0
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
                c.target = s16(0x3000 - ((note << 8) + c.rel))
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
                c.offset = 0
        # 0x2994c
        if c.keyon and self.n_ins != 0:
            c.vol = c.fvol = c.svol
            if not self.m.opt & 2:
                c.pan = c.fpan = c.span
            c.fade = 0x8000
            c.envpt = 0
            c.envpos = 0
        if keyoff and self.n_ins == 0:
            if self.d[c.insp + 0x6a] == 0:                  # no volume envelope: key off cuts
                c.fade = 0

    # --- 0x29cc4..0x29d6d: volume (master, global, fadeout, envelope) ---
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

    # --- effects: jump table 0x2a3f7 ---
    def fx(self, idx, c):
        par = self.n_par
        if idx in (0x05, 0x06, 0x0a):                       # 0x29ac0: volume slide memory
            if par:
                c.vslide = par
        elif idx == 0x08:                                   # 0x29a4a: 8xx set panning
            c.pan = c.fpan = par
        elif idx == 0x0c:                                   # 0x29a3a: Cxx set volume (max 0x40)
            c.vol = c.fvol = min(par, 0x40)
        elif idx == 0x0d:                                   # 0x299f2: Dxx pattern break (decimal row)
            if self.jumporder == U32:
                self.jumporder = (self.order + 1) & U32
            self.jumprow = (par >> 4) * 10 + (par & 15)
        elif idx == 0x0f:                                   # 0x29a1b: Fxx speed (< 0x20) / tempo
            if par >= 0x20:
                self.period = TEMPO_K // par
            elif par:
                self.speed = par
        elif idx == 0x25:                                   # 0x29a64: E1x fine porta up
            if par:
                c.fpup = par
            p = self.clamp_pitch(c.pitch - (c.fpup << 4))
            c.pitch = c.fpitch = p
        elif idx == 0x26:                                   # 0x29a86: E2x fine porta down
            if par:
                c.fpdn = par
            p = self.clamp_pitch(c.pitch + (c.fpdn << 4))
            c.pitch = c.fpitch = p
        elif idx == 0x32:                                   # 0x29a14: EEx pattern delay
            self.patdelay = par
        elif idx == 0x3e:                                   # 0x29acb: Axy volume slide, every tick but the row tick
            bl = c.vslide
            al = c.vol
            if not self.newrow:
                if bl & 0xf0:
                    al = min(al + (bl >> 4), 0x40)
                else:
                    al = al - bl if al >= bl else 0
            c.vol = c.fvol = al
        elif idx == 0x61:                                   # 0x29af6: E9x retrigger (interval never set: no-op)
            if c.retrig and self.tick % c.retrig == 0:
                c.offset = 0
        elif 0x69 <= idx <= 0x6d:                           # volume column 0x10..0x50: set volume
            c.vol = c.fvol = 0x40 if idx == 0x6d else (idx - 0x69) * 0x10 + self.n_vol
        elif idx == 0x74:                                   # volume column 0xCx: set panning x * 0x11
            c.pan = c.fpan = (self.n_vol << 4) | self.n_vol
        # everything else: 0x299f1 (ret)

    def clamp_pitch(self, p):                               # 0x299d4
        if p <= self.m.lowpitch:
            p = self.m.lowpitch
        if p >= self.m.highpitch:
            p = self.m.highpitch
        return p


def load_module(a):
    import mxm
    if a.synth:
        import mp4synth
        img = open(a.img, 'rb').read()
        data = mp4synth.expand(img)
        data += bytes(0xe2b9c - len(data))                  # the zeroed buffer behind the generated samples
    else:
        data = open(a.module, 'rb').read()
    return mxm.Module(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--img', default=os.path.join(RE, 'jizz_img.bin'))
    ap.add_argument('--module', default=os.path.join(RE, 'dump', 'smokeout.mxm'))
    ap.add_argument('--synth', action='store_true', help='build the module with mp4synth.py instead of reading it')
    ap.add_argument('--check', default=RE, help='directory with full.gus / full.dram / full.ticks ("" = none)')
    ap.add_argument('--out', help='write <out>.gus')
    ap.add_argument('--play-tick', type=int, default=433, help='log tick of xmpPlay (main line)')
    ap.add_argument('--stop-tick', type=int, default=9356, help='log tick of xmpStop (main line, after that IRQ)')
    ap.add_argument('--real-volume-read', action='store_true')
    ap.add_argument('--wav', help='render through the GF1 model (implies --real-volume-read; needs numpy)')
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
        positions[t] = (pl.order, pl.row)
        if real:
            facc += gus.frame_rate() * pl.cur / PIT_HZ - gus.waited
            gus.waited = 0
            n = int(facc)
            facc -= n
            gus.run(n)
    gus.tick = a.stop_tick
    pl.stop()
    print('module: %d orders, %d channels, %d instruments, speed %d, %d BPM -> tick period %d PIT clocks '
          '(%.4f Hz)' % (mod.ordnum, mod.channels, mod.insnum, mod.speed, mod.bpm, pl.period, PIT_HZ / pl.period))
    print('player ticks run: %d; last position %02x:%02x' % (ticks_run, pl.order, pl.row))
    if a.out:
        open(a.out + '.gus', 'w').write('\n'.join(gus.lines) + '\n')
    if a.wav:
        import numpy as np
        import wave
        pcm = np.concatenate(gus.out)
        peak = np.abs(pcm).max()
        rate = gus.frame_rate()
        print('rendered %d frames at %.1f Hz (%.2f s); peak %.3f (1.0 = one voice at full scale and volume, hard '
              'panned)' % (len(pcm), rate, len(pcm) / rate, peak))
        with wave.open(a.wav, 'wb') as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(int(round(rate)))
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
                if (int(f[3]), int(f[4])) != positions[t]:
                    bad.append(t)
        print('music position: %d ticks compared, %d differ%s' % (n, len(bad),
                                                                  (' (first at tick %d)' % bad[0]) if bad else ''))
        ok &= not bad
        print('ALL MATCH' if ok else 'MISMATCH')


if __name__ == '__main__':
    main()

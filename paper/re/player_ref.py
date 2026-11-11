"""Reference implementation of PAPER.EXE's music (Statix / Vic of Acme, Wired '96): instrument upload,
pattern unpacker, tempo accumulator and player, written to mirror the assembly one-to-one, plus a GF1 model.

From paper_img.bin alone it regenerates
  - the GUS DRAM image (code32 0x1d5b..0x1e1d)                    -> compare with full.dram (first 34944 bytes)
  - the GF1 register-write log of a full run, in the exact text format of run_paper.py's Gus class
    ("tick vN rXX val" / "tick pPPP val")                          -> compare with full.gus
  - the music counter [0x1ea8b] after each tick's IRQ             -> compare with full.ticks column 2
and optionally renders the music through a GF1 model (--wav, needs numpy).

usage:  python3 player_ref.py [--img paper_img.bin] [--check DIR | --check ''] [--out PREFIX]
        [--music-start 104] [--music-stop 12235] [--real-volume-read] [--wav out.wav]

Code32 routine map (see notes-music.md for the full spec):
  0x1c24 sound init   0x2256 ULTRASND detection   0x22f8 GF1 init / 0x1c8e sound off
  0x1ca4 period table 0x1cbe pattern unpack       0x1d5b sample copy   0x1dd8 DRAM upload
  0x1e45 IRQ0 (tempo accumulator [0x1e5fb] / [0x1e5ff])   0x1e1e set tempo
  0x1ed6 player: row (0x1ee3..0x1fb9) + per-tick effects (0x1fbf..0x20f1)
  0x20f2 trigger   0x21d9 frequency   0x2205 start sample   0x2112 volume (ramp)

Volume reads: the volume routine 0x2112 reads the voice's current volume (reg 0x89) and ramps from it.
run_paper.py's fake GUS answers that read with the last value *written* to reg 9 (always 0 after init), so the
logged run always "ramps up from 0". By default this file answers the read the same way (to match full.gus).
With --real-volume-read (implied by --wav) the read is answered by the GF1 model, as on real hardware: volume
decreases then become ramp-downs and unchanged volumes write nothing. The remake must do the latter.
"""
import argparse
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
C32 = 0xd00                 # code32 = image offset 0xd00

# ---- static data (code32 offsets) ----
PAN_TAB = 0x7a8b            # 32 bytes, voice v pan = byte[0x7a8b + v] & 15 (voices 16..31 read into the header)
TUNE = 0x7a9b               # u8 channels (10), u16 rows (1472), u8 instruments (31), then channel-major events
VOL_TAB = 0x1e49c           # 65 bytes: channel volume 0..64 -> GF1 volume high byte (bits 11..4 of the 12-bit volume)
INIT_SPEED = 0x1e4e5        # u8 ticks per row (6)
INIT_COUNT = 0x1e4e6        # u8 tick countdown (1)
PERIOD_SEED = 0x1e4eb       # 16 u16: Amiga periods * 8 (856*8 = 6848 ... 453*8 = 3624), then 4 zeros

PIT_DIV = 0x4295            # IRQ0 divisor: 1193182 / 17045 = 70.0018 Hz
PIT_HZ = 1193182.0 / PIT_DIV
TEMPO_K = 0x2d8426          # 2982950 = 2.5 * 1193180: player tick = TEMPO_K // bpm PIT clocks (bpm 125 -> 23863)
FREQ_K = 0x5adc             # 23260: FC = (c2spd * 23260 // period) >> 11  (tuned for 16 active voices)
GF1_CLOCK = 617400.0        # GF1 frame rate = 617400 / active voices (14..32)


class Tune:
    """Everything the intro derives from the exe at start-up (0x1ca4..0x1e1d)."""

    def __init__(self, seg):
        self.nch = seg[TUNE]
        self.nrows = struct.unpack_from('<H', seg, TUNE + 1)[0]
        self.nins = seg[TUNE + 3]
        # period table (0x1ca4): 16 seed words, then 0x70 more, each = the one 16 entries earlier >> 1
        per = list(struct.unpack_from('<16H', seg, PERIOD_SEED))
        for i in range(0x70):
            per.append(per[i] >> 1)
        self.period = per                                  # index = note byte (octave * 16 + semitone)
        self.voltab = seg[VOL_TAB:VOL_TAB + 65]
        # pattern unpack (0x1cbe): channel-major stream, one flag byte per row and channel
        p = TUNE + 4
        ev = [[None] * self.nch for _ in range(self.nrows)]
        for ch in range(self.nch):
            for r in range(self.nrows):
                note, ins, eff, par = 0, 0, 0, 0                 # defaults: dword 0x00ff0000, byte 4 = 0
                f = seg[p]; p += 1
                vol = f & 0x3f
                if vol == 0x3f: vol = 0x40
                if vol == 2: vol = 0xff                          # 2 = "no volume" (0x3f = 64)
                if f & 0x40:
                    b = seg[p]; b2 = seg[p + 1]; p += 2
                    note = b & 0x7f
                    ins = ((b >> 7) << 4) + (b2 & 0x0f)
                    eff = b2 >> 4
                if f & 0x80:
                    par = seg[p]; p += 1
                ev[r][ch] = (note, ins, vol, eff, par)
        self.events = ev
        self.pattern_end = p
        # instruments (0x1d5b): records of 0x14 bytes at 0x1e60b + 0x14*(i-1):
        #   +0 length, +4 DRAM start, +8 DRAM loop start (0 = no loop), +0xc DRAM end, +0x10 u16 c2spd
        buf = bytearray(b'\x80' * 8)
        ebp = 8
        self.ins = {}
        for i in range(1, self.nins + 1):
            L = struct.unpack_from('<h', seg, p)[0]
            P = p + 2
            if L == 0:
                p = P
                continue
            if L > 0:                                      # one-shot: u16 c2spd, L bytes
                length = L
                c2spd = struct.unpack_from('<H', seg, P)[0]
                rec = {'len': length, 'start': ebp, 'loop': 0, 'end': ebp + length, 'c2spd': c2spd, 'src': P + 2}
                buf += seg[P + 2:P + 2 + length]
                buf += seg[P + length + 1:P + length + 3]       # pad: last sample byte + the next header's 1st byte
                p = P + length + 2
            else:                                          # looped: u16 loopofs, u16 length, u16 c2spd, data
                loopofs, length, c2spd = struct.unpack_from('<HHH', seg, P)
                rec = {'len': length, 'start': ebp, 'loop': ebp + loopofs, 'end': ebp + length, 'c2spd': c2spd,
                       'src': P + 6}
                buf += seg[P + 6:P + 6 + length]
                buf += seg[P + 6 + loopofs:P + 8 + loopofs]     # pad: the two bytes at the loop start
                p = P + (-L) + 6                                # stream advance uses |L| (= length + 2)
            ebp += length + 2
            self.ins[i] = rec
        self.sample_end = p
        self.dram = bytes(b ^ 0x80 for b in buf)          # upload xors 0x80 (unsigned -> signed), DRAM address 0


class GusLogger:
    """The GUS as run_paper.py's Gus class sees it: logs writes as text, answers reads the same way.
    w8 = register select 0x343 + byte to 0x345 (data high); w16 = register select + word to 0x344."""

    def __init__(self, base=0x240):
        self.base = base
        self.voice = 0
        self.regs = {}
        self.lines = []
        self.tick = 0

    def port(self, port, val):                             # writes to ports other than 0x342..0x347
        self.lines.append('%d p%03x %x' % (self.tick, port, val))

    def select(self, v):                                   # out 0x342 (not logged)
        self.voice = v & 31

    def _w(self, reg, val):
        self.regs[(self.voice, reg)] = val
        if reg not in (0x43, 0x44):
            self.lines.append('%d v%d r%02x %x' % (self.tick, self.voice, reg, val))

    def w8(self, reg, val):
        self._w(reg, val & 0xff)

    def w16(self, reg, val):
        self._w(reg, val & 0xffff)

    def r16(self, reg):                                    # register select + word from 0x344
        v = self.regs.get((self.voice, reg - 0x80), 0)
        if reg in (0x80, 0x8d): v |= 1
        return v & 0xffff


class GF1(GusLogger):
    """GF1 synthesizer model (what real hardware does with the same writes). Still logs like GusLogger.

    Per voice: wave position/start/end in 1/512 sample (20.9 fixed point), FC (reg 1) -> increment FC >> 1 per
    frame; 12-bit volume with ramps (kept in 1/512 units of a 12-bit step); pan 0..15. One frame = every active
    voice computed once; frame rate 617400 / active voices. Per frame and voice (order as in DOSBox-X):
      out += sample(pos) * gain(vol) * pan;  then advance pos (if running) and vol (if ramping).
    Stopped voices are still mixed (the GF1 always plays DRAM[pos] * volume)."""

    def __init__(self, dram):
        super().__init__()
        self.dram = bytearray(dram) + bytearray((1 << 20) - len(dram) + 2)
        self.latch = 0
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

    def r16(self, reg):
        s = self.v[self.voice]
        if reg == 0x89:
            return ((s['vol'] >> 9) << 4) & 0xffff
        return 0

    def hw(self, reg, d):
        s = self.v[self.voice]
        if reg == 0x4c:
            if not d & 0x100: self.reset()
            self.dac = bool(d & 0x200)
        elif reg == 0x0e: self.active = max(14, ((d >> 8) & 31) + 1)
        elif reg == 0x00: s['ctl'] = (d >> 8) & 0x7f
        elif reg == 0x01: s['fc'] = d
        elif reg in (0x02, 0x04, 0x0a):
            k = {0x02: 'start', 0x04: 'end', 0x0a: 'pos'}[reg]
            s[k] = (s[k] & 0xffff) | ((d & 0x1fff) << 16)
        elif reg in (0x03, 0x05, 0x0b):
            k = {0x03: 'start', 0x05: 'end', 0x0b: 'pos'}[reg]
            s[k] = (s[k] & ~0xffff) | d
        elif reg == 0x06: s['rate'] = d >> 8
        elif reg == 0x07: s['lo'] = ((d >> 8) << 4) << 9
        elif reg == 0x08: s['hi'] = ((d >> 8) << 4) << 9
        elif reg == 0x09: s['vol'] = (d >> 4) << 9
        elif reg == 0x0c: s['pan'] = (d >> 8) & 15
        elif reg == 0x0d: s['vctl'] = (d >> 8) & 0x7f

    def frame_rate(self):
        return GF1_CLOCK / self.active

    def advance(self, n, out=None):
        """Run n frames; if out = [L, R] (list of numpy arrays of length n), mix the active voices into them."""
        for i in range(self.active):
            s = self.v[i]
            if out is not None and self.dac:
                self._render(s, n, out)
            # wave position
            if not s['ctl'] & 3 and n:
                inc = s['fc'] >> 1
                assert not s['ctl'] & 0x50, 'backward / bidirectional play not modelled (unused by the intro)'
                p = s['pos'] + n * inc
                if p > s['end']:
                    L = s['end'] - s['start']
                    if s['ctl'] & 8 and L > 0:
                        p = s['start'] + (p - s['end']) % L
                    else:
                        p = s['end']; s['ctl'] |= 1
                s['pos'] = p
            # volume ramp
            if not s['vctl'] & 3 and n:
                assert not s['vctl'] & 0x18, 'volume-ramp loops not modelled (unused by the intro)'
                inc = ((s['rate'] & 63) * 512) >> (3 * (s['rate'] >> 6))
                if s['vctl'] & 0x40:
                    s['vol'] -= n * inc
                    if s['vol'] <= s['lo']: s['vol'] = s['lo']; s['vctl'] |= 1
                else:
                    s['vol'] += n * inc
                    if s['vol'] >= s['hi']: s['vol'] = s['hi']; s['vctl'] |= 1

    def _render(self, s, n, out):
        import numpy as np
        k = np.arange(n, dtype=np.int64)
        if not s['ctl'] & 3:
            p = s['pos'] + k * (s['fc'] >> 1)
            over = p > s['end']
            if over.any():
                L = s['end'] - s['start']
                if s['ctl'] & 8 and L > 0:
                    p[over] = s['start'] + (p[over] - s['end']) % L
                else:
                    p[over] = s['end']
        else:
            p = np.full(n, s['pos'], dtype=np.int64)
        a = (p >> 9) & 0xfffff
        f = (p & 511).astype(np.float64) / 512.0
        d = self.dram_i8
        smp = d[a] + (d[a + 1] - d[a]) * f
        if not s['vctl'] & 3:
            inc = ((s['rate'] & 63) * 512) >> (3 * (s['rate'] >> 6))
            if s['vctl'] & 0x40:
                v = np.maximum(s['vol'] - k * inc, s['lo'])
            else:
                v = np.minimum(s['vol'] + k * inc, s['hi'])
            v[0] = s['vol']
        else:
            v = np.full(n, s['vol'], dtype=np.int64)
        g = smp * self.gain[v >> 9]
        out[0] += g * ((15 - s['pan']) / 15.0)
        out[1] += g * (s['pan'] / 15.0)

    def prepare_render(self):
        import numpy as np
        self.dram_i8 = np.frombuffer(bytes(self.dram), dtype=np.int8).astype(np.float64) / 128.0
        e = np.arange(4096) >> 8
        m = np.arange(4096) & 255
        self.gain = (256 + m) * np.exp2(e - 24.0)            # GF1 log volume: (256 + mantissa) << exponent


class Player:
    """State and code of 0x1e45 (music part) / 0x1ed6 and helpers."""

    def __init__(self, tune, gus, seg):
        self.t = tune
        self.g = gus
        self.seg = bytearray(seg)                          # code32 memory as the player sees it
        self.speed = seg[INIT_SPEED]                       # [0x1e4e5]
        self.count = seg[INIT_COUNT]                       # [0x1e4e6]
        self.row = 0                                       # [0x1ea8b] (0 in the image, -1 after init)
        self.acc = 0                                       # [0x1e5fb]
        self.tempo = 0                                     # [0x1e5ff]
        self.ofs = 0                                       # [0x1c20] sample offset (effect 0xd: param << 8)
        # channel records at 0x1e88b, 16 bytes: +0 period, +4 target period, +8 instrument, +0xc volume,
        # +0xe portamento speed memory, +0xf volume slide memory
        self.ch = [{'per': 0, 'tgt': 0, 'ins': 0, 'vol': 0, 'pspd': 0, 'vsl': 0} for _ in range(tune.nch)]
        self.on = False                                    # [0x1ea93]

    # --- 0x22f8 GF1 init (also the sound-off path 0x1c8e) ---
    def gf1_init(self):
        g = self.g
        g.port(g.base, 9)                                  # mix control: line in off, output on, latches on
        g.w8(0x4c, 0)                                      # reset (each after 256 dummy reads of 0x343)
        g.w8(0x4c, 1)
        g.w8(0x41, 0)                                      # DRAM DMA control
        g.w8(0x45, 0)                                      # timer control
        g.w8(0x49, 0)                                      # sampling control
        g.w8(0x0e, 0xcf)                                   # 16 active voices -> 617400 / 16 = 38587.5 Hz
        for cl in range(32, 0, -1):
            g.select(cl - 1)
            g.w8(0x00, 3)                                  # voice stopped
            g.w8(0x0d, 3)                                  # ramp stopped
            g.w8(0x06, 0x3f)
            g.w8(0x09, 0x3f)                               # byte to 0x345: volume 0x3fxx ...
            g.w8(0x01, 0)                                  # byte to 0x345: frequency high byte 0
            g.w16(0x09, 0)                                 # ... then word 0: volume 0
            g.w8(0x0c, self.seg[PAN_TAB + cl - 1] & 0x0f)  # pan
        g.w8(0x4c, 3)                                      # run, DAC on (no GF1 IRQs)

    # --- 0x1e1e ---
    def set_tempo(self, bpm):
        self.tempo = TEMPO_K // bpm                       # (div by 0 if bpm = 0; not in the tune)

    # --- 0x1c24 (GUS found) ---
    def sound_init(self):
        self.gf1_init()
        self.row = -1
        self.set_tempo(0x7d)
        # the upload loop counts the instrument count byte [0x7a9e] down to 0 in place; that byte is also the
        # pan of voice 19 (0x7a8b + 19), so the GF1 re-init of 0x1c8e at exit writes pan 0 instead of 15 there
        self.seg[TUNE + 3] = 0

    # --- 0x1e45, music part ---
    def irq(self):
        if not self.on:
            return
        self.acc -= PIT_DIV
        while self.acc <= 0:
            self.play()
            self.acc += self.tempo

    # --- 0x21d9 ---
    def set_freq(self, c, per):
        c2 = self.t.ins[c['ins']]['c2spd'] if c['ins'] else struct.unpack_from('<H', self.seg, 0x10)[0]
        if per == 0:
            per = 1
        q = (c2 * FREQ_K) // per                           # idiv; both operands positive here
        self.g.w16(0x01, q >> 11)

    # --- 0x2205 ---
    def start_sample(self, c):
        r = self.t.ins[c['ins']]
        g = self.g
        g.w8(0x00, 3)
        g.w8(0x00, 3)
        a = (r['start'] + self.ofs) & 0xffffffff
        self.ofs = 0
        for reg, val in ((0x0a, a), (0x02, r['loop']), (0x04, r['end'])):
            g.w16(reg, val >> 7)                           # address bits 19..7
            g.w16(reg + 1, val << 9)                       # address bits 6..0 in bits 15..9, fraction 0
        g.w8(0x00, 8 if r['loop'] else 0)                  # forward, 8-bit, loop on/off, no IRQ

    # --- 0x2112 ---
    def set_volume(self, vol):
        g = self.g
        bh = self.seg[VOL_TAB + vol]
        if bh <= 5: bh = 5
        elif bh >= 0xfa: bh = 0xfa
        g.w8(0x06, 0)
        g.w8(0x0d, 3)
        bl = (g.r16(0x89) >> 8) & 0xff
        if bl == bh:
            return
        if bl < bh:
            g.w8(0x07, bl); g.w8(0x08, bh); g.w8(0x0d, 0x00)
        else:
            g.w8(0x07, bh); g.w8(0x08, bh); g.w8(0x0d, 0x40)
        g.w8(0x06, 0x30)

    # --- 0x20f2 ---
    def trigger(self, c):
        self.set_freq(c, c['per'])
        self.start_sample(c)
        self.set_volume(c['vol'])

    # --- 0x1ed6 ---
    def play(self):
        t = self.t
        self.count = (self.count - 1) & 0xff
        if not (0 < self.count < 0x80):                    # 'dec byte; jg': signed byte > 0
            self.count = self.speed
            self.row += 1
            if (self.row & 0xffff) >= t.nrows:             # 'cmp si, word [0x7a9c]; jb'
                self.row = 0
            evs = t.events[self.row]
            for k in range(t.nch):
                c = self.ch[k]
                self.g.select(t.nch - 1 - k)               # channel k -> voice 9 - k
                note, ins, vol, eff, par = evs[k]
                trig = False
                if note:
                    trig = True
                    c['tgt'] = t.period[note]
                if ins:
                    c['ins'] = ins
                    trig = True
                if eff:
                    if eff == 0xa: self.speed = par
                    if eff == 9: self.set_tempo(par)
                    if eff == 0xd: self.ofs = par << 8
                    if eff in (3, 8): trig = False
                if trig:
                    c['per'] = c['tgt']
                    self.trigger(c)
                if vol != 0xff:
                    c['vol'] = vol
                    self.set_volume(vol)
        # per-tick effects (also on the row tick)
        evs = t.events[self.row]
        for k in range(t.nch):
            c = self.ch[k]
            self.g.select(t.nch - 1 - k)
            note, ins, vol, eff, par = evs[k]
            if eff in (1, 2, 3):
                if eff == 1: c['tgt'] = 0
                if eff == 2: c['tgt'] = 0x7ffff
                if self.count == 3:                         # no slide while the countdown is 3
                    continue
                spd = par or c['pspd']
                c['pspd'] = spd
                per = c['per']
                if per == c['tgt']:
                    continue
                if per > c['tgt']:
                    per = max(per - spd, c['tgt'])
                else:
                    per = min(per + spd, c['tgt'])
                c['per'] = per
                self.set_freq(c, per)
            elif eff == 4:                                  # volume slide: only the low nibble works (bug)
                spd = par or c['vsl']
                c['vsl'] = spd
                v = c['vol'] - (spd & 0x0f)
                if v < 0: v = 0
                c['vol'] = v
                self.set_volume(v)
            elif eff == 8:                                  # note delay: trigger when countdown - 1 == param
                if ((self.count - 1) & 0xff) == par:
                    c['per'] = c['tgt']
                    self.trigger(c)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--img', default=os.path.join(HERE, 'paper_img.bin'))
    ap.add_argument('--check', default=HERE, help='directory with full.dram / full.gus / full.ticks ("" = none)')
    ap.add_argument('--out', help='write <out>.gus, <out>.dram, <out>.ticks')
    ap.add_argument('--music-start', type=int, default=104, help='first tick whose IRQ sees [0x1ea93] = 1 '
                    '(main sets it after the precalc; 104 in the 1.5M-instructions-per-tick run)')
    ap.add_argument('--music-stop', type=int, default=12235, help='tick at which the intro exits (0x7a12: '
                    '[0x1ea93] = 0, 0x1c8e sound off)')
    ap.add_argument('--real-volume-read', action='store_true')
    ap.add_argument('--wav', help='render through the GF1 model (implies --real-volume-read; needs numpy)')
    a = ap.parse_args()

    seg = open(a.img, 'rb').read()[C32:]
    tune = Tune(seg)
    real = a.real_volume_read or a.wav
    gus = GF1(tune.dram) if real else GusLogger()
    pl = Player(tune, gus, seg)
    ticks = []
    mix = []
    if a.wav:
        import numpy as np
        gus.prepare_render()
    facc = 0.0
    gus.tick = 0
    pl.sound_init()                                         # tick 0: 0x1c24 runs before the first IRQ
    for t in range(0, a.music_stop):
        gus.tick = t
        if t == a.music_start:
            pl.on = True
        pl.irq()                                            # IRQ0 at the end of tick t
        ticks.append(pl.row)
        if real:                                            # frames until the next IRQ
            facc += gus.frame_rate() / PIT_HZ
            n = int(facc)
            facc -= n
            if a.wav:
                buf = [np.zeros(n), np.zeros(n)]
                gus.advance(n, buf)
                mix.append(np.stack(buf, 1))
            else:
                gus.advance(n)
    gus.tick = a.music_stop
    pl.on = False
    pl.gf1_init()                                           # 0x1c8e: PIT back to 18.2 Hz, GF1 re-init

    print('tune: %d channels, %d rows, %d instrument slots; patterns 0x%x..0x%x, samples ..0x%x (code32)' % (
        tune.nch, tune.nrows, tune.nins, TUNE, tune.pattern_end, tune.sample_end))
    print('DRAM image: %d bytes' % len(tune.dram))
    if a.out:
        open(a.out + '.dram', 'wb').write(tune.dram + bytes((1 << 20) - len(tune.dram)))
        open(a.out + '.gus', 'w').write('\n'.join(gus.lines) + '\n')
        open(a.out + '.ticks', 'w').write(''.join('%d %d\n' % (t, r) for t, r in enumerate(ticks)))
    if a.wav:
        import wave
        pcm = np.concatenate(mix)
        peak = np.abs(pcm).max()
        print('rendered %d frames at %.1f Hz; peak %.3f (1.0 = one voice at full scale, full volume, hard pan)'
              % (len(pcm), gus.frame_rate(), peak))
        with wave.open(a.wav, 'wb') as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(int(gus.frame_rate()))          # 38587 (true rate 38587.5)
            w.writeframes((pcm / peak * 0.9 * 32767).astype('<i2').tobytes())
    if a.check and not real:
        ok = True
        ref = open(os.path.join(a.check, 'full.dram'), 'rb').read()
        n = len(tune.dram)
        same = ref[:n] == tune.dram
        rest0 = not any(ref[n:])
        print('DRAM: first %d bytes %s; rest of the 1 MB %s' % (n, 'identical' if same else 'DIFFER',
              'all zero' if rest0 else 'NOT zero'))
        ok &= same and rest0
        ref = open(os.path.join(a.check, 'full.gus')).read().split('\n')
        if ref and ref[-1] == '': ref.pop()
        diff = next((i for i, (x, y) in enumerate(zip(ref, gus.lines)) if x != y), None)
        if diff is None and len(ref) == len(gus.lines):
            print('GUS log: %d lines, identical' % len(ref))
        else:
            ok = False
            i = diff if diff is not None else min(len(ref), len(gus.lines))
            print('GUS log: %d vs %d lines, first difference at line %d: %r vs %r' % (
                len(ref), len(gus.lines), i + 1, ref[i] if i < len(ref) else None,
                gus.lines[i] if i < len(gus.lines) else None))
        rt = {}
        for line in open(os.path.join(a.check, 'full.ticks')):
            f = line.split()
            rt[int(f[0])] = int(f[1])
        common = [t for t in rt if t < len(ticks)]
        bad = [t for t in common if rt[t] != ticks[t]]
        print('music counter: %d ticks compared, %d differ%s' % (
            len(common), len(bad), (' (first at tick %d)' % bad[0]) if bad else ''))
        ok &= not bad
        print('ALL MATCH' if ok else 'MISMATCH')


if __name__ == '__main__':
    main()

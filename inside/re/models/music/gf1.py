"""GF1 model for rendering: clone/re/models/music/mxmplay.py's class GF1 (itself JIZZ's model), copied here and put
behind dsmi.Card's interface, so that the DSMI driver port runs against a card that answers its reads as hardware
does:
  - reg 0x89 (current volume) returns the ramped volume, so a ramp starts where the voice is;
  - reg 0x8d (volume control) has bit 0 set only when the ramp has stopped; the driver's wait 0x1152f50 polls the
    selected voice at most 20 times (about 40 us on the ISA bus, one GF1 frame at 25,725 Hz): wait_voice() lets the
    card run one frame when the ramp is still going;
  - reg 0x41 reads the DMA as finished.
Positions in 1/512 address units (address << 9 | fraction), increment FC >> 1 per frame; 12-bit log volume kept in
1/512 steps; ramps; pan 0..15 (L = (15 - pan) / 15, R = pan / 15). One frame computes every active voice once:
617,400 / active frames per second (24 voices: 25,725 Hz). 8-bit voices read DRAM[a] (signed); linear interpolation
between a and a + 1. Forward loops (bit 3), one-shots stop at the end. Stopped voices keep being mixed at their
parked position (their volume is at 0x1770 or below, -87 dB, after the driver stops them).
"""
import numpy as np

import dsmi

GF1_CLOCK = 617400.0


class GF1(dsmi.Card):
    def __init__(self, base=0x220):
        super().__init__(base)
        self.latch = 0
        self.out = None
        self.waited = 0
        self.log_enabled = False
        self.reset()
        e = np.arange(4096) >> 8
        m = np.arange(4096) & 255
        self.gain = (256 + m) * np.exp2(e - 24.0)            # GF1 log volume: (256 + mantissa) << exponent

    def reset(self):
        self.active = 14
        self.dac = False
        self.v = [{'ctl': 3, 'fc': 0, 'start': 0, 'end': 0, 'pos': 0, 'vctl': 3, 'rate': 0, 'lo': 0, 'hi': 0,
                   'vol': 0, 'pan': 7} for _ in range(32)]

    # ---- the driver's port helpers
    def port(self, p, val):
        if self.log_enabled:
            super().port(p, val)

    def _write(self, reg, val):
        if self.log_enabled:
            super()._write(reg, val)

    def w8(self, reg, val):
        self._write(reg, val & 0xff)
        self.hw(reg, ((val & 0xff) << 8) | self.latch)

    def w16(self, reg, val):
        self._write(reg, val & 0xffff)
        self.latch = val & 0xff
        self.hw(reg, val & 0xffff)

    def r8(self, reg):
        s = self.v[self.voice]
        if reg == 0x80:
            return s['ctl']
        if reg == 0x8d:
            return s['vctl']
        if reg == 0x41:
            return 0x40
        return 0

    def r16(self, reg):
        s = self.v[self.voice]
        if reg == 0x89:
            return ((s['vol'] >> 9) << 4) & 0xffff
        return 0

    def wait_voice(self):
        """0x1152f50: at most 20 reads of reg 0x8d of the selected voice (about one GF1 frame)."""
        if not self.v[self.voice]['vctl'] & 1 and self.ramp_inc(self.v[self.voice]):
            self.run(1)
            self.waited += 1

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

    def run(self, n):
        if n <= 0:
            return
        if self.out is not None:
            self.out.append(self.render(n))
        else:
            for i in range(self.active):
                self.voice_frames(self.v[i], n, False)

    def prepare_render(self):
        d = bytes(self.dram)
        self.dram8 = np.frombuffer(d + b'\0', dtype=np.int8).astype(np.float64) / 128.0
        self.out = []

    def render(self, n):
        """n frames of stereo output (1.0 = one voice at full scale and gain 1, hard panned)"""
        L = np.zeros(n)
        R = np.zeros(n)
        for i in range(self.active):
            s = self.v[i]
            pos, vol = self.voice_frames(s, n, True)
            a = (pos >> 9) & 0xfffff
            f = (pos & 511).astype(np.float64) / 512.0
            s0 = self.dram8[a]
            s1 = self.dram8[(a + 1) & 0xfffff]
            g = (s0 + (s1 - s0) * f) * self.gain[vol >> 9]
            L += g * ((15 - s['pan']) / 15.0)
            R += g * (s['pan'] / 15.0)
        out = np.stack([L, R], 1)
        return out if self.dac else out * 0

    def voice_frames(self, s, n, arrays):
        """advance one voice by n frames; with arrays=True return the position and volume of each frame
        (forward loops and one-shots; the bidirectional / backwards cases of clone's model are not reached)"""
        pos = vol = None
        ctl, p0, inc = s['ctl'], s['pos'], s['fc'] >> 1
        st, en = s['start'], s['end']
        if ctl & 3 or inc == 0:
            if arrays:
                pos = np.full(n, p0, dtype=np.int64)
        else:
            Lp = en - st
            k = np.arange(n + 1, dtype=np.int64) if arrays else None
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
        v0 = s['vol']
        inc = self.ramp_inc(s)
        if not s['vctl'] & 3 and inc:
            if s['vctl'] & 0x40:
                if arrays:
                    vol = np.maximum(v0 - np.arange(n, dtype=np.int64) * inc, s['lo'])
                v = v0 - n * inc
                if v <= s['lo']:
                    v = s['lo']
                    s['vctl'] |= 1
            else:
                if arrays:
                    vol = np.minimum(v0 + np.arange(n, dtype=np.int64) * inc, s['hi'])
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

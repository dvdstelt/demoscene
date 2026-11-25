"""Render VIVID.EXE's music to a WAV: the MIDAS port (midas.py) and the timer (player.py) drive a GF1 model that
answers the device's reads as a real card does (reg 0x89: the ramped volume; reg 0x8d: bit 0 only when the ramp has
stopped; the device's busy wait for the ramps of starting voices lets the card run one frame per pass).

usage (inside the toolbox):
  python3 render.py full  runs/full.wav     [--rate 44100] [--all]
  python3 render.py part2 runs/part2.wav

The GF1 model follows inside/re/models/music/gf1.py (itself clone's mxmplay.py) with bidirectional loops added the way
inside/remake/src/gf1.cpp does them (an unfolded coordinate of period 2 x loop length): positions in 1/512 address
units, increment FC >> 1 per card frame, 12-bit log volume (256 + mantissa) * 2^(exponent - 24) kept in 1/512 steps,
ramps of (rate & 63) * 512 >> (3 * (rate >> 6)) per frame, pan L = (15 - p) / 15, R = p / 15, linear interpolation
between DRAM[a] and DRAM[a + 1] (signed bytes). One card frame computes every active voice once: 617,400 / active
voices frames per second (16 voices in part I: 38,587.5 Hz; 20 in part II: 30,870 Hz).

By default the WAV starts at the first record of the capture that has a script line (frames2mp4.py's default start);
--all starts at program time 0. Each part is rendered at the card's rate and resampled linearly to --rate.
"""
import argparse
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import midas  # noqa: E402
import player  # noqa: E402

GF1_CLOCK = 617400.0


class GF1:
    def __init__(self, dram):
        self.dram = dram
        self.latch = 0
        self.active = 14
        self.dac = False
        self.v = [dict(ctl=3, fc=0, start=0, end=0, pos=0, vctl=3, rate=0, lo=0, hi=0, vol=0, pan=7)
                  for _ in range(32)]
        e = np.arange(4096) >> 8
        m = np.arange(4096) & 255
        self.gain = (256 + m) * np.exp2(e - 24.0)
        self.voice = 0
        self.chunks = []          # (time in s, rate, stereo float32 array)
        self.time = None          # card time in s of the next frame
        self.waited = 0
        self.d8 = None

    # ---- what the device writes (midas.Gf1Port listener) and reads (answers)
    def write(self, voice, reg, val, size):
        self.voice = voice
        if size == 1:
            d = ((val & 0xff) << 8) | self.latch
        else:
            self.latch = val & 0xff
            d = val & 0xffff
        s = self.v[voice]
        if reg == 0x4c:
            if not d & 0x100:
                self.active = 14
                self.v = [dict(ctl=3, fc=0, start=0, end=0, pos=0, vctl=3, rate=0, lo=0, hi=0, vol=0, pan=7)
                          for _ in range(32)]
            self.dac = bool(d & 0x200)
        elif reg == 0x0e:
            self.active = max(14, ((d >> 8) & 31) + 1)
            self.d8 = None                       # the samples of the next module have been uploaded by now
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

    def port(self, p, val):
        pass

    def read(self, voice, reg):
        s = self.v[voice]
        if reg == 0x80:
            return s['ctl']
        if reg == 0x8d:
            return s['vctl']
        if reg == 0x89:
            return ((s['vol'] >> 9) << 4) & 0xffff
        return 0

    def wait(self):
        self.run(1)
        self.waited += 1

    # ---- time
    def rate(self):
        return GF1_CLOCK / self.active

    def advance_to(self, t):
        if self.time is None:
            self.time = t
            return
        n = int((t - self.time) * self.rate())
        if n > 0:
            self.run(n)

    def run(self, n):
        if self.d8 is None:
            self.d8 = np.frombuffer(bytes(self.dram) + b'\0', dtype=np.int8).astype(np.float32) / 128.0
        L = np.zeros(n, dtype=np.float32)
        R = np.zeros(n, dtype=np.float32)
        for i in range(self.active):
            s = self.v[i]
            pos, vol = self.voice_frames(s, n)
            if isinstance(pos, int) and isinstance(vol, int):
                a = (pos >> 9) & 0xfffff
                f = (pos & 511) / 512.0
                x = self.d8[a] + (self.d8[(a + 1) & 0xfffff] - self.d8[a]) * f
                g = np.float32(x * self.gain[min(max(vol >> 9, 0), 4095)])
            else:
                pos = np.broadcast_to(np.asarray(pos, dtype=np.int64), (n,))
                vol = np.broadcast_to(np.asarray(vol, dtype=np.int64), (n,))
                a = (pos >> 9) & 0xfffff
                f = (pos & 511).astype(np.float32) / 512.0
                s0 = self.d8[a]
                s1 = self.d8[(a + 1) & 0xfffff]
                g = (s0 + (s1 - s0) * f) * self.gain[np.clip(vol >> 9, 0, 4095)].astype(np.float32)
            L += g * ((15 - s['pan']) / 15.0)
            R += g * (s['pan'] / 15.0)
        out = np.stack([L, R], 1)
        if not self.dac:
            out *= 0
        self.chunks.append((self.time, self.rate(), out))
        self.time += n / self.rate()

    @staticmethod
    def voice_frames(s, n):
        """advance one voice n frames; returns the position and volume of each frame (ints when constant)"""
        ctl, p0, inc = s['ctl'], s['pos'], s['fc'] >> 1
        st, en = s['start'], s['end']
        ln = en - st
        if ctl & 3 or inc == 0:
            pos = p0
        else:
            k = np.arange(n, dtype=np.int64)
            if (ctl & 0x18) == 0x18 and ln > 0:
                per = 2 * ln
                u0 = st + per - (p0 - st) if (ctl & 0x40) and st <= p0 <= en else p0
                u = u0 + k * inc
                m = np.where(u <= en, u - st, (u - st) % per)
                pos = np.where(m > ln, st + per - m, st + m)
                un = u0 + n * inc
                if un <= en:
                    s['pos'] = un
                    s['ctl'] = ctl & ~0x40
                else:
                    mm = (un - st) % per
                    back = mm > ln
                    s['pos'] = st + per - mm if back else st + mm
                    s['ctl'] = (ctl & ~0x40) | (0x40 if back else 0)
            elif ctl & 0x40:
                pp = p0 - k * inc
                if ctl & 8 and ln > 0:
                    pos = np.where(pp < st, en - (st - pp) % ln, pp)
                else:
                    pos = np.maximum(pp, st)
                p = p0 - n * inc
                if p < st:
                    if ctl & 8 and ln > 0:
                        p = en - (st - p) % ln
                    else:
                        p = st
                        s['ctl'] = ctl | 1
                s['pos'] = p
            else:
                pp = p0 + k * inc
                if ctl & 8 and ln > 0:
                    pos = np.where(pp > en, st + (pp - en) % ln, pp)
                else:
                    pos = np.minimum(pp, en)
                p = p0 + n * inc
                if p > en:
                    if ctl & 8 and ln > 0:
                        p = st + (p - en) % ln
                    else:
                        p = en
                        s['ctl'] = ctl | 1
                s['pos'] = p
        v0 = s['vol']
        vinc = ((s['rate'] & 63) * 512) >> (3 * (s['rate'] >> 6))
        if not s['vctl'] & 3 and vinc:
            k = np.arange(n, dtype=np.int64)
            if s['vctl'] & 0x40:
                vol = np.maximum(v0 - k * vinc, s['lo'])
                v = v0 - n * vinc
                if v <= s['lo']:
                    v = s['lo']
                    s['vctl'] |= 1
            else:
                vol = np.minimum(v0 + k * vinc, s['hi'])
                v = v0 + n * vinc
                if v >= s['hi']:
                    v = s['hi']
                    s['vctl'] |= 1
            s['vol'] = v
        else:
            vol = v0
        return pos, vol


class RenderRun(player.Run):
    def __init__(self, which):
        super().__init__(which)
        self.gf1 = GF1(self.sd.dram)
        self.port.answers = self.gf1
        self.port.listeners.append(self.gf1)

    def player_tick(self, irq, t):
        self.gf1.advance_to(t / player.PIT_HZ)
        super().player_tick(irq, t)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('which', choices=sorted(player.RUNS))
    ap.add_argument('out')
    ap.add_argument('--rate', type=int, default=44100)
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--ticks', help='the capture .ticks (for the start record); default ../../<which>.ticks')
    a = ap.parse_args()
    r = RenderRun(a.which)
    r.go()
    g = r.gf1
    print('register writes with a card\'s answers: %d; passes of the ramp wait: %d' % (len(r.port.log), g.waited))
    t0 = 0.0
    if not a.all:
        path = a.ticks or os.path.join(midas.RE, a.which + '.ticks')
        for line in open(path):
            if line.startswith('#'):
                continue
            f = line.split()
            if int(f[6]) >= 0:
                t0 = (int(f[0]) + 1) * player.FRAME / player.PIT_HZ
                break
    t_end = g.time
    n = int((t_end - t0) * a.rate)
    out = np.zeros((n, 2), dtype=np.float32)
    tt = t0 + np.arange(n) / a.rate
    for (ts, fr, x) in g.chunks:
        te = ts + len(x) / fr
        i0 = max(0, int(np.ceil((ts - t0) * a.rate)))
        i1 = min(n, int(np.ceil((te - t0) * a.rate)))
        if i1 <= i0:
            continue
        src = (tt[i0:i1] - ts) * fr
        out[i0:i1, 0] = np.interp(src, np.arange(len(x)), x[:, 0])
        out[i0:i1, 1] = np.interp(src, np.arange(len(x)), x[:, 1])
    peak = float(np.abs(out).max()) or 1.0
    print('peak %.3f of one full-scale voice; normalised to 0.9; %.2f s from program time %.3f s'
          % (peak, n / a.rate, t0))
    pcm = np.clip(out / peak * 0.9 * 32767, -32768, 32767).astype('<i2')
    with open(a.out, 'wb') as f:
        data = pcm.tobytes()
        f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' +
                struct.pack('<IHHIIHH', 16, 1, 2, a.rate, a.rate * 4, 4, 16) + b'data' + struct.pack('<I', len(data)))
        f.write(data)
    print('wrote', a.out)


if __name__ == '__main__':
    main()

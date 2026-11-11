"""Offline Gravis Ultrasound (GF1) synthesis from run_paper.py's register log and DRAM dump.

Replays the logged voice-register writes tick by tick (70.0 Hz, the intro's PIT rate) against the
sample memory the intro uploaded, and mixes the voices the way the GF1 does: linear interpolation,
12-bit logarithmic volume with ramps, 4-bit pan, output rate 617400 / active voices.

usage: python3 gf1synth.py full.gus full.dram out.wav [--rate 44100]
"""
import argparse
import wave

import numpy as np

PIT_HZ = 1193182.0 / 0x4295


def vol2amp(v):
    """12-bit GF1 volume (exponent 4 bits, mantissa 8 bits) to linear gain, 0xfff ~ 1.0"""
    v = np.clip(v, 0, 4095)
    e = np.floor(v / 256.0)
    m = v - e * 256.0
    return (256.0 + m) / 512.0 * np.exp2(e - 15.0)


def addr(hi, lo, frac_bits):
    """GF1 address registers: hi bits 12..0 = address bits 19..7, lo bits 15..9 = bits 6..0, fraction below"""
    a = ((hi & 0x1fff) << 7) | ((lo >> 9) & 0x7f)
    f = (lo >> (9 - frac_bits)) & ((1 << frac_bits) - 1)
    return a + f / float(1 << frac_bits)


class Voice:
    def __init__(self):
        self.r = {}
        self.ctrl = 3          # stopped
        self.vctrl = 3         # ramp stopped
        self.pos = 0.0
        self.vol = 0.0
        self.pan = 7

    def start(self): return addr(self.r.get(2, 0), self.r.get(3, 0), 4)
    def end(self): return addr(self.r.get(4, 0), self.r.get(5, 0), 4)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('dram')
    ap.add_argument('out')
    a = ap.parse_args()
    dram = np.frombuffer(open(a.dram, 'rb').read(), dtype=np.int8).astype(np.float32) / 128.0
    events = {}
    last = 0
    for line in open(a.log):
        t, what, *rest = line.split()
        t = int(t)
        last = max(last, t)
        events.setdefault(t, []).append((what, rest))
    voices = [Voice() for _ in range(32)]
    active = 14
    out = []
    acc = 0.0
    for t in range(last + 200):
        for what, rest in events.get(t, []):
            if what[0] != 'v':
                continue
            v = voices[int(what[1:])]
            reg = int(rest[0][1:], 16)
            val = int(rest[1], 16)
            v.r[reg] = val
            if reg == 0x00:
                v.ctrl = val
            elif reg == 0x0d:
                v.vctrl = val
            elif reg == 0x09:
                v.vol = float((val >> 4) & 0xfff)
            elif reg == 0x0a or reg == 0x0b:
                v.pos = addr(v.r.get(0x0a, 0), v.r.get(0x0b, 0), 9)
            elif reg == 0x0c:
                v.pan = val & 15
            elif reg == 0x0e:
                active = (val & 0x1f) + 1
        rate = 617400.0 / max(14, active)
        acc += rate / PIT_HZ
        n = int(acc)
        acc -= n
        mixL = np.zeros(n, np.float32)
        mixR = np.zeros(n, np.float32)
        k = np.arange(n, dtype=np.float64)
        for vi in range(active):
            v = voices[vi]
            # volume (with ramp)
            vols = np.full(n, v.vol)
            if not (v.vctrl & 3):
                rr = v.r.get(0x06, 0)
                step = (rr & 0x3f) / float(8 ** (rr >> 6))       # volume units per output sample
                lo = float((v.r.get(0x07, 0) & 0xff) << 4)
                hi = float((v.r.get(0x08, 0) & 0xff) << 4)
                if v.vctrl & 0x40:                                # decreasing
                    vols = np.maximum(v.vol - step * (k + 1), lo)
                    if vols[-1] <= lo: v.vctrl |= 1
                else:
                    vols = np.minimum(v.vol + step * (k + 1), hi)
                    if vols[-1] >= hi: v.vctrl |= 1
                v.vol = float(vols[-1])
            if v.ctrl & 3:
                continue
            inc = (v.r.get(0x01, 0) >> 1) / 512.0
            s, e = v.start(), v.end()
            if v.ctrl & 0x40:
                inc = -inc
            idx = v.pos + inc * k
            alive = np.ones(n, bool)
            L = e - s
            if v.ctrl & 0x08 and L > 0:                           # loop
                if v.ctrl & 0x10:                                 # bidirectional
                    over = idx > e
                    t2 = np.mod(idx[over] - s, 2 * L)
                    idx[over] = s + np.where(t2 < L, t2, 2 * L - t2)
                else:
                    over = idx >= e
                    idx[over] = s + np.mod(idx[over] - e, L)
                    under = idx < s
                    idx[under] = e - np.mod(s - idx[under], L)
            else:
                alive = (idx < e) if inc >= 0 else (idx > s)
                if not alive.all():
                    v.ctrl |= 1
            i0 = np.clip(np.floor(idx).astype(np.int64), 0, len(dram) - 2)
            f = (idx - np.floor(idx)).astype(np.float32)
            smp = dram[i0] * (1 - f) + dram[i0 + 1] * f
            smp = smp * vol2amp(vols).astype(np.float32) * alive
            mixL += smp * (15 - v.pan) / 15.0
            mixR += smp * v.pan / 15.0
            v.pos = float(idx[-1] + inc) if n else v.pos
        out.append(np.stack([mixL, mixR], 1))
    pcm = np.concatenate(out)
    pcm = pcm / max(1e-6, np.abs(pcm).max()) * 0.9
    with wave.open(a.out, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(int(round(617400.0 / max(14, active))))
        w.writeframes((pcm * 32767).astype(np.int16).tobytes())
    print('wrote', a.out, len(pcm), 'samples at', 617400.0 / max(14, active))


if __name__ == '__main__':
    main()

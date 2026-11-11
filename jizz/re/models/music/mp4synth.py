"""Model of JIZZ.EXE's "mp4 cruncher" (0x1632b, Probe): the module expander that runs at "extracting music".

It is not a decompressor: the first 32,625 bytes of the module (0x1f644.., MXM header, patterns, instruments and
14,037 bytes of stored samples) are copied as they are, and 896,032 bytes of samples are *synthesised* from 12
instrument records (0x16423, 0x7b bytes each): 32 passes of 16 notes, each a sawtooth / shaped / noise oscillator
through a resonant filter, then a feedback echo, then decimated by 2. See notes-music.md section 2.

All x87 maths runs with the precision control at 24 bits (main 0x18712), rounding to nearest: every arithmetic
result is rounded to a 24-bit mantissa (exponent range is not an issue here). Loads (fld qword, fild) are exact.
This model rounds exactly like that: products and sums of two 24-bit values are exact in double and then rounded
to float32 (double rounding is innocuous for 24-bit operands); operations with a full 53-bit double operand use an
fma-based correction (mul24x) or exact fractions (cached).

usage:  python3 mp4synth.py [--img ../../jizz_img.bin] [--out x.mxm] [--check ../../dump/smokeout.mxm]
"""
import argparse
import math
import os
import struct
import sys
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))

SRC, SRC_LEN = 0x1f644, 0x7f71          # module as stored (copied to [0x6546] by main 0x18aaa)
REC, REC_LEN = 0x16423, 0x7b            # 12 synth records
ORDER = 0x163f3                         # 12 dwords: record number + 1, in output order
K3529 = 0x15dc6                         # qword 3529.0 (samples per note - 1)
K102 = 0x1605f                          # qword 1.02 (second oscillator detune)
K32767 = 0x16015                        # qword 32767.0
NSEG, SEGLEN = 16, 0xdc9                # 16 notes of 0xdc9 + 1 = 3530 samples
GLIDE = 0x6e4                           # glide length in samples (pitch moves 2 units of k per sample / 3529)
ECHO_BACK, ECHO_WRAP = 0x295b, 0xb335   # echo delay 3 * 3529 back; first 3 notes read 13 * 3529 ahead
HALVE_LEN, DECIM_LAST, COPY_LEN = 0xe290, 0x6d60, 0x1b58 * 4

_F = struct.Struct('<f')


def f32(x):
    """round a double to 24-bit mantissa, nearest even (the x87 with PC = single)"""
    return _F.unpack(_F.pack(x))[0]


def mul24x(x, y):
    """x * y rounded once to 24 bits, for any doubles x, y (fma decides the double-rounding midpoints)"""
    p = x * y
    f = f32(p)
    if f == p:
        return f
    # find the float32 neighbour of f in the direction of p
    if p > f:
        g = _next32(f, 1)
    else:
        g = _next32(f, -1)
    if p - f != g - p:                      # not a midpoint: f32(p) is the correctly rounded result
        return f
    e = math.fma(x, y, -p)                  # exact = p + e
    if e == 0:
        return f                            # true tie: f32 already chose the even one
    return g if (e > 0) == (g > f) else f


def _next32(f, d):
    b = _F.pack(f)
    i = struct.unpack('<i', b)[0]
    if f == 0:
        return math.copysign(1.4e-45, d)
    if (f > 0) == (d > 0):
        i += 1
    else:
        i -= 1
    return _F.unpack(struct.pack('<i', i))[0]


def r24frac(fr):
    """exact Fraction -> nearest 24-bit-mantissa value (ties to even), as a float"""
    if fr == 0:
        return 0.0
    s = -1 if fr < 0 else 1
    fr = abs(fr)
    n, d = fr.numerator, fr.denominator
    e = n.bit_length() - d.bit_length()
    # want 2^23 <= n / d * 2^-e' < 2^24
    sh = 23 - e
    while True:
        num = n << sh if sh >= 0 else n
        den = d if sh >= 0 else d << -sh
        q, r = divmod(num, den)
        if q >= 1 << 24:
            sh -= 1
            continue
        if q < 1 << 23:
            sh += 1
            continue
        break
    if 2 * r > den or (2 * r == den and q & 1):
        q += 1
    return s * math.ldexp(q, -sh)


def fist(x):
    """fistp with RC = nearest even"""
    return int(round(x))


def s16(v):
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


def s8(v):
    v &= 0xff
    return v - 0x100 if v & 0x80 else v


class Rng:
    """0x141f6 (state 0x141ea / 0x141ee / 0x141f2, all 0 in the image)."""

    def __init__(self, s0=0, s1=0, s2=0):
        self.s = [s0, s1, s2]

    def __call__(self):
        M = 0xffffffff
        rol = lambda v, n: ((v << n) | (v >> (32 - n))) & M
        eax = (self.s[0] + 0xe372471) & M
        ebx = (self.s[1] + 0xbc5ea9) & M
        ecx = (self.s[2] + 0xc8ade7) & M
        eax ^= ebx
        sa = eax - (1 << 32) if eax & 0x80000000 else eax
        sc = ecx - (1 << 32) if ecx & 0x80000000 else ecx
        prod = (sa * sc) & 0xffffffffffffffff           # imul ecx: edx:eax signed
        eax, edx = prod & M, prod >> 32
        edx = (rol(edx, 13) + eax) & M
        ecx = rol(ecx, 14)
        edx = (edx - ebx) & M
        eax ^= edx
        ebx = (ebx + ecx) & M
        ecx ^= edx
        eax = (eax - ebx) & M
        ebx = rol(ebx, 9)
        prod = eax * ecx                                # mul ecx: unsigned
        eax, edx = prod & M, prod >> 32
        ebx ^= edx
        eax = (eax + 0x1e42ac3) & M
        ebx = (ebx + 0x5edf04) & M
        ecx = (ecx + 0x11a2ec1) & M
        self.s = [ebx, ecx, eax]
        return eax


class Record:
    def __init__(self, raw):
        self.a0, self.b0, self.r, self.q = struct.unpack_from('<4d', raw, 0)        # 0x15e18 .. 0x15e30
        self.mode, self.npass = raw[0x20], raw[0x21]                                 # 0x15e38, 0x15e39
        self.mul, self.gain, self.fb = struct.unpack_from('<3d', raw, 0x22)          # 0x15e3a, 0x15e42, 0x15e4a
        self.harm = raw[0x3a]                                                        # 0x15e52
        self.raw = bytearray(raw) + bytes([0xdd])     # + the code byte after the record (0x15e93, fld opcode)

    # fields that the code reads with an index that can run past their array (seg = 16)
    def pitch(self, i):            # word [0x15e53 + 2i]
        return struct.unpack_from('<H', self.raw, 0x3b + 2 * i)[0]

    def x4(self, i):               # byte [0x15e73 + i]
        return self.raw[0x5b + i]

    def tie(self, i):              # byte [0x15e83 + i]   (i = 16 reads the code byte 0xdd)
        return self.raw[0x6b + i]


class Synth:
    def __init__(self, img, rng):
        self.img = img
        self.rng = rng
        self.W = bytearray(0x10000)     # [0x15dce], zeroed by main
        self.cnt = 0                    # [0x15dd2]
        self.seg = 0                    # [0x15e00]
        self.y1 = 0                     # [0x15dfa]
        self.k3529 = struct.unpack_from('<d', img, K3529)[0]
        self.k102 = struct.unpack_from('<d', img, K102)[0]
        self.k32767 = struct.unpack_from('<d', img, K32767)[0]
        self.out = bytearray()
        self._gain_cache = {}
        self._fb_cache = {}
        self._omq = {}
        self._detune = {}

    # 0x161f2: note on (also called once per pass with whatever [0x15e00] holds)
    def note_on(self):
        R = self.rec
        self.y1 = 0
        self.ph = 0x8002
        R.b0 = mul24x(R.mul, R.b0)          # fld MUL; fld st0; fmul [0x15e20]; fst [0x15e20]; fstp [0x15dea]
        self.b = R.b0
        R.a0 = mul24x(R.mul, R.a0)          # fmul [0x15e18]; fst [0x15e18]; fstp [0x15de2]
        self.a = R.a0
        self.q = R.q                        # fld/fstp qword: exact copies
        self.r = R.r
        if R.x4(self.seg) == 1:
            self.b = f32(self.b * 4.0)      # fmul dword 4.0
            self.a = f32(self.a * 4.0)
        self.pitch = R.pitch(self.seg)

    def one_minus_q(self):
        v = self._omq.get(self.q)
        if v is None:
            v = self._omq[self.q] = r24frac(1 - Fraction(self.q))
        return v

    # 0x15f55: mode 2 oscillator (x^7 shaped phase, plus one "harmonic")
    def shaped(self):
        R = self.rec
        K = self.k32767

        def shape(p):
            t = float(p)
            if p > 0:
                t = float(32767 - p)                    # fchs; fadd 32767 (exact)
            t = f32(t / K)
            t2 = f32(t * t)
            t4 = f32(t2 * t2)
            t6 = f32(t2 * t4)
            return f32(f32(t6 * K) * t)

        s = 0
        ebx = 0
        while True:
            s = fist(shape(s16(self.ph)))               # the loop recomputes and overwrites the base each time
            if R.harm != 0:
                e = (self.cnt * ebx) & 0xffffffff
                e = e * (self.pitch & 0xffffffff)
                eax = (e >> 7) & 0xffffffff
                if not ebx & 1:
                    eax = (-eax) & 0xffffffff
                self.ph2 = (eax + self.ph) & 0xffff
                s = fist(f32(shape(s16(self.ph2)) + s))
            ebx += 1
            if s8(ebx) > s8(R.harm):
                break
        return s

    # 0x15e93: the filter. x = input (int), uses y1 = previous output
    def filt(self, x):
        self.a = mul24x(self.a, self.r)
        c = f32(self.a + self.b)
        m = f32(c * f32(float(x + self.y1)))
        dd = f32(self.d - m)
        self.d = f32(self.one_minus_q() * dd)
        return fist(f32(self.d + self.y1))

    # 0x1601d: one sample
    def sample(self, j):
        R = self.rec
        s = s16(self.ph)
        if R.harm != 0:
            t = self._detune.get(self.pitch)
            if t is None:
                t = self._detune[self.pitch] = fist(mul24x(float(self.pitch), self.k102))
            self.ph2 = (self.ph2 + t) & 0xffff
            s += s16(self.ph2)
        if R.mode == 2:
            s = self.shaped()
        elif R.mode == 4:
            s = (self.rng() & 0xffffff) % 0xfa01 - 0x7d00
        seg = self.seg
        if seg != 0 and R.tie(seg) == 1:
            if self.k < GLIDE:
                K = self.k3529
                k2 = 2 * self.k
                u = f32(f32(K - k2) / K)
                u = f32(u * R.pitch(seg - 1))
                v = f32(f32(k2 / K) * R.pitch(seg))
                self.pitch = fist(f32(u + v))
            else:
                self.pitch = R.pitch(seg)
        self.ph = (self.ph + self.pitch) & 0xffff
        g = self._gain_cache.get((R.gain, s))
        if g is None:
            g = self._gain_cache[(R.gain, s)] = fist(mul24x(float(s), R.gain))
        s = self.filt(g)
        self.cnt += 1
        self.y1 = s
        if seg <= 15 and R.tie(seg + 1) == 0:          # fade out a note that is not continued
            f = f32(1.0 - f32(j / self.k3529))
            s = fist(f32(f * s))
        if s > 0x7fff:
            s = 0x7fff
        elif s < -0x7fff:
            s = -0x7fff
        self.W[self.cnt] = (s >> 8) & 0xff
        self.k += 1

    # 0x15ede: feedback echo over the 16 notes
    def echo(self):
        W = self.W
        fb = self.rec.fb
        cache = self._fb_cache
        for e in range(NSEG):
            for bx in range(SEGLEN + 1):
                i = e * SEGLEN + bx
                a = s8(W[i])
                dly = s8(W[i - ECHO_BACK]) if e >= 3 else s8(W[i + ECHO_WRAP])
                key = (fb, dly)
                v = cache.get(key)
                if v is None:
                    v = cache[key] = fist(mul24x(float(dly), fb))
                a += v
                if a > 0x7f:
                    a = 0x7f
                elif a < -0x7f:
                    a = -0x7f
                W[i] = a & 0xff

    # 0x162cd: (halve record 7), decimate by 2, append 28001 bytes
    def flush(self, recno):
        W = self.W
        if recno == 7:
            for i in range(HALVE_LEN):
                W[i] = (s8(W[i]) >> 1) & 0xff
        for i in range(DECIM_LAST + 1):
            W[i] = W[2 * i]
        self.cnt = 0
        self.out += W[:COPY_LEN]
        self.out.append(W[COPY_LEN - 1])

    def run(self, progress=None):
        img = self.img
        for it in range(12):                                            # [0x163eb]
            recno = struct.unpack_from('<I', img, ORDER + 4 * it)[0] - 1
            self.ph = 0
            self.ph2 = 0
            self.a = self.b = self.q = self.r = self.d = 0.0
            self.rec = R = Record(img[REC + REC_LEN * recno:REC + REC_LEN * (recno + 1)])
            for pas in range(R.npass + 1):                              # [0x163e6]
                self.note_on()
                for self.seg in range(NSEG):                            # 0x1628f
                    if R.tie(self.seg) == 0:
                        self.note_on()
                    self.k = 0
                    for j in range(SEGLEN + 1):
                        self.sample(j)
                self.seg = NSEG
                self.echo()
                self.flush(recno)
                if progress:
                    progress(it, recno, pas, len(self.out))
        return bytes(self.out)


def expand(img, rng_calls=80):
    """the whole of 0x18aaa..0x18ac2: copy the stored module, append the synthesised samples"""
    rng = Rng()
    for _ in range(rng_calls):          # precalc 0x11dfe calls the generator 80 times before
        rng()
    syn = Synth(img, rng)
    gen = syn.run(lambda it, rn, p, n: print('  record %2d pass %d -> %d bytes' % (rn, p, n), flush=True))
    mod = bytes(img[SRC:SRC + SRC_LEN]) + gen
    return mod


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--img', default=os.path.join(RE, 'jizz_img.bin'))
    ap.add_argument('--out', default='')
    ap.add_argument('--check', default=os.path.join(RE, 'dump', 'smokeout.mxm'))
    a = ap.parse_args()
    img = open(a.img, 'rb').read()
    mod = expand(img)
    if a.out:
        open(a.out, 'wb').write(mod)
    if a.check:
        ref = open(a.check, 'rb').read()
        n = min(len(ref), len(mod))
        diff = [i for i in range(n) if ref[i] != mod[i]]
        print('model %d bytes, reference %d bytes; %d bytes differ in the common part' % (len(mod), len(ref),
                                                                                         len(diff)))
        if len(ref) > len(mod):
            tail = ref[len(mod):]
            print('reference tail after the model: %d bytes, all zero: %s' % (len(tail), not any(tail)))
        if diff:
            print('first differences at', diff[:20])


if __name__ == '__main__':
    main()

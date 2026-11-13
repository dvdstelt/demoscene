"""CLONE.EXE's module builder 0x414aee and its two sample synthesisers 0x4149b4 / 0x414a38, with the x87 arithmetic of
the capture (unicorn): 64-bit mantissas, rounding = truncate (control word 0x1f7f), fsin = the host's double sin of
the operand truncated to a double.

The builder (see notes-music.md section 2):
  - allocates 0x124f80 bytes (heap 0x428470 in the capture), copies the stored module part, 0xe153 bytes from
    0x406862 (MXM header, instruments, patterns, 4,288 bytes of 8-bit samples);
  - appends 20 16-bit samples, each followed by one word of gap (never written: zero), made by
      synth A 0x4149b4(dst, n, amp, f, df, ddf):      x[i] = trunc(sin(i * f_i) * amp * 255)
            with f_i = f - df_0 - ... - df_(i)  (f and df updated before the sample, stored as float), df -= ddf
      synth B 0x414a38(dst, n, P, M, D, E, amp):      x[i] = trunc(sin(2 pi i / E) * sin(2 pi i / (P + D sin(2 pi i / M)))
                                                                 * amp * 255)
    clamped to +-0x7e81 (32385) and stored as words.
The parameters are the immediates the builder pushes (read here from clone_img.bin by decoding 0x414b1f..0x414efd).

usage: python3 modsynth.py [--out module.bin] [--check SNAPDUMP_module.bin] [--dram ../../full.dram]
"""
import argparse
import math
import os
import struct
import sys

import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
BASE = 0x400000
MOD_SRC, MOD_STORED = 0x406862, 0xe153        # stored module part in the image
BUF_SIZE = 0x124f80                           # the builder's allocation
SYNTH_A, SYNTH_B = 0x4149b4, 0x414a38
CALLS_FROM, CALLS_TO = 0x414b1f, 0x414efd     # the 20 calls (after the copy loop, before the return)
TWO_PI = 0x41a458                             # qword 6.283185307179586 (synth B)
K255_A, K255_B = 0x41a450, 0x41a454           # float 255.0 (synth A, synth B)
CLAMP = 0x7e81


def load_img(path=None):
    return open(path or os.path.join(RE, 'unpacked', 'clone_img.bin'), 'rb').read()


# ---------------------------------------------------------------------------------------------------------------
# x87 with RC = truncate: a value is (m, e) = m * 2^e with a signed integer mantissa
# ---------------------------------------------------------------------------------------------------------------
# --variant changes the arithmetic to show what the rounding mode does (the default is what the capture ran):
#   x87     64-bit mantissas, every result truncated (RC = 3): the program
#   nearest 64-bit mantissas, round to nearest even, fistp rounds to nearest (the x87 default control word)
#   double  53-bit doubles rounded to nearest for the arithmetic; float stores, the double for fsin and fistp
#           truncated (a C++ port with doubles that only takes care of the explicit stores and conversions)
VARIANT = 'x87'
EXT = 64


def chop(m, e, bits=64, store=False):
    """round the mantissa to `bits` significant bits (x87: toward zero)"""
    if m == 0:
        return 0, 0
    if bits == 64:
        bits = EXT
    a = -m if m < 0 else m
    n = a.bit_length()
    if n > bits:
        k = n - bits
        if VARIANT == 'nearest' or (VARIANT == 'double' and not store):
            r = a & ((1 << k) - 1)
            a >>= k
            half = 1 << (k - 1)
            if r > half or (r == half and a & 1):
                a += 1
                if a.bit_length() > bits:
                    a >>= 1
                    k += 1
        else:
            a >>= k
        e += k
    return (-a if m < 0 else a), e


def xf(v):
    """exact value of a Python float (double) as (m, e)"""
    if v == 0:
        return 0, 0
    fr, ex = math.frexp(v)
    m = int(fr * (1 << 53))
    return m, ex - 53


def xi(i):
    return i, 0


def f32(bits):
    """float32 bit pattern -> exact (m, e)"""
    return xf(struct.unpack('<f', struct.pack('<I', bits))[0])


def mul(a, b, bits=64):
    return chop(a[0] * b[0], a[1] + b[1], bits)


def add(a, b, bits=64):
    (m1, e1), (m2, e2) = a, b
    if m1 == 0:
        return chop(m2, e2, bits)
    if m2 == 0:
        return chop(m1, e1, bits)
    if e1 < e2:
        (m1, e1), (m2, e2) = (m2, e2), (m1, e1)
    return chop((m1 << (e1 - e2)) + m2, e2, bits)


def sub(a, b, bits=64):
    return add(a, (-b[0], b[1]), bits)


def div(a, b, bits=64):
    """a / b truncated to `bits` bits (enough quotient bits, then chop: = chop of the exact quotient)"""
    (m1, e1), (m2, e2) = a, b
    if m1 == 0:
        return 0, 0
    neg = (m1 < 0) != (m2 < 0)
    m1, m2 = abs(m1), abs(m2)
    k = bits + 2 + m2.bit_length() - m1.bit_length()
    if k < 0:
        k = 0
    q = (m1 << k) // m2
    q, e = chop(q, e1 - e2 - k, bits)
    return (-q if neg else q), e


def to_double(a):
    """fstp qword / the conversion before the emulator's fsin: truncate to 53 bits"""
    m, e = chop(a[0], a[1], 53, store=True)
    return math.ldexp(m, e)


def fsin(a):
    return xf(math.sin(to_double(a)))


def trunc(a):
    """fistp with RC = truncate (round to nearest even in the 'nearest' variant)"""
    m, e = a
    if e >= 0:
        return m << e
    if VARIANT == 'nearest':
        q, r = divmod(m, 1 << -e)
        h = 1 << (-e - 1)
        return q + 1 if r > h or (r == h and q & 1) else q
    return -((-m) >> -e) if m < 0 else m >> -e


def clamp(v):
    return CLAMP if v > CLAMP else (-CLAMP if v < -CLAMP else v)


# ---------------------------------------------------------------------------------------------------------------
# the synthesisers
# ---------------------------------------------------------------------------------------------------------------
def synth_a(n, amp, f, df, ddf, k255):
    """0x4149b4: drums / toms. amp, f, df, ddf: float32 bit patterns (stack arguments 1..4)"""
    amp = chop(*mul(f32(amp), k255), 24, True)          # fld; fmul 255.0; fstp dword
    f, df, ddf = f32(f), f32(df), f32(ddf)
    out = []
    for i in range(n):
        f_new = chop(*sub(f, df), 24, True)             # fsub; fstp dword [f]
        df_new = chop(*sub(df, ddf), 24, True)          # fsub; fstp dword [df]
        x = mul(xi(i), f_new)                           # fild i; fmul dword [f]  (the new f)
        y = mul(fsin(x), amp)
        out.append(clamp(trunc(y)))
        f, df = f_new, df_new
    return out


def synth_b(n, p, m, d, e, amp, k255, two_pi):
    """0x414a38: tones. p (base period), m (modulation period), d (modulation depth), e (envelope period), amp:
    float32 bit patterns (stack arguments 1..5)"""
    amp = chop(*mul(f32(amp), k255), 24, True)
    p, m, d, e = f32(p), f32(m), f32(d), f32(e)
    out = []
    for i in range(n):
        di = xi(i)                                      # fild i; fstp qword (exact)
        a = mul(div(two_pi, m), di)                     # 2 pi / M * i
        b = add(mul(fsin(a), d), p)                     # sin(.) * D + P   (+ i / 999999999 = 0)
        c = mul(div(two_pi, b), di)                     # 2 pi / (.) * i
        s2 = fsin(c)                                    # (stored as a double: exact)
        g = mul(div(two_pi, e), di)                     # 2 pi / E * i
        y = mul(mul(fsin(g), s2), amp)
        out.append(clamp(trunc(y)))
    return out


# ---------------------------------------------------------------------------------------------------------------
# the builder 0x414aee
# ---------------------------------------------------------------------------------------------------------------
def calls(img):
    """decode the call sequence: [(call address, synth, buffer offset, n, [stack args in order arg1..])]"""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    code = img[CALLS_FROM - BASE:CALLS_TO - BASE]
    ecx = edx = None
    pushes = []
    out = []
    for ins in md.disasm(code, CALLS_FROM):
        mn, ops = ins.mnemonic, ins.op_str
        if mn == 'push' and ops.startswith('0x') or ops == '0' and mn == 'push':
            pushes.append(int(ops, 0))
        elif mn == 'mov' and ops.startswith('edx, 0x') and int(ops[5:], 0) < 0x100000:
            edx = int(ops[5:], 0)
        elif mn == 'lea' and ops.startswith('ecx, [ebx + '):
            ecx = int(ops[12:-1], 0)
        elif mn == 'add' and ops.startswith('ecx, '):
            ecx += int(ops[5:], 0)
        elif mn == 'call' and int(ops, 0) in (SYNTH_A, SYNTH_B):
            out.append((ins.address, int(ops, 0), ecx, edx, pushes[::-1]))
            pushes = []
    return out


def build(img, verbose=False):
    buf = bytearray(BUF_SIZE)
    buf[:MOD_STORED] = img[MOD_SRC - BASE:MOD_SRC - BASE + MOD_STORED]
    k255a = f32(struct.unpack_from('<I', img, K255_A - BASE)[0])
    k255b = f32(struct.unpack_from('<I', img, K255_B - BASE)[0])
    two_pi = xf(struct.unpack_from('<d', img, TWO_PI - BASE)[0])
    for (addr, fn, ofs, n, args) in calls(img):
        if fn == SYNTH_A:
            w = synth_a(n, *args, k255=k255a)
        else:
            w = synth_b(n, *args, k255=k255b, two_pi=two_pi)
        struct.pack_into('<%dh' % n, buf, ofs, *w)
        if verbose:
            fl = [struct.unpack('<f', struct.pack('<I', x))[0] for x in args]
            print('%06x %s ofs %#07x n %#6x  args %s  peak %d' % (
                addr, 'A' if fn == SYNTH_A else 'B', ofs, n, ' '.join('%.9g' % x for x in fl),
                max(abs(x) for x in w)), flush=True)
    return buf


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--img')
    ap.add_argument('--out')
    ap.add_argument('--check', help='module buffer dumped from snaps/t11.snap (dumpmod.py)')
    ap.add_argument('--dram', default=os.path.join(RE, 'full.dram'))
    ap.add_argument('--variant', default='x87', choices=('x87', 'nearest', 'double'))
    a = ap.parse_args()
    global VARIANT, EXT
    VARIANT = a.variant
    EXT = 53 if a.variant == 'double' else 64
    img = load_img(a.img)
    buf = build(img, verbose=True)
    if a.out:
        open(a.out, 'wb').write(buf)
    if a.check:
        ref = open(a.check, 'rb').read()
        bad = [i for i in range(len(ref)) if ref[i] != buf[i]]
        words = sorted({i >> 1 for i in bad})
        print('module buffer: %s' % ('identical (%d bytes)' % len(ref) if not bad else
                                      '%d bytes / %d samples differ, first at %#x, max difference %d' % (
                                          len(bad), len(words), bad[0], max(abs(
                                              struct.unpack_from('<h', ref, 2 * w)[0] -
                                              struct.unpack_from('<h', buf, 2 * w)[0]) for w in words))))
    if a.dram and os.path.exists(a.dram):
        ref = open(a.dram, 'rb').read()
        smpstart, smp8, smp16 = struct.unpack_from('<3I', buf, 0x1c)
        n = ((smp16 * 2 + smp8) >> 4) << 4
        dram = bytes(buf[smpstart:smpstart + n]) + bytes(len(ref) - n)
        bad = sum(1 for x, y in zip(dram, ref) if x != y)
        print('GUS DRAM (upload of %d bytes): %s' % (n, 'identical (1 MB)' if not bad else '%d bytes differ' % bad))


if __name__ == '__main__':
    main()

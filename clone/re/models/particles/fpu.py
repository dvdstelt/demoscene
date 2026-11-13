"""Helpers that mimic the x87 as CLONE runs it (control word 0x1f7f: 64-bit precision, rounding toward zero).

Python floats are doubles rounded to nearest; for the values here that is close to the 64-bit extended results, and
the stores into float32 variables (fstp dword) truncate toward zero, which f32t() does. fistp truncates: int().
"""
import math
import struct

import numpy as np


def f32(x):
    """float32 value nearest to x (what a float constant / variable holds)."""
    return struct.unpack('<f', struct.pack('<f', x))[0]


def f32t(x):
    """x stored as float32 with rounding toward zero (fstp dword under RC = truncate)."""
    if x != x or x in (math.inf, -math.inf):
        return x
    f = f32(x)
    if abs(f) > abs(x):
        f = float(np.nextafter(np.float32(f), np.float32(0)))
    return f


def bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def fromf(u):
    return struct.unpack('<f', struct.pack('<I', u & 0xffffffff))[0]


def ext(x):
    """Fraction x rounded to the x87's 64-bit mantissa toward zero (what one fadd / fmul / fsub gives)."""
    from fractions import Fraction
    x = Fraction(x)
    if x == 0:
        return x
    s = -1 if x < 0 else 1
    a = abs(x)
    e = a.numerator.bit_length() - a.denominator.bit_length()
    if Fraction(2) ** e > a:
        e -= 1
    scale = Fraction(2) ** (63 - e)                 # 64 significant bits
    return s * Fraction(int(a * scale)) / scale     # int() truncates


def trunc_bits(x, bits):
    """Fraction x truncated toward zero to `bits` significant bits (53: a double store under RC = truncate)."""
    from fractions import Fraction
    x = Fraction(x)
    if x == 0:
        return x
    s = -1 if x < 0 else 1
    a = abs(x)
    e = a.numerator.bit_length() - a.denominator.bit_length()
    if Fraction(2) ** e > a:
        e -= 1
    scale = Fraction(2) ** (bits - 1 - e)
    return s * Fraction(int(a * scale)) / scale


def t70_dt(timers):
    """main 0x400f76..0x400fbf: t70 = timer * K kept as a double, dt = (float)(t70 - previous t70). K is the double
    at 0x41a128 = 5.8675115883353874e-05 = 1/17043 (not 70/1193182 = 1/17045.46: the program's t70 runs 0.0144 %
    fast against the music timer).
    timers: xmpGetTimer values of consecutive frames. Returns [(t70, dt)] from the second value on."""
    from fractions import Fraction
    K = Fraction(struct.unpack('<d', bytes.fromhex('ba1866a13dc30e3f'))[0])   # the double at 0x41a128
    out, prev = [], None
    for tm in timers:
        t70 = trunc_bits(ext(tm * K), 53)
        if prev is not None:
            out.append((float(t70), f32t(float(trunc_bits(ext(t70 - prev), 53)))))
        prev = t70
    return out

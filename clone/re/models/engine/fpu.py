"""x87 arithmetic as CLONE runs it: 64-bit mantissa, rounding = truncate toward zero (control word 0x1f7f).

np.longdouble is the x87 80-bit format on x86-64 Linux; after chop() every longdouble operation and every
conversion to float32 / float64 truncates like the original. Call chop() once before modelling and nearest()
around host maths that must not run in that mode (math.sin / math.cos: the emulator computes fsin / fcos in
double precision on the host, in round-to-nearest).
"""
import ctypes
import ctypes.util
import math
import struct

import numpy as np

L = np.longdouble
_libm = ctypes.CDLL(ctypes.util.find_library('m'))


def chop():
    _libm.fesetround(0xc00)          # FE_TOWARDZERO (x87 control word and MXCSR)


def nearest():
    _libm.fesetround(0)


def f32(x):
    """fst dword: store to float (chopped), as extended again."""
    return L(np.float32(x))


def rdf(mem, a, base=0x400000):
    """float at linear address a of a memory image starting at base, as extended."""
    return L(np.frombuffer(bytes(mem[a - base:a - base + 4]), '<f4')[0])


def rdd(mem, a, base=0x400000):
    return L(np.frombuffer(bytes(mem[a - base:a - base + 8]), '<f8')[0])


def rd32(mem, a, base=0x400000):
    return struct.unpack('<I', bytes(mem[a - base:a - base + 4]))[0]


def rdi32(mem, a, base=0x400000):
    return struct.unpack('<i', bytes(mem[a - base:a - base + 4]))[0]


def fist(x):
    """fistp: truncate toward zero (rounding control = chop)."""
    return int(x)


def magic_low(v, magic):
    """fadd magic (extended, chopped), fstp qword (chopped to double), read the low dword (unsigned).
    magic 1.5*2^52 (+ offset): floor(v) (+ offset); magic 1.5*2^60: floor(v / 256)."""
    d = np.float64(L(magic) + v)
    return int(np.array([d]).view(np.uint64)[0]) & 0xffffffff


def s32(x):
    x &= 0xffffffff
    return x - (1 << 32) if x >= 1 << 31 else x


def fsin(a):
    """fsin of the emulator: host double sin of the (exactly converted) argument."""
    nearest()
    r = math.sin(float(a))
    chop()
    return L(r)


def fcos(a):
    nearest()
    r = math.cos(float(a))
    chop()
    return L(r)

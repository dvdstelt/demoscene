"""Model of main's time step (0x400f76..0x400fbf) with the FPU's truncating rounding, checked bit for bit
against the snapshots: t70 = timer * C (C = double at 0x41a128 = 1/17043), stored as double [0xe9572e];
dt = (t70 in 64-bit precision) - previous double t70, stored as float [0x420489]; [0xe9573e] += 0.03 dt.

usage (toolbox, from clone/re): python3 models/core/dtmodel.py
"""
import glob
import os
import pickle
import struct
from fractions import Fraction as F

import unicorn  # noqa: F401

RE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
IMG = open(os.path.join(RE, 'unpacked', 'clone_img.bin'), 'rb').read()
C = F(struct.unpack('<d', IMG[0x1a128:0x1a130])[0])
K003 = F(struct.unpack('<d', IMG[0x1a130:0x1a138])[0])


def trunc_bits(x, bits):
    """x (Fraction) rounded toward zero to a binary float with `bits` mantissa bits (no exponent limits)."""
    if x == 0: return F(0)
    s = -1 if x < 0 else 1
    a = abs(x)
    e = a.numerator.bit_length() - a.denominator.bit_length()
    while F(2) ** e > a: e -= 1
    while F(2) ** (e + 1) <= a: e += 1
    q = F(2) ** (e - bits + 1)
    return s * (a // q) * q


def step(timer, prev, acc):
    t_ext = trunc_bits(timer * C, 64)          # fild * qword: extended product (RC = truncate)
    t70 = trunc_bits(t_ext, 53)                # fst qword [0xe9572e]
    d_ext = trunc_bits(t_ext - prev, 64)       # fsub qword [0xe95736]
    dt = trunc_bits(d_ext, 24)                 # fst dword [0x420489]
    acc = trunc_bits(trunc_bits(trunc_bits(d_ext * K003, 64) + acc, 64), 24)
    return t70, dt, acc


flips = []
for line in open(os.path.join(RE, 'full.flips')):
    if line.startswith('#'): continue
    f = line.split()
    flips.append((int(f[0]), int(f[1]), int(f[3])))

# replay the whole run from the first timer read: prev = 0, acc = 0
prev, acc = F(0), F(0)
state = {}
for n, tick, timer in flips:
    if timer == -(1 << 40): continue
    t70, dt, acc = step(timer, prev, acc)
    prev = t70
    state[n] = (float(t70), float(dt), float(acc))

for path in sorted(glob.glob(os.path.join(RE, 'snaps', 't*.snap')), key=lambda p: int(os.path.basename(p)[1:-5])):
    s = pickle.load(open(path, 'rb'))
    mem = s['mem']
    tick = int(os.path.basename(path)[1:-5])
    got = (struct.unpack('<d', mem[0xe9572e:0xe95736])[0], struct.unpack('<f', mem[0x420489:0x42048d])[0],
           struct.unpack('<f', mem[0xe9573e:0xe95742])[0])
    # the snapshot holds the values computed after the last frame shown before it: the timer of the next flip
    ok = [n for n in state if state[n] == got]
    near = min(state, key=lambda n: abs(state[n][0] - got[0]))
    print(f'{os.path.basename(path):12s} t70 {got[0]:.9f} dt {got[1]:.9f} acc {got[2]:.6f}  matches model at flip '
          f'{ok} (nearest t70: flip {near}, model {state[near]})')

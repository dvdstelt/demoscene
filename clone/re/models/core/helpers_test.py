"""Run the shared 2D helpers and the 15-bit conversion of CLONE.EXE in unicorn on random inputs and compare them
with the Python models below (the pseudo-C of notes-core.md). Memory comes from snaps/t11.snap (image + tables).

  0x4043be fill rectangle, 0x404002 picture blit (0x4043ec key blit, 0x40442c blend through the multiply table),
  0x4045b4 feedback blend, 0x404a08 8x8 block expansion, 0x4167b7 / 0x4034e7 log curves, 0x4188d4 32 -> 15 bit.

usage (toolbox, from clone/re): python3 models/core/helpers_test.py
"""
import math
import os
import pickle
import random
import struct

import numpy as np
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP

RE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SNAP = pickle.load(open(os.path.join(RE, 'snaps', 't11.snap'), 'rb'))
RAM = 0x2000000
STACK = 0x1ff0000
RET = 0x1ff8000           # sentinel return address
STUB = 0x1ff9000          # fldcw stub
SCR = 0x1000000           # scratch buffers (above the program's BSS)

mu = Uc(UC_ARCH_X86, UC_MODE_32)
mu.mem_map(0, RAM)
mu.mem_write(0, SNAP['mem'])
mu.mem_write(STUB, b'\xd9\x2d' + struct.pack('<I', STUB + 0x10) + b'\x90')
mu.mem_write(STUB + 0x10, struct.pack('<H', 0x1f7f))
mu.reg_write(UC_X86_REG_ESP, STACK)
mu.emu_start(STUB, STUB + 6)            # FPU: truncate, 64-bit precision, as 0x4010f6 sets it


def call(addr, regs=(), stack=(), callee_pops=False):
    esp = STACK
    for v in reversed(stack):
        esp -= 4
        mu.mem_write(esp, struct.pack('<I', v & 0xffffffff))
    esp -= 4
    mu.mem_write(esp, struct.pack('<I', RET))
    mu.reg_write(UC_X86_REG_ESP, esp)
    for r, v in zip((UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ECX), regs):
        mu.reg_write(r, v & 0xffffffff)
    mu.emu_start(addr, RET)
    return mu.reg_read(UC_X86_REG_EAX)


def rd(a, n): return bytes(mu.mem_read(a, n))
def wr(a, b): mu.mem_write(a, bytes(b))


rng = random.Random(1998)
nprng = np.random.default_rng(1998)
MUL = SNAP['mem'][0xea740c:0xea740c + 64 * 512]


# ---------------------------------------------------------------- models
def fill_rect(dst, colour, w, h):                     # 0x4043be(dst, colour, w, h); w, h are bytes (0 = 256)
    w = w & 0xff or 256
    h = h & 0xff or 256
    for y in range(h):
        dst[y * 320:y * 320 + w] = colour


def key_blit(src, w, h, dst):                         # 0x4043ec: skip 0xffffffff
    for y in range(h):
        row = src[y * w:(y + 1) * w]
        m = row != 0xffffffff
        dst[y * 320:y * 320 + w][m] = row[m]


def blend_blit(src, w, h, dst, level):                # 0x40442c: c = s + trunc(level * (d - s) / 64) per channel
    for y in range(h):
        for x in range(w):
            s = int(src[y * w + x])
            if s == 0xffffffff: continue
            d = int(dst[y * 320 + x])
            o = s & 0xff000000
            for sh in (0, 8, 16):
                cs, cd = (s >> sh) & 255, (d >> sh) & 255
                o |= ((cs + MUL[level * 512 + 256 + cd - cs]) & 255) << sh
            dst[y * 320 + x] = o


def blit(src, w, h, x, y, dst, mode, level):          # 0x404002
    view = dst[x + y * 320:]
    if mode == 0 or (mode == 1 and level <= 0):
        key_blit(src, w, h, view)
    elif mode == 1:
        blend_blit(src, w, h, view, min(level, 63))


def logcurve(x, mult, scale):                         # 0x4167b7 (1/128, 1000/11), 0x4034e7 (1, 23)
    a = (1.0 + x) * mult
    if a <= 0: return 0                               # ln of <= 0: fistp gives 0x80000000 -> clamped to 0
    v = math.log(a) * scale + 128.0
    v = int(v)
    return 255 if v > 255 else (0 if v < 0 else v)


def to15(src):                                        # 0x4188d4: pixels 8..63991 only; source cleared
    out = np.zeros(64000, np.uint16)
    s = src.astype(np.uint32)
    p = (((s >> 16) & 0xff) >> 3) << 10 | (((s >> 8) & 0xff) >> 3) << 5 | ((s & 0xff) >> 3)
    out[8:63992] = p[8:63992]
    return out


# ---------------------------------------------------------------- tests
def test_fill():
    bad = 0
    for _ in range(20):
        dst = nprng.integers(0, 1 << 32, 64000, dtype=np.uint64).astype(np.uint32)
        w, h = rng.randint(1, 256), rng.randint(1, 100)
        x, y = rng.randint(0, 320 - min(w, 320)), rng.randint(0, 200 - h)
        if w > 320 - x: w = 320 - x
        col = rng.getrandbits(32)
        wr(SCR, dst.tobytes())
        call(0x4043be, stack=(SCR + 4 * (x + 320 * y), col, w, h))
        want = dst.copy()
        fill_rect(want[x + 320 * y:], col, w, h)
        bad += int(np.count_nonzero(np.frombuffer(rd(SCR, 256000), np.uint32) != want))
    print('0x4043be fill rectangle: %d pixels differ' % bad)


def test_blit():
    bad = 0
    n = 0
    for mode, level in [(0, 0), (1, -5), (1, 0), (1, 1), (1, 17), (1, 32), (1, 63), (1, 64), (1, 200), (2, 10)]:
        w, h = rng.randint(1, 60), rng.randint(1, 40)
        x, y = rng.randint(0, 320 - w), rng.randint(0, 200 - h)
        src = nprng.integers(0, 1 << 24, w * h, dtype=np.uint32)
        src[nprng.random(w * h) < 0.3] = 0xffffffff
        dst = nprng.integers(0, 1 << 24, 64000, dtype=np.uint32)
        wr(SCR, dst.tobytes())
        wr(SCR + 0x100000, src.tobytes())
        wr(SCR + 0x200000, struct.pack('<3I', SCR + 0x100000, w, h))
        call(0x404002, regs=(SCR + 0x200000, x, y, SCR), stack=(mode, level))
        want = dst.copy()
        blit(src, w, h, x, y, want, mode, level)
        got = np.frombuffer(rd(SCR, 256000), np.uint32)
        bad += int(np.count_nonzero(got != want))
        n += 1
    print('0x404002 blit (modes 0, 1 with levels -5..200, 2): %d pixels differ in %d calls' % (bad, n))


def test_feedback():
    src = nprng.integers(0, 256, 64000, dtype=np.uint8)
    dst = nprng.integers(0, 256, 64000, dtype=np.uint8)
    wr(SCR, src.tobytes()); wr(SCR + 0x10000, dst.tobytes())
    call(0x4045b4, stack=(SCR, SCR + 0x10000))
    want = ((15 * dst.astype(np.int32) + src) >> 4).astype(np.uint8)
    got = np.frombuffer(rd(SCR + 0x10000, 64000), np.uint8)
    print('0x4045b4 feedback (15 dst + src) / 16: %d bytes differ' % np.count_nonzero(got != want))


def test_expand():
    src = nprng.integers(0, 256, 64000, dtype=np.uint8)
    pal = nprng.integers(0, 1 << 32, 256, dtype=np.uint64).astype(np.uint32)
    dst = np.zeros(64000, np.uint32)
    wr(SCR, src.tobytes()); wr(SCR + 0x10000, pal.tobytes()); wr(SCR + 0x20000, dst.tobytes())
    bx, by = 13, 7                                     # one block at (13*8, 7*8)
    off = by * 8 * 320 + bx * 8
    call(0x404a08, stack=(SCR + off, SCR + 0x20000 + 4 * off, SCR + 0x10000))
    want = dst.copy()
    for y in range(8):
        for x in range(8):
            want[off + y * 320 + x] = pal[src[off + y * 320 + x]]
    got = np.frombuffer(rd(SCR + 0x20000, 256000), np.uint32)
    print('0x404a08 8x8 block through a palette: %d pixels differ' % np.count_nonzero(got != want))


def test_log():
    for addr, mult, scale, name in ((0x4167b7, 1 / 128, struct.unpack('<d', SNAP['mem'][0x41a498:0x41a4a0])[0],
                                     '128 + (1000/11) ln((1+x)/128)'),
                                    (0x4034e7, 1.0, 23.0, '128 + 23 ln(1+x)')):
        xs = [rng.uniform(-1.5, 300) for _ in range(3000)] + [i * 0.25 for i in range(-8, 1200)] + [-1.0, 0.0]
        bad = []
        for x in xs:
            xf = struct.unpack('<f', struct.pack('<f', x))[0]
            got = call(addr, stack=(struct.unpack('<I', struct.pack('<f', xf))[0],))
            if got != logcurve(xf, mult, scale): bad.append((xf, got, logcurve(xf, mult, scale)))
        print('%#x %s: %d of %d differ %s' % (addr, name, len(bad), len(xs), bad[:5]))


def test_15bit():
    src = nprng.integers(0, 1 << 24, 64000, dtype=np.uint32)
    lfb = np.full(64000, 0xabcd, np.uint16)
    wr(SCR, src.tobytes()); wr(SCR + 0x100000, lfb.tobytes())
    call(0x4188d4, stack=(SCR, SCR + 0x100000))
    got = np.frombuffer(rd(SCR + 0x100000, 128000), np.uint16)
    want = to15(src)
    want[:8] = 0xabcd; want[63992:] = 0xabcd
    left = np.frombuffer(rd(SCR, 256000), np.uint32)
    cleared = np.count_nonzero(left[8:63992]), np.count_nonzero(left[:8] == src[:8]) + \
        np.count_nonzero(left[63992:] == src[63992:])
    print('0x4188d4 32 -> 15 bit: %d pixels differ; source pixels 8..63991 non-zero afterwards: %d; first / last 8 '
          'kept: %d of 16' % (np.count_nonzero(got != want), cleared[0], cleared[1]))


test_fill()
test_blit()
test_feedback()
test_expand()
test_log()
test_15bit()

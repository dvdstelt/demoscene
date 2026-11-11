"""Run the original 0x1632b (the "mp4" sample synthesiser) alone in unicorn, as run_jizz.py would, to get
intermediate values for checking mp4synth.py.

The progress routine 0x1115c is patched to `ret`; the random generator state is set to its value after the
80 calls of the precalc 0x11dfe; the FPU is initialised like main does (fninit, precision control 24 bit).

usage: python3 emu_synth.py [--out x.bin] [--pc 0|2|3] [--trace N]   (N: the first N filter calls of the 2nd record)
"""
import argparse
import os
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import mp4synth

IMG = open(os.path.join(RE, 'jizz_img.bin'), 'rb').read()
WORK = 0x200000
OUT = 0x300000
STUB = 0x7f000
STACK = 0x7e000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default='')
    ap.add_argument('--trace', type=int, default=0, help='print the filter input/output of the first N samples '
                    'of the second record')
    ap.add_argument('--pc', type=int, default=0, help='precision control field (0 = 24 bit, 2 = 53, 3 = 64)')
    a = ap.parse_args()
    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(0, 0x1000000)
    mu.mem_write(0, IMG)
    mu.mem_write(0x1115c, b'\xc3')
    mu.mem_write(0x15dce, struct.pack('<I', WORK))
    rng = mp4synth.Rng()
    for _ in range(80):
        rng()
    mu.mem_write(0x141ea, struct.pack('<III', *rng.s))
    mu.mem_write(0x15e14, struct.pack('<I', OUT))
    # stub: fninit; fstcw [0x2000]; and word [0x2000], ~0x300 | pc<<8; fldcw [0x2000]; call 0x1632b; hlt
    cw = 0x037f & 0xfcff | (a.pc << 8)
    code = b'\xdb\xe3' + b'\x66\xc7\x05' + struct.pack('<IH', 0x2000, cw) + b'\xd9\x2d' + struct.pack('<I', 0x2000)
    call_at = STUB + len(code)
    code += b'\xe8' + struct.pack('<i', 0x1632b - (call_at + 5)) + b'\xf4'
    mu.mem_write(STUB, code)
    mu.reg_write(UC_X86_REG_ESP, STACK)
    end = STUB + len(code) - 1

    state = {'n': 0, 'rec': 0}
    if a.trace:
        def hook(mu, addr, size, _):
            if addr == 0x1632b:
                state['rec'] += 1
            if state['rec'] == 2 and state['n'] < a.trace:
                if addr == 0x16138:      # before the filter call: [0x2000] = input
                    x = struct.unpack('<i', mu.mem_read(0x2000, 4))[0]
                    y1 = struct.unpack('<i', mu.mem_read(0x15dfa, 4))[0]
                    ph = struct.unpack('<H', mu.mem_read(0x15dd6, 2))[0]
                    pit = struct.unpack('<i', mu.mem_read(0x15df2, 4))[0]
                    av, bv, dv = struct.unpack('<d', mu.mem_read(0x15de2, 8))[0], \
                        struct.unpack('<d', mu.mem_read(0x15dea, 8))[0], struct.unpack('<d', mu.mem_read(0x15dda, 8))[0]
                    print('n %d x %d y1 %d ph %04x pitch %d a %r b %r d %r' % (state['n'], x, y1, ph, pit, av, bv, dv))
                    state['n'] += 1
        mu.hook_add(UC_HOOK_CODE, hook, None, 0x16138, 0x16138)
        mu.hook_add(UC_HOOK_CODE, hook, None, 0x1632b, 0x1632b)
    mu.emu_start(STUB, end)
    n = struct.unpack('<I', mu.mem_read(0x15e14, 4))[0] - OUT
    data = bytes(mu.mem_read(OUT, n))
    print('generated %d bytes' % n)
    if a.out:
        open(a.out, 'wb').write(data)
    ref = open(os.path.join(RE, 'dump', 'smokeout.mxm'), 'rb').read()[0x7f71:0x7f71 + n]
    print('equal to smokeout.mxm samples:', ref == data,
          'differing bytes:', sum(1 for i in range(n) if ref[i] != data[i]))


if __name__ == '__main__':
    main()

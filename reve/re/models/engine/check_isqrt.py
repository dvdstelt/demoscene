"""Run the square root 0x2175dc (EAX in, EAX out) in unicorn on the image and compare it with floor(sqrt(x)).

usage: python3 check_isqrt.py [N]
"""
import math
import random
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP

import engine


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200000
    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(0x200000, 0x80000)
    mu.mem_write(engine.IMG_BASE, engine.img())
    mu.mem_map(0x10000, 0x10000)
    stop = 0x200000                      # a 'hlt' address we never execute: emu_start ends there
    vals = [0, 1, 2, 3, 0xffffffff, 0xfffffffe, 0x80000000, 0x7fffffff] + \
           [k * k + d for k in (1, 2, 255, 256, 65535) for d in (-1, 0, 1)] + \
           [random.getrandbits(random.randint(1, 32)) for _ in range(n)]
    bad = 0
    for v in vals:
        v &= 0xffffffff
        mu.reg_write(UC_X86_REG_ESP, 0x1f000)
        mu.mem_write(0x1f000, stop.to_bytes(4, 'little'))
        mu.reg_write(UC_X86_REG_EAX, v)
        mu.emu_start(0x2175dc, stop)
        got = mu.reg_read(UC_X86_REG_EAX)
        if got != math.isqrt(v):
            bad += 1
            if bad < 10:
                print('isqrt(%#x): routine %d, floor %d' % (v, got, math.isqrt(v)))
    print('%d values, %d differ from floor(sqrt)' % (len(vals), bad))


if __name__ == '__main__':
    main()

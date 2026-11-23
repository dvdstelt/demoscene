"""Call one engine routine of the original in unicorn (from a snapshot's memory) and return the memory after it:
a direct check of a decoded routine. Used here for the look-at matrix 0x414cec."""
import math, pickle, struct, sys
import numpy as np
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import *
sys.path.insert(0, '.')
import engine as E

def machine(snap):
    s = pickle.load(open(snap, 'rb'))
    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(0, 0x4000000)
    mu.mem_write(0, s['mem'])
    mu.context_restore(s['ctx'])
    mu.reg_write(UC_X86_REG_FPCW, 0x127f)
    return mu

def call(mu, fn, regs, stack=b'', scratch=0x3f00000):
    sp = scratch + 0x8000
    ret = scratch                         # return address: a hlt-free sentinel we stop at
    data = struct.pack('<I', ret) + stack
    sp -= len(data)
    mu.mem_write(sp, data)
    mu.reg_write(UC_X86_REG_ESP, sp)
    for r, v in regs.items():
        mu.reg_write(r, v)
    mu.emu_start(fn, ret, count=10_000_000)

if __name__ == '__main__':
    mu = machine(sys.argv[1])
    bad = 0
    rng = np.random.default_rng(1)
    for k in range(200):
        d = rng.normal(size=3) * rng.uniform(1, 5000)
        mu.mem_write(0x3f10000, struct.pack('<3d', *d))
        call(mu, 0x414cec, {UC_X86_REG_EAX: 0x3f10100, UC_X86_REG_EDX: 0x3f10000})
        orig = struct.unpack('<9d', mu.mem_read(0x3f10100, 72))
        # model: d / |d|; r0 = normalise(-d.z, 0, d.x); r1 = normalise(d x r0); rows r0, r1, d
        n = d / math.sqrt((d[1] * d[1] + d[0] * d[0]) + d[2] * d[2])
        r0 = np.array([-n[2], 0.0, n[0]]); r0 = r0 / math.sqrt((r0[1] * r0[1] + r0[0] * r0[0]) + r0[2] * r0[2])
        r1 = np.array([n[1] * r0[2] - n[2] * r0[1], n[2] * r0[0] - n[0] * r0[2], n[0] * r0[1] - n[1] * r0[0]])
        r1 = r1 / math.sqrt((r1[1] * r1[1] + r1[0] * r1[0]) + r1[2] * r1[2])
        mine = list(r0) + list(r1) + list(n)
        err = max(abs(a - b) for a, b in zip(orig, mine))
        if err > 1e-12:
            bad += 1
            if bad < 4:
                print('dir', d, '\n orig', np.round(orig, 6), '\n mine', np.round(mine, 6))
    print('%d of 200 differ (more than 1e-12)' % bad)

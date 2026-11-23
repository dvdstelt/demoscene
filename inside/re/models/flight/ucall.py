"""Call a routine of INSIDE.EXE in unicorn on the memory of a snapshot (models/flight helper).

    u = UCall('snaps/f7063.snap'); st0 = u.call(0x4012e0, eax=5, stack=[struct float bits]) ...
Returns registers and the x87 st(0) (read through a small fstp stub).
"""
import os
import pickle
import struct
import sys

import unicorn  # noqa: F401
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
STUB = 0x7e800          # fstp qword [0x7e900]; hlt
OUT = 0x7e900
ESP = 0x7d000


class UCall:
    def __init__(self, snap=os.path.join(RE, 'snaps', 'f7063.snap')):
        s = pickle.load(open(snap, 'rb'))
        self.mu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.mu.mem_map(0, 0x4000000)
        self.mu.mem_write(0, s['mem'])
        self.mu.context_restore(s['ctx'])
        self.mu.mem_write(STUB, b'\xdd\x1d' + struct.pack('<I', OUT) + b'\xf4')

    def call(self, addr, eax=0, edx=0, ebx=0, ecx=0, stack=b'', fpu_result=True):
        mu = self.mu
        sp = ESP - len(stack)
        mu.mem_write(sp, stack)
        sp -= 4
        mu.mem_write(sp, struct.pack('<I', STUB if fpu_result else STUB + 6))
        for r, v in ((UC_X86_REG_EAX, eax), (UC_X86_REG_EDX, edx), (UC_X86_REG_EBX, ebx), (UC_X86_REG_ECX, ecx),
                     (UC_X86_REG_ESP, sp), (UC_X86_REG_EBP, sp)):
            mu.reg_write(r, v & 0xffffffff)
        mu.emu_start(addr, STUB + 6, count=50_000_000)
        return struct.unpack('<d', mu.mem_read(OUT, 8))[0] if fpu_result else mu.reg_read(UC_X86_REG_EAX)

    def rd(self, a, n): return bytes(self.mu.mem_read(a, n))
    def wr(self, a, b): self.mu.mem_write(a, b)

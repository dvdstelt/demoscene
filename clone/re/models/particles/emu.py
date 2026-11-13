"""Call single routines of CLONE.EXE in unicorn on the memory of a snapshot (for checking the particle models).

    e = Emu('t11148')                  # snaps/t11148.snap: memory, LFB and CPU context (GDT, segments)
    e.call(0x405527)                   # run part 1's frame routine once (fninit + control word 0x1f7f first)
    e.call(0x403df7, eax=buf, stack=[f32(160), f32(100), f32(0.9), f32(-0.025), 0])
    e.rd(a, n), e.wr(a, bytes), e.rd32/wr32/rdf/wrf, e.buf(a, n) -> numpy uint8

Stack arguments are dwords, first argument first (it ends up at [esp+4] when the routine starts). Nothing else
runs: no timer interrupt, no events; the caller sets dt ([0x420489]) and the event variables itself.
"""
import os
import pickle
import struct

import numpy as np
import unicorn  # noqa: F401  (the snapshot's context unpickles into unicorn objects)
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
RAM = 0x2000000
LFB = 0xe0000000
LFB_SIZE = 0x200000
STUB = 0x3fe000          # fninit; fldcw [STUB+0x100]; call target   (free memory below the image at 0x400000)
STACK = 0x3fd000


def f32(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


class Emu:
    def __init__(self, snap):
        path = snap if os.path.exists(snap) else os.path.join(RE, 'snaps', snap + '.snap')
        s = pickle.load(open(path, 'rb'))
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_32)
        mu.mem_map(0, RAM)
        mu.mem_map(LFB, LFB_SIZE)
        mu.mem_write(0, s['mem'])
        mu.mem_write(LFB, s['lfb'])
        mu.context_restore(s['ctx'])
        self.ctx = mu.context_save()
        self.icount = 0

    def call(self, target, eax=0, edx=0, ebx=0, ecx=0, esi=0, edi=0, stack=(), count=0):
        mu = self.mu
        code = b'\xdb\xe3' + b'\xd9\x2d' + struct.pack('<I', STUB + 0x100)
        call_at = STUB + len(code)
        code += b'\xe8' + struct.pack('<i', target - (call_at + 5))
        end = STUB + len(code)
        mu.mem_write(STUB, code)
        mu.mem_write(STUB + 0x100, struct.pack('<H', 0x1f7f))
        sp = STACK - 4 * len(stack)
        mu.mem_write(sp, b''.join(struct.pack('<I', v & 0xffffffff) for v in stack))
        for r, v in ((UC_X86_REG_EAX, eax), (UC_X86_REG_EDX, edx), (UC_X86_REG_EBX, ebx), (UC_X86_REG_ECX, ecx),
                     (UC_X86_REG_ESI, esi), (UC_X86_REG_EDI, edi), (UC_X86_REG_EBP, STACK + 0x100),
                     (UC_X86_REG_ESP, sp)):
            mu.reg_write(r, v & 0xffffffff)
        mu.emu_start(STUB, end, count=count)
        return mu.reg_read(UC_X86_REG_EAX)

    def rd(self, a, n): return bytes(self.mu.mem_read(a, n))
    def wr(self, a, b): self.mu.mem_write(a, bytes(b))
    def rd32(self, a): return struct.unpack('<I', self.rd(a, 4))[0]
    def rds32(self, a): return struct.unpack('<i', self.rd(a, 4))[0]
    def wr32(self, a, v): self.wr(a, struct.pack('<I', v & 0xffffffff))
    def rdf(self, a): return struct.unpack('<f', self.rd(a, 4))[0]
    def wrf(self, a, v): self.wr(a, struct.pack('<f', v))
    def buf(self, a, n): return np.frombuffer(self.rd(a, n), dtype=np.uint8).copy()

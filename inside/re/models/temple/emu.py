"""Call single routines of INSIDE.EXE in unicorn, on the bare image or on a snapshot's memory.
   e = Emu(); e.call(0x4145f4, eax=buf, stack=[...dwords...]); e.mat(buf)"""
import os, struct, sys
import numpy as np
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import *
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
IMG = open(os.path.join(RE, 'unpacked', 'inside_img.bin'), 'rb').read()
STOP = 0x7fff0


class Emu:
    def __init__(self, snapmem=None):
        u = Uc(UC_ARCH_X86, UC_MODE_32)
        u.mem_map(0, 0x4000000)
        if snapmem is not None:
            u.mem_write(0, bytes(snapmem))
        else:
            u.mem_write(0x400000, IMG)
        u.mem_write(STOP, b'\xf4')
        self.u = u
        self.scratch = 0x3000000

    def alloc(self, n):
        a = self.scratch; self.scratch += (n + 15) & ~15; return a

    def call(self, addr, eax=0, edx=0, ebx=0, ecx=0, esi=0, edi=0, stack=(), esp=0x3f00000, count=50_000_000):
        u = self.u
        sp = esp - 4 * len(stack)
        u.mem_write(sp, struct.pack('<%dI' % len(stack), *[s & 0xffffffff for s in stack]))
        sp -= 4
        u.mem_write(sp, struct.pack('<I', STOP))
        for r, v in ((UC_X86_REG_EAX, eax), (UC_X86_REG_EDX, edx), (UC_X86_REG_EBX, ebx), (UC_X86_REG_ECX, ecx),
                     (UC_X86_REG_ESI, esi), (UC_X86_REG_EDI, edi), (UC_X86_REG_ESP, sp), (UC_X86_REG_EBP, sp)):
            u.reg_write(r, v & 0xffffffff)
        u.emu_start(addr, STOP, count=count)
        return u.reg_read(UC_X86_REG_EAX)

    def rd(self, a, n): return bytes(self.u.mem_read(a, n))
    def wr(self, a, b): self.u.mem_write(a, b)
    def mat(self, a): return np.array(struct.unpack('<9d', self.rd(a, 72))).reshape(3, 3)
    def vec(self, a): return np.array(struct.unpack('<3d', self.rd(a, 24)))


def dpush(*vals):
    """stack dwords for double arguments (first argument at the lowest address)"""
    out = []
    for v in vals:
        lo, hi = struct.unpack('<II', struct.pack('<d', v))
        out += [lo, hi]
    return out

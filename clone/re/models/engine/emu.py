"""Minimal unicorn harness: call one routine of clone_img.bin (or of a dump's memory) with chosen registers and
stack arguments, with the FPU set up as the intro sets it (control word 0x1f7f: 64-bit precision, truncate)."""
import os
import struct

import numpy as np
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
IMG = open(os.path.join(RE, 'unpacked', 'clone_img.bin'), 'rb').read()
BASE = 0x400000
RAM = 0x2000000
RET = 0x7fff0
FPU_INIT = 0x7fe00            # fninit; fldcw [0x7fe10]; ret  (with the word 0x1f7f at 0x7fe10)


class Emu:
    def __init__(self, mem=None, lfb=None):
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_32)
        mu.mem_map(0, RAM)
        if mem is not None:          # memory image from linear 0x400000 (a dump) or from 0 (a snapshot)
            mu.mem_write(BASE if len(mem) < RAM else 0, bytes(mem) if len(mem) < RAM else bytes(mem[:RAM]))
        else:
            mu.mem_write(BASE, IMG)
        if lfb is not None:
            mu.mem_map(0xe0000000, 0x200000)
            mu.mem_write(0xe0000000, bytes(lfb))
        mu.mem_write(RET, b'\xf4')
        mu.mem_write(FPU_INIT, b'\xdb\xe3\xd9\x2d' + struct.pack('<I', FPU_INIT + 0x10) + b'\xc3')
        mu.mem_write(FPU_INIT + 0x10, struct.pack('<H', 0x1f7f))
        self.esp0 = 0xf93940 - 0x100
        self.call(FPU_INIT)

    def call(self, addr, eax=0, edx=0, ebx=0, ecx=0, esi=0, edi=0, stack=()):
        mu = self.mu
        esp = self.esp0 - 4 * len(stack) - 4
        mu.mem_write(esp, struct.pack('<I', RET) + b''.join(
            struct.pack('<f', v) if isinstance(v, float) else struct.pack('<I', v & 0xffffffff) for v in stack))
        for r, v in ((UC_X86_REG_EAX, eax), (UC_X86_REG_EDX, edx), (UC_X86_REG_EBX, ebx), (UC_X86_REG_ECX, ecx),
                     (UC_X86_REG_ESI, esi), (UC_X86_REG_EDI, edi), (UC_X86_REG_ESP, esp), (UC_X86_REG_EBP, 0)):
            mu.reg_write(r, v & 0xffffffff)
        mu.emu_start(addr, RET, count=500_000_000)
        return mu.reg_read(UC_X86_REG_EAX)

    def rd(self, a, n):
        return bytes(self.mu.mem_read(a, n))

    def wr(self, a, b):
        self.mu.mem_write(a, bytes(b))

    def floats(self, a, n):
        return np.frombuffer(self.rd(a, 4 * n), '<f4').copy()

    def wfloats(self, a, v):
        self.wr(a, np.asarray(v, '<f4').tobytes())

"""Run JIZZ.EXE's texture interpreter 0x12e72 in unicorn on one script and record the work buffers after every op.

The memory comes from the tick-430 snapshot (precalc done: buffer pointers, colour shifts), the progress print
0x1115c is patched to a ret, and the FPU is set up as main does (fninit, precision control 24 bit).

    from emu import run_script
    states = run_script(0x12b5d)    # list of (script address of the next op, W pointer, {name: bytes})
"""
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, RE)

import unicorn  # noqa: E402  (the snapshot pickle needs it)
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EAX, UC_X86_REG_ESP  # noqa: E402

STUB = 0x7e000
_mem = None


def snapshot_mem():
    global _mem
    if _mem is None:
        _mem = bytes(pickle.load(open(os.path.join(RE, 'snaps', 't430.snap'), 'rb'))['mem'])
    return _mem


def rd32(m, a): return struct.unpack_from('<I', m, a)[0]


def make_uc(mem=None):
    mem = mem or snapshot_mem()
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(0, len(mem))
    uc.mem_write(0, mem)
    uc.mem_write(0x1115c, b'\xc3')
    return uc


def run_script(script, dest=0x6582, record=True, uc=None, extra=None):
    """Run 0x12e72 with ESI = script, EDI = [dest]; return the per-op states (B0..B4 and W)."""
    uc = uc or make_uc()
    m = snapshot_mem()
    bufs = [rd32(m, 0x12425 + 4 * i) for i in range(5)]
    code = bytes([0xdb, 0xe3])                                      # fninit
    code += bytes([0xd9, 0x2d]) + struct.pack('<I', STUB + 0x100)   # fldcw [STUB+0x100]
    call_at = STUB + len(code)
    code += b'\xe8' + struct.pack('<i', 0x12e72 - (call_at + 5))
    hlt = STUB + len(code)
    code += b'\xf4'
    uc.mem_write(STUB, code)
    uc.mem_write(STUB + 0x100, struct.pack('<H', 0x037f & 0xfcff))
    uc.reg_write(UC_X86_REG_ESI, script)
    uc.reg_write(UC_X86_REG_EDI, rd32(m, dest))
    uc.reg_write(UC_X86_REG_EAX, 0)
    uc.reg_write(UC_X86_REG_ESP, 0x7d000)
    states = []

    def hook(uc_, addr, size, _):
        esi = uc_.reg_read(UC_X86_REG_ESI)
        w = struct.unpack('<I', uc_.mem_read(0x12e46, 4))[0]
        d = {f'B{i}': bytes(uc_.mem_read(b, 0x30000)) for i, b in enumerate(bufs)}
        states.append((esi, w, d))

    if record:
        uc.hook_add(UC_HOOK_CODE, hook, begin=0x12ebb, end=0x12ebb)
    if extra:
        extra(uc)
    uc.emu_start(STUB, hlt)
    final = bytes(uc.mem_read(rd32(m, dest), 0x30000))
    return states, final, bufs, uc

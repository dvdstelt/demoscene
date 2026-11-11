import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_INSN
from unicorn.x86_const import *
from snap import load
SENT = 0x7ffff0
class Emu:
    def __init__(self, tick):
        s = load(tick)
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_32)
        mu.mem_map(0, 0x4000000)
        mu.mem_write(0, s['mem'])
        mu.context_restore(s['ctx'])
        self.eip = s['eip']
        mu.hook_add(UC_HOOK_INSN, lambda mu, port, size, _: 0, None, 1, 0, UC_X86_INS_IN)
        mu.hook_add(UC_HOOK_INSN, lambda mu, port, size, val, _: None, None, 1, 0, UC_X86_INS_OUT)
    def rd(self, a, n): return bytes(self.mu.mem_read(a, n))
    def d(self, a): return struct.unpack('<I', self.rd(a, 4))[0]
    def i(self, a): return struct.unpack('<i', self.rd(a, 4))[0]
    def ii(self, a, n): return list(struct.unpack('<%di' % n, self.rd(a, 4 * n)))
    def ff(self, a, n): return list(struct.unpack('<%df' % n, self.rd(a, 4 * n)))
    def wi(self, a, v): self.mu.mem_write(a, struct.pack('<i', v))
    def run_to(self, addr, maxn=200_000_000):
        """continue main line until eip == addr (hook-based)"""
        hit = {}
        def h(mu, a, size, _):
            hit['x'] = 1; mu.emu_stop()
        hh = self.mu.hook_add(UC_HOOK_CODE, h, None, addr, addr)
        # step one instruction first if we are already at addr
        self.mu.emu_start(self.eip, 0xffffffff, count=maxn)
        self.mu.hook_del(hh)
        self.eip = self.mu.reg_read(UC_X86_REG_EIP)
        assert self.eip == addr, hex(self.eip)
    def call(self, addr, **regs):
        """call a routine with a sentinel return address, keep main context"""
        ctx = self.mu.context_save()
        for k, v in regs.items():
            self.mu.reg_write(getattr(__import__('unicorn.x86_const', fromlist=['x']), 'UC_X86_REG_' + k.upper()), v)
        esp = self.mu.reg_read(UC_X86_REG_ESP) - 0x100
        self.mu.mem_write(esp, struct.pack('<I', SENT))
        self.mu.reg_write(UC_X86_REG_ESP, esp)
        self.mu.emu_start(addr, SENT, count=500_000_000)
        out = {r: self.mu.reg_read(getattr(__import__('unicorn.x86_const', fromlist=['x']), 'UC_X86_REG_' + r.upper())) for r in ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp')}
        self.mu.context_restore(ctx)
        return out

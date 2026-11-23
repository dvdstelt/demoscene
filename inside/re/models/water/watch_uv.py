"""Run a few refreshes from snaps/f8468 and print which code writes the u,v words (+0x28) of one obuke vertex and of
one fish vertex (to find the engine's environment-mapping routine)."""
import os, sys, runpy
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EIP

import water_hooks
orig_install = water_hooks.install
seen = {}


def install(mu, st, a):
    orig_install(mu, st, a)
    import struct
    r32 = lambda x: struct.unpack('<I', bytes(mu.mem_read(x, 4)))[0]
    ob = 0x10ebaf0                 # [0x4265fc] and [0x426570] in snaps/f8468 (memory is loaded after install)
    fish = 0x10f779c
    targets = {}

    def on_write(uc, access, addr, size, value, ud):
        for t, name in targets.items():
            if addr <= t < addr + size or t <= addr < t + 4:
                eip = uc.reg_read(UC_X86_REG_EIP)
                key = (name, eip)
                if seen.get(key, 0) < 3:
                    print('write %s at eip %#x size %d value %#x refresh %d' % (name, eip, size, value, st['frame']))
                seen[key] = seen.get(key, 0) + 1
    vb = ob + 0x10c

    def on_any(uc, access, addr, size, value, ud):
        off = (addr - vb) % 64
        eip = uc.reg_read(UC_X86_REG_EIP)
        key = ('obuke+%#x' % off, eip)
        seen[key] = seen.get(key, 0) + 1
    mu.hook_add(UC_HOOK_MEM_WRITE, on_any, None, vb, vb + 287 * 64 - 1)
    print('watching %#x..%#x' % (vb, vb + 287 * 64), flush=True)

    def on_cnt(uc, access, addr, size, value, ud):
        seen[('counter', uc.reg_read(UC_X86_REG_EIP))] = seen.get(('counter', uc.reg_read(UC_X86_REG_EIP)), 0) + 1
    mu.hook_add(UC_HOOK_MEM_WRITE, on_cnt, None, 0x4248c4, 0x4248c7)


water_hooks.install = install
sys.argv = ['run_inside.py', '--load', '--snap', '../../snaps/f8468.snap', '--out', 'watch', '--frames', '8472',
            '--water-dump', 'watch.pkl', '--no-frames']
runpy.run_path('run_inside.py', run_name='__main__')
for k, v in sorted(seen.items()): print(k[0], hex(k[1]), v)

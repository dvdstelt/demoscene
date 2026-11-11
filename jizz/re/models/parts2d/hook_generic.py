"""Hooks for run_hook.py: per frame (at the flip call 0x651f... configured below) record timer reads, chosen
memory ranges and registers at chosen addresses. Configure with the file <out>.cfg (a python literal):
{'frame_end': addr, 'grabs': {addr: [('name', ptr_or_addr, size, deref), ...]}, 'regs': {addr: ['eax', ...]}}
deref = True: ptr_or_addr is a pointer variable (read the dword, then size bytes there); 'PAGE' = the draw page."""
import ast
import os
import pickle
import struct

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, \
    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP

REGS = {'eax': UC_X86_REG_EAX, 'ebx': UC_X86_REG_EBX, 'ecx': UC_X86_REG_ECX, 'edx': UC_X86_REG_EDX,
        'esi': UC_X86_REG_ESI, 'edi': UC_X86_REG_EDI, 'ebp': UC_X86_REG_EBP}
OUT = None
frames = []
cur = {'timers': []}


def install(mu, st, a):
    global OUT
    OUT = a.out + '.hook.pkl'
    cfg = ast.literal_eval(open(a.out + '.cfg').read())

    def rd(addr, n): return bytes(mu.mem_read(addr, n))
    def u32(addr): return struct.unpack('<I', rd(addr, 4))[0]

    def on_timer(mu, addr, size, _):
        ret = u32(mu.reg_read(UC_X86_REG_ESP) + 4)
        cur['timers'].append((ret, mu.reg_read(UC_X86_REG_EAX), st['tick']))

    def make_grab(addr, items, regs):
        def h(mu, a_, size, _):
            for name, p, n, deref in items:
                if p == 'PAGE': base = u32(0x2060)
                elif deref: base = u32(p)
                else: base = p
                cur[name] = rd(base, n)
            for r in regs:
                cur['%x_%s' % (addr, r)] = mu.reg_read(REGS[r])
            cur.setdefault('ticks', []).append((addr, st['tick']))
        return h

    def on_end(mu, addr, size, _):
        cur['tick'] = st['tick']
        frames.append(dict(cur))
        cur.clear()
        cur['timers'] = []

    mu.hook_add(UC_HOOK_CODE, on_timer, None, 0x29f61, 0x29f61)
    addrs = set(cfg.get('grabs', {})) | set(cfg.get('regs', {}))
    for ad in addrs:
        mu.hook_add(UC_HOOK_CODE, make_grab(ad, cfg.get('grabs', {}).get(ad, []), cfg.get('regs', {}).get(ad, [])),
                    None, ad, ad)
    mu.hook_add(UC_HOOK_CODE, on_end, None, cfg['frame_end'], cfg['frame_end'])


def finish():
    pickle.dump(frames, open(OUT, 'wb'))
    print('hook_generic: %d frames -> %s' % (len(frames), OUT))
